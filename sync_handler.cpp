#include "sync_handler.hpp"

inline static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

SyncHandler::SyncHandler(const std::string config_name, const std::filesystem::path& loc_path, const bool IS_INITIAL) : _loc_path(loc_path), _db_file_name(config_name + ".sqlite3") {
    if (IS_INITIAL) {
        _db = std::make_unique<Database>(_db_file_name);
        std::ifstream config_file(config_name);
        std::stringstream buf;
        buf << config_file.rdbuf();
        const auto config_json = nlohmann::json::parse(buf.str());

        const auto ftime = std::filesystem::last_write_time(_loc_path);
        const auto stime = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
        const auto time = std::chrono::system_clock::to_time_t(stime);

        int global_id = _db->add_file(_loc_path, "dir", time);

        for (const auto& cloud : config_json["clouds"]) {
            std::string name = cloud["name"];
            std::string type = cloud["type"];
            const nlohmann::json cloud_data = cloud["data"];
            const std::filesystem::path dir_path(cloud_data["dir"]);
            int cloud_id = _db->add_cloud(name, type, cloud_data);
            if (type == "GoogleDrive") {
                _clouds.emplace(
                    cloud_id,
                    std::make_unique<GoogleDrive>(
                        cloud_data["client_id"],
                        cloud_data["client_secret"],
                        cloud_data["refresh_token"],
                        dir_path));
            }
            if (type == "Dropbox") {
                _clouds.emplace(
                    cloud_id,
                    std::make_unique<Dropbox>(
                        cloud_data["client_id"],
                        cloud_data["client_secret"],
                        cloud_data["refresh_token"],
                        dir_path));
            }
        }

        initial_upload();
    }
    else {
        _db = std::make_unique<Database>(_db_file_name);
        const std::vector<nlohmann::json> clouds = _db->get_clouds();
        for (const auto& cloud : clouds) {
            const int cloud_id = cloud["config_id"];
            const std::string type = cloud["type"];
            const nlohmann::json cloud_data = cloud["config_data"];
            const std::string dir_id = cloud_data["dir"];
            if (type == "GoogleDrive") {
                _clouds.emplace(cloud_id,
                    std::make_unique<GoogleDrive>(
                        cloud_data["client_id"],
                        cloud_data["client_secret"],
                        cloud_data["refresh_token"],
                        dir_id,
                        cloud_data["start_page_token"]));
            }
            else if (type == "Dropbox") {
                _clouds.emplace(cloud_id,
                    std::make_unique<Dropbox>(
                        cloud_data["client_id"],
                        cloud_data["client_secret"],
                        cloud_data["refresh_token"],
                        dir_id,
                        cloud_data["start_page_token"]));
            }
            else if (type == "OneDrive") {

            }
        }
        std::cout << "dfsdfg" << '\n';
        sync();
    }



}

const std::filesystem::path& SyncHandler::get_local_path() const {
    return _loc_path;
}
SyncHandler::~SyncHandler() {}

void SyncHandler::initial_upload() {
    _small_curl_finished = false;
    std::thread multi_worker_dirs(&SyncHandler::async_curl_multi_small_worker, this);
    _file_link_finished = false;
    std::thread dir_link_adder(&SyncHandler::async_db_add_file_link, this);
    std::vector<std::pair<int, std::filesystem::path>> dirs_to_map;
    std::vector<std::pair<int, std::filesystem::path>> files_to_map;

    std::vector<CURLSH*> shared_handles;

    for (const auto& [cloud_id, cloud] : _clouds) {
        CURLSH* shared_handle = curl_share_init();
        curl_share_setopt(shared_handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(shared_handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        shared_handles.emplace_back(std::move(shared_handle));
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(_loc_path)) {
        const auto entry_path = entry.path();
        const auto ftime = std::filesystem::last_write_time(entry_path);
        const auto stime = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
        const auto time = std::chrono::system_clock::to_time_t(stime);
        if (entry.is_regular_file()) {
            int global_id = _db->add_file(entry_path, "file", time);
            files_to_map.emplace_back(global_id, entry_path);
        }
        else if (entry.is_directory()) {
            int global_id = _db->add_file(entry_path, "dir", time);
            dirs_to_map.emplace_back(global_id, entry_path);
            for (const auto& [cloud_id, cloud] : _clouds) {
                std::unique_ptr<CurlEasyHandle> easy_handle = cloud->create_dir_upload_handle(std::filesystem::relative(entry_path, _loc_path));
                if (easy_handle) {
                    easy_handle->_file_info.emplace("dir", global_id, cloud_id, "root", std::filesystem::relative(entry_path, _loc_path));
                    curl_easy_setopt(easy_handle->_curl, CURLOPT_SHARE, shared_handles[cloud_id - 1]);
                    {
                        std::lock_guard<std::mutex> lock(_small_curl_mutex);
                        _small_curl_queue.emplace(std::move(easy_handle));
                    }
                    _small_curl_CV.notify_one();
                }
            }
        }
    }
    _small_curl_finished = true;
    _small_curl_CV.notify_all();
    multi_worker_dirs.join();

    _file_link_finished = true;
    _file_link_CV.notify_all();
    dir_link_adder.join();

    _file_link_finished = false;
    std::thread file_link_adder(&SyncHandler::async_db_add_file_link, this);

    _small_curl_finished = false;
    std::thread multi_worker_patch(&SyncHandler::async_curl_multi_small_worker, this);

    for (const auto& [cloud_id, cloud] : _clouds) {
        if (_db->get_cloud_type(cloud_id) == "GoogleDrive") {
            for (const auto& dir_id_path_pair : dirs_to_map) {
                std::string rel_parent_path = std::filesystem::relative(dir_id_path_pair.second.parent_path(), _loc_path).string();
                std::string cloud_file_id = _db->get_cloud_file_id_by_cloud_id(cloud_id, dir_id_path_pair.first);
                std::unique_ptr<CurlEasyHandle> patch_handle = cloud->create_parent_update_handle(cloud_file_id, rel_parent_path);
                curl_easy_setopt(patch_handle->_curl, CURLOPT_SHARE, shared_handles[cloud_id - 1]);
                _small_curl_queue.emplace(std::move(patch_handle));
                _db->update_file_link_one_field(cloud_id, dir_id_path_pair.first, "cloud_parent_id", cloud->get_path_id_mapping(rel_parent_path));
            }
        }
        for (const auto& file_id_path_pair : files_to_map) {
            std::string rel_parent_path = std::filesystem::relative(file_id_path_pair.second.parent_path(), _loc_path).string();
            std::unique_ptr<CurlEasyHandle> easy_handle = cloud->create_file_upload_handle(file_id_path_pair.second, rel_parent_path);
            easy_handle->_file_info.emplace("file", file_id_path_pair.first, cloud_id, cloud->get_path_id_mapping(rel_parent_path), file_id_path_pair.second);
            curl_easy_setopt(easy_handle->_curl, CURLOPT_SHARE, shared_handles[cloud_id - 1]);
            _files_curl_queue.emplace(std::move(easy_handle));
        }
    }
    dirs_to_map.clear();
    files_to_map.clear();

    _small_curl_finished = true;
    _small_curl_CV.notify_all();
    multi_worker_patch.join();

    std::cout << "finished dirs" << '\n';

    _files_curl_finished = false;
    std::thread multi_worker_files(&SyncHandler::async_curl_multi_files_worker, this);

    _files_curl_finished = true;
    _files_curl_CV.notify_all();
    multi_worker_files.join();

    std::cout << "finished files" << '\n';

    for (auto& shared_handle : shared_handles) {
        curl_share_cleanup(shared_handle);
    }
    curl_global_cleanup();

    for (const auto& [cloud_id, cloud] : _clouds) {
        std::string pt = cloud->post_upload();
        nlohmann::json cloud_data = _db->get_cloud_config(cloud_id);
        cloud_data["start_page_token"] = pt;
        cloud_data["dir"] = cloud->get_home_dir_id();
        _db->update_cloud_data(cloud_id, cloud_data);
    }

    _file_link_finished = true;
    _file_link_CV.notify_all();
    file_link_adder.join();
}

void SyncHandler::async_curl_multi_small_worker() {
    std::cout << "started dirs" << '\n';
    CURLM* multi_handle = curl_multi_init();
    int still_running = 0;
    _small_active_count = 0;
    while (!_small_curl_finished || !_small_curl_queue.empty() || still_running > 0 || !_delayed_vec.empty()) {
        {
            std::unique_lock<std::mutex> lock(_small_curl_mutex);
            _small_curl_CV.wait(lock, [this] { return !_small_curl_queue.empty() || _small_curl_finished; });
            while (!_small_curl_queue.empty() && _small_active_count < _SMALL_MAX_ACTIVE) {
                std::unique_ptr<CurlEasyHandle> easy_handle = std::move(_small_curl_queue.front());
                _small_curl_queue.pop();
                CURL* easy = easy_handle->_curl;
                curl_multi_add_handle(multi_handle, easy);
                {
                    std::unique_lock<std::mutex> lock(_handles_map_mutex);
                    _active_handles_map.insert({ easy_handle->_curl, std::move(easy_handle) });
                }
                _small_active_count++;
            }
        }
        CURLMcode mc = curl_multi_perform(multi_handle, &still_running);
        if (mc != CURLM_OK) {
            std::cerr << "curl_multi_perform() failed, code: " << mc << '\n';
            break;
        }

        int numfds = 0;
        mc = curl_multi_wait(multi_handle, nullptr, 0, 100, &numfds);
        if (mc != CURLM_OK) {
            std::cerr << "curl_multi_wait() failed, code: " << mc << '\n';
            break;
        }
        CURLMsg* msg;
        int msgs_left;
        while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
            if (msg->msg == CURLMSG_DONE) {
                CURL* easy = msg->easy_handle;
                curl_multi_remove_handle(multi_handle, easy);
                _small_active_count--;

                long http_code = 0;
                curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);

                if (msg->data.result != CURLE_OK) {
                    std::cerr << "Request failed: "
                        << curl_easy_strerror(msg->data.result) << "\n";
                    std::cout << "http code: " << http_code << '\n';
                    _active_handles_map.erase(easy);
                }
                else if (http_code == 403 || http_code == 429 || http_code == 408 || http_code >= 500 && http_code < 600) {
                    _active_handles_map[easy]->_response = "";
                    _active_handles_map[easy]->retry_count++;
                    schedule_retry(std::move(_active_handles_map[easy]));
                    _active_handles_map.erase(easy);
                }
                else if (http_code != 200) {
                    std::cout << "http code: " << http_code << '\n';
                    std::cout << "small response: " << _active_handles_map[easy]->_response << '\n';
                }
                /* else if (upload_incomplete) {                                             <-------- TODO resumable upload
                    ...
                } */
                else if (_active_handles_map[easy]->_file_info.has_value()) {
                    std::cout << "small response: " << _active_handles_map[easy]->_response << '\n';
                    FileLinkRecord file_link_data(std::move(*_active_handles_map[easy]->_file_info));
                    _clouds[file_link_data.cloud_id]->procces_response(file_link_data, nlohmann::json::parse(_active_handles_map[easy]->_response));
                    if (file_link_data.parent_id == "root") {
                        _clouds[file_link_data.cloud_id]->insert_path_id_mapping(
                            std::filesystem::relative(_db->find_path_by_global_id(file_link_data.global_id), _loc_path),
                            file_link_data.cloud_file_id);
                    }
                    {
                        std::lock_guard<std::mutex> lock(_file_link_mutex);
                        _file_link_queue.emplace(std::move(file_link_data));
                    }
                    _file_link_CV.notify_one();
                    _active_handles_map.erase(easy);
                }
            }
        }
        check_delayed_requests("SMALL");
    }
    curl_multi_cleanup(multi_handle);
}

void SyncHandler::async_curl_multi_files_worker() {
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "started files" << '\n';
    CURLM* multi_handle = curl_multi_init();
    int still_running = 0;
    _files_active_count = 0;
    while (!_files_curl_finished || !_files_curl_queue.empty() || still_running > 0 || !_delayed_vec.empty()) {
        {
            std::unique_lock<std::mutex> lock(_files_curl_mutex);
            _files_curl_CV.wait(lock, [this] { return !_files_curl_queue.empty() || _files_curl_finished; });
            while (!_files_curl_queue.empty() && _files_active_count < _FILES_MAX_ACTIVE) {
                std::unique_ptr<CurlEasyHandle> easy_handle = std::move(_files_curl_queue.front());
                _files_curl_queue.pop();
                CURL* easy = easy_handle->_curl;
                curl_multi_add_handle(multi_handle, easy);
                _active_handles_map.insert({ easy, std::move(easy_handle) });
                _files_active_count++;
            }
        }
        CURLMcode mc = curl_multi_perform(multi_handle, &still_running);
        if (mc != CURLM_OK) {
            std::cerr << "curl_multi_perform() failed, code: " << mc << '\n';
            break;
        }

        int numfds = 0;
        mc = curl_multi_wait(multi_handle, nullptr, 0, 100, &numfds);
        if (mc != CURLM_OK) {
            std::cerr << "curl_multi_wait() failed, code: " << mc << '\n';
            break;
        }
        CURLMsg* msg;
        int msgs_left;
        while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
            if (msg->msg == CURLMSG_DONE) {
                CURL* easy = msg->easy_handle;
                curl_multi_remove_handle(multi_handle, easy);
                _files_active_count--;

                long http_code = 0;
                curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);

                if (msg->data.result != CURLE_OK) {
                    std::cerr << "Request failed: "
                        << curl_easy_strerror(msg->data.result) << "\n";
                    std::cout << "http code: " << http_code << '\n';
                }
                else if (http_code == 403 || http_code == 429 || http_code == 408 || http_code >= 500 && http_code < 600) {
                    std::cout << "file response: " << _active_handles_map[easy]->_response << '\n';
                    _active_handles_map[easy]->_response = "";
                    _active_handles_map[easy]->retry_count++;
                    schedule_retry(std::move(_active_handles_map[easy]));
                }
                else if (http_code != 200) {
                    std::cout << "http code: " << http_code << '\n';
                    std::cout << "files response: " << _active_handles_map[easy]->_response << '\n';
                }
                else if (_active_handles_map[easy]->_file_info.has_value()) {
                    std::cout << "file response: " << _active_handles_map[easy]->_response << '\n';
                    FileLinkRecord file_link_data(std::move(*_active_handles_map[easy]->_file_info));
                    _clouds[file_link_data.cloud_id]->procces_response(file_link_data, nlohmann::json::parse(_active_handles_map[easy]->_response));

                    {
                        std::lock_guard<std::mutex> lock(_file_link_mutex);
                        _file_link_queue.emplace(std::move(file_link_data));
                    }
                    _file_link_CV.notify_one();
                }
                _active_handles_map.erase(easy);
            }
        }
        check_delayed_requests("FILES");
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "Время выполнения загрузки: " << (double)duration / 1000 << " с" << std::endl;
    curl_multi_cleanup(multi_handle);
    _file_link_finished = true;
    _file_link_CV.notify_one();
}


void SyncHandler::async_db_add_file_link() {
    std::unique_ptr<Database> db_conn = std::make_unique<Database>(_db_file_name);
    int batch_size = 25;
    std::vector<FileLinkRecord> batch{};
    while (true) {
        {
            std::unique_lock<std::mutex> lock(_file_link_mutex);
            _file_link_CV.wait(lock, [this] { return !_file_link_queue.empty() || _file_link_finished; });

            while (!_file_link_queue.empty() && batch.size() < batch_size) {
                batch.emplace_back(std::move(_file_link_queue.front()));
                _file_link_queue.pop();
                if (batch.back().info == "change") {
                    FileLinkRecord curr(std::move(batch.back()));
                    std::cout << curr.cloud_id << curr.global_id << curr.hash_check_sum << curr.modified_time << curr.parent_id << '\n';
                    db_conn->update_file_link(curr.cloud_id, curr.global_id, curr.hash_check_sum, curr.modified_time, curr.parent_id, curr.cloud_file_id);
                    batch.pop_back();
                }
            }

        }
        if (!_file_link_queue.empty() && batch.size() == batch_size) {
            db_conn->initial_add_file_links(batch);
            batch.clear();
        }
        else if (_file_link_finished) {
            db_conn->initial_add_file_links(batch);
            break;
        }
    }
}

void SyncHandler::sync() {
    std::shared_ptr<Database> db_shared_conn = std::make_shared<Database>(_db_file_name);

    std::unordered_map<std::string, nlohmann::json> all_changes;

    for (const auto& [cloud_id, cloud] : _clouds) {
        std::vector<nlohmann::json> curr_cloud_changes = cloud->get_changes(cloud_id, db_shared_conn);

        for (const auto& change : curr_cloud_changes) {
            if (all_changes.contains(change["file"])) {                                                                 // TODO more sound logic (now its stupid for transparency)
                if (all_changes[change["file"]]["tag"] == "DELETED" && change["data"]["tag"] == "CHANGED") {            // if maybe both DELETED then dont delete on second one too?
                    continue;                                                                                           // if both CHANGED choose one that newer?
                }
                else if (all_changes[change["file"]["data"]]["tag"] == "CHANGED" && change["data"]["tag"] == "DELETED") {
                    all_changes[change["file"]] = change["data"];
                    continue;
                }
                else {
                    continue;
                }
            }
            else {
                all_changes.emplace(change["file"], change["data"]);
            }
        }
    }

    std::vector<CURLSH*> shared_handles;
    for (const auto& [cloud_id, cloud] : _clouds) {
        CURLSH* shared_handle = curl_share_init();
        curl_share_setopt(shared_handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(shared_handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        shared_handles.emplace_back(std::move(shared_handle));
    }

    _small_curl_finished = false;
    std::thread multi_worker_small(&SyncHandler::async_curl_multi_small_worker, this);

    _file_link_finished = false;
    std::thread file_links_worker(&SyncHandler::async_db_add_file_link, this);

    std::cout << all_changes.size() << '\n';
    for (const auto& [file, data] : all_changes) {
        std::cout << file << " " << data << '\n';
        int cloud_id = data["cloud_id"];
        if (data["tag"] == "NEW") {
            std::filesystem::path path(file);
            int parent_global_id = _db->get_global_id_by_cloud_id(cloud_id, data["cloud_parent_id"]);
            int global_id = _db->add_file(path, data["type"], 0);
            _db->add_file_link(global_id, cloud_id, data["cloud_hash_check_sum"], data["cloud_parent_id"], data["cloud_file_modified_time"], data["cloud_file_id"]);
            if (data["type"] == "file") {
                std::unique_ptr<CurlEasyHandle> download_handle = _clouds[cloud_id]->create_file_download_handle(data["cloud_file_id"], path);
                curl_easy_setopt(download_handle->_curl, CURLOPT_SHARE, shared_handles[cloud_id - 1]);
                {
                    std::lock_guard<std::mutex> lock(_small_curl_mutex);
                    _small_curl_queue.emplace(std::move(download_handle));
                }
                _small_curl_CV.notify_one();
                for (const auto& [_cloud_id, cloud] : _clouds) {
                    if (_cloud_id != cloud_id) {
                        std::string parent_cloud_id = _db->get_cloud_file_id_by_cloud_id(_cloud_id, parent_global_id);
                        std::unique_ptr<CurlEasyHandle> easy_handle = cloud->create_file_upload_handle(path, parent_cloud_id);
                        easy_handle->_file_info.emplace(data["type"], global_id, _cloud_id, parent_cloud_id, "");
                        easy_handle->_file_info->info = "change";
                        curl_easy_setopt(easy_handle->_curl, CURLOPT_SHARE, shared_handles[_cloud_id - 1]);
                        _files_curl_queue.emplace(std::move(easy_handle));
                    }
                }
            }
            else {
                std::filesystem::create_directory(path);
                const auto ftime = std::filesystem::last_write_time(file);
                const auto stime = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
                const auto time = std::chrono::system_clock::to_time_t(stime);
                _db->update_file_time(global_id, "local_modified_time", time);
                for (const auto& [_cloud_id, cloud] : _clouds) {
                    if (_cloud_id != cloud_id) {
                        std::string parent_cloud_id = _db->get_cloud_file_id_by_cloud_id(_cloud_id, parent_global_id);
                        std::unique_ptr<CurlEasyHandle> easy_handle = cloud->create_dir_upload_handle(path, parent_cloud_id);
                        easy_handle->_file_info.emplace(data["type"], global_id, _cloud_id, parent_cloud_id, "");
                        easy_handle->_file_info->info = "change";
                        curl_easy_setopt(easy_handle->_curl, CURLOPT_SHARE, shared_handles[_cloud_id - 1]);
                        {
                            std::lock_guard<std::mutex> lock(_small_curl_mutex);
                            _small_curl_queue.emplace(std::move(easy_handle));
                        }
                        _small_curl_CV.notify_one();
                    }
                }
            }
        }
        else if (data["tag"] == "CHANGED") {
            int global_id = data["global_id"];
            std::filesystem::path path = _db->find_path_by_global_id(global_id);
            bool is_changed = data.contains("changed");
            bool is_renamed = data.contains("renamed");
            bool is_moved = data.contains("moved");

            _db->update_file_link(cloud_id, global_id, data["cloud_hash_check_sum"], data["cloud_file_modified_time"], data["cloud_parent_id"], data["cloud_file_id"]);

            if (is_renamed) {
                std::filesystem::path old_path = path;
                std::filesystem::path path = old_path.parent_path().string() + "/" + data["name"].get<std::string>();
                std::filesystem::rename(old_path, path);
                _db->update_file_path(global_id, "path", path.string());
                for (const auto& [_cloud_id, cloud] : _clouds) {
                    if (_cloud_id != cloud_id) {
                        std::string cloud_file_id = _db->get_cloud_file_id_by_cloud_id(_cloud_id, global_id);
                        std::unique_ptr<CurlEasyHandle> metadata_handle = cloud->create_name_update_handle(cloud_file_id, data["name"]);
                        curl_easy_setopt(metadata_handle->_curl, CURLOPT_SHARE, shared_handles[_cloud_id - 1]);
                        metadata_handle->_file_info.emplace(data["type"], global_id, _cloud_id, "", "");
                        metadata_handle->_file_info->info = "change";
                        {
                            std::lock_guard<std::mutex> lock(_small_curl_mutex);
                            _small_curl_queue.emplace(std::move(metadata_handle));
                        }
                        _small_curl_CV.notify_one();
                    }
                }
            }
            if (is_moved) {
                int new_parent_global_id = _db->get_global_id_by_cloud_id(cloud_id, data["cloud_parent_id"]);
                std::filesystem::path old_path = path;
                std::filesystem::path path = _db->find_path_by_global_id(new_parent_global_id).string() + "/" + data["name"].get<std::string>();
                std::filesystem::rename(old_path, path);
                _db->update_file_path(global_id, "path", path.string());
                for (const auto& [_cloud_id, cloud] : _clouds) {
                    if (_cloud_id != cloud_id) {
                        std::string cloud_file_id = _db->get_cloud_file_id_by_cloud_id(_cloud_id, global_id);
                        std::string new_parent_cloud_id = _db->get_cloud_file_id_by_cloud_id(_cloud_id, new_parent_global_id);
                        std::string old_parent_cloud_id = _db->get_cloud_parent_id_by_cloud_id(_cloud_id, cloud_file_id);
                        std::unique_ptr<CurlEasyHandle> metadata_handle = cloud->create_parent_update_handle(cloud_file_id, new_parent_cloud_id, old_parent_cloud_id);
                        metadata_handle->_file_info.emplace(data["type"], global_id, _cloud_id, new_parent_cloud_id, "");
                        metadata_handle->_file_info->info = "change";
                        curl_easy_setopt(metadata_handle->_curl, CURLOPT_SHARE, shared_handles[_cloud_id - 1]);
                        {
                            std::lock_guard<std::mutex> lock(_small_curl_mutex);
                            _small_curl_queue.emplace(std::move(metadata_handle));
                        }
                        _small_curl_CV.notify_one();
                    }
                }
            }
            if (is_changed) {
                std::unique_ptr<CurlEasyHandle> download_handle = _clouds[cloud_id]->create_file_download_handle(data["cloud_file_id"], path);
                curl_easy_setopt(download_handle->_curl, CURLOPT_SHARE, shared_handles[cloud_id - 1]);
                {
                    std::lock_guard<std::mutex> lock(_small_curl_mutex);
                    _small_curl_queue.emplace(std::move(download_handle));
                }
                _small_curl_CV.notify_one();
                for (const auto& [_cloud_id, cloud] : _clouds) {
                    if (_cloud_id != cloud_id) {
                        std::string cloud_file_id = _db->get_cloud_file_id_by_cloud_id(_cloud_id, global_id);
                        std::unique_ptr<CurlEasyHandle> update_handle = cloud->create_file_update_handle(cloud_file_id, path, data["name"]);
                        update_handle->_file_info.emplace(data["type"], global_id, _cloud_id, "", "");
                        update_handle->_file_info->info = "change";
                        curl_easy_setopt(update_handle->_curl, CURLOPT_SHARE, shared_handles[_cloud_id - 1]);
                        _files_curl_queue.emplace(std::move(update_handle));
                    }
                }
            }
        }
        else if (data["tag"] == "DELETED") {
            int global_id = data["global_id"];
            std::filesystem::remove(file);
            for (const auto& [_cloud_id, cloud] : _clouds) {
                if (_cloud_id != cloud_id) {
                    std::string cloud_file_id = _db->get_cloud_file_id_by_cloud_id(_cloud_id, global_id);
                    std::unique_ptr<CurlEasyHandle> delete_handle = cloud->create_file_delete_handle(cloud_file_id);
                    curl_easy_setopt(delete_handle->_curl, CURLOPT_SHARE, shared_handles[_cloud_id - 1]);
                    {
                        std::lock_guard<std::mutex> lock(_small_curl_mutex);
                        _small_curl_queue.emplace(std::move(delete_handle));
                    }
                    _small_curl_CV.notify_one();
                }
                _db->delete_file_and_links(global_id);
            }
        }
    }
    std::cout << "finished files" << '\n';

    _small_curl_finished = true;
    _small_curl_CV.notify_all();
    multi_worker_small.join();

    _files_curl_finished = false;
    std::thread multi_worker_files(&SyncHandler::async_curl_multi_files_worker, this);

    _files_curl_finished = true;
    _files_curl_CV.notify_all();
    multi_worker_files.join();

    _file_link_finished = true;
    _files_curl_CV.notify_all();
    file_links_worker.join();

    for (auto& shared_handle : shared_handles) {
        curl_share_cleanup(shared_handle);
    }
    curl_global_cleanup();
}

void SyncHandler::check_delayed_requests(const std::string& type) {
    auto now = std::chrono::steady_clock::now();
    const int MAX_RETRY = 5;

    if (type == "SMALL") {
        auto it = _delayed_vec.begin();
        while (it != _delayed_vec.end()) {
            if (it->get()->timer <= now) {
                if (it->get()->retry_count > MAX_RETRY) {
                    throw std::runtime_error("Too many retry attempts on a request");
                }
                else {
                    {
                        std::unique_lock<std::mutex> lock(_small_curl_mutex);
                        _small_curl_queue.emplace(std::move(*it));
                        it = _delayed_vec.erase(it);
                    }
                    _small_curl_CV.notify_all();
                }
            }
            else {
                ++it;
            }
        }
    }
    else {
        auto it = _delayed_vec.begin();
        while (it != _delayed_vec.end()) {
            if (it->get()->timer <= now) {
                if (it->get()->retry_count > MAX_RETRY) {
                    throw std::runtime_error("Too many retry attempts on a request");
                }
                else {
                    it->get()->retry_count++;
                    {
                        std::unique_lock<std::mutex> lock(_files_curl_mutex);
                        _files_curl_queue.emplace(std::move(*it));
                        it = _delayed_vec.erase(it);
                    }
                    _files_curl_CV.notify_all();
                }
            }
            else {
                ++it;
            }
        }
    }
}

void SyncHandler::schedule_retry(std::unique_ptr<CurlEasyHandle> easy_handle) {
    int BASE_DELAY = 500;
    int delay = BASE_DELAY * (1 << easy_handle->retry_count + 1);

    if (easy_handle->_ifc.is_open()) {
        easy_handle->_ifc.clear();
        easy_handle->_ifc.seekg(0, std::ios::beg);
    }

    int jitter_range = easy_handle->retry_count * BASE_DELAY;
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(-jitter_range, jitter_range);

    delay += dist(gen);

    auto delay_timer = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
    easy_handle->timer = delay_timer;
    _delayed_vec.emplace_back(std::move(easy_handle));
    std::cout << "Запрос отложен на " << delay << " мс, будет выполнен в "
        << std::chrono::duration_cast<std::chrono::milliseconds>(delay_timer.time_since_epoch()).count()
        << " мс\n";
}

