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
            _db->add_cloud(name, type, cloud_data);
            if (type == "GoogleDrive") {
                _clouds.emplace_back(
                    std::make_unique<GoogleCloud>(
                        cloud_data["client_id"],
                        cloud_data["client_secret"],
                        cloud_data["refresh_token"],
                        dir_path,
                        cloud_data["start_page_token"]));
                }
            }
        std::cout << "bebra" << '\n';
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
            _clouds.resize(clouds.size());
            if (type == "GoogleDrive") {
                _clouds[cloud_id - 1] =
                    std::make_unique<GoogleCloud>(
                        cloud_data["client_id"],
                        cloud_data["client_secret"],
                        cloud_data["refresh_token"],
                        dir_id,
                        cloud_data["start_page_token"]);
            }
            else if (type == "DropBox") {
                
            }
            else if (type == "OneDrive") {
                
            }
        }
        std::cout << "bebra not" << '\n';
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
    std::vector<std::pair<int, std::string>> dirs_to_map;

    std::vector<CURLSH*> shared_handles;
    int cloud_id = 1;
    for (const auto& cloud : _clouds) {
        CURLSH* shared_handle = curl_share_init();
        curl_share_setopt(shared_handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(shared_handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        shared_handles.emplace_back(std::move(shared_handle));
        cloud_id++;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(_loc_path)) {
        const auto entry_path = entry.path();
        const auto ftime = std::filesystem::last_write_time(entry_path);
        const auto stime = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
        const auto time = std::chrono::system_clock::to_time_t(stime);
        if (entry.is_regular_file()) {
            int cloud_id_2 = 1;
            int global_id = _db->add_file(entry_path, "file", time);
            for (const auto& cloud : _clouds) {
                std::unique_ptr<CurlEasyHandle> easy_handle = cloud->create_file_upload_handle(entry_path);
                easy_handle->_file_info.emplace(entry_path, "file", time, global_id, cloud_id_2);
                curl_easy_setopt(easy_handle->_curl, CURLOPT_SHARE, shared_handles[cloud_id_2-1]);
                curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEDATA, &(easy_handle->_responce));
                
                _files_curl_queue.emplace(std::move(easy_handle));
                cloud_id_2++;
            }
        }
        else if (entry.is_directory()) {
            int cloud_id_2 = 1;
            int global_id = _db->add_file(entry_path, "dir", time);
            dirs_to_map.emplace_back(global_id, std::filesystem::relative(entry_path.parent_path(), _loc_path).string());
            for (const auto& cloud : _clouds) {
                std::unique_ptr<CurlEasyHandle> easy_handle = cloud->create_dir_upload_handle(entry_path);
                easy_handle->_file_info.emplace(entry_path, "dir", time, global_id, cloud_id_2);
                curl_easy_setopt(easy_handle->_curl, CURLOPT_SHARE, shared_handles[cloud_id_2-1]);
                curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEDATA, &(easy_handle->_responce));

                {
                    std::lock_guard<std::mutex> lock(_small_curl_mutex);
                    _small_curl_queue.emplace(std::move(easy_handle));
                }
                _small_curl_CV.notify_one();
                cloud_id_2++;
            }
        }
    }
    _small_curl_finished = true;
    _small_curl_CV.notify_all();
    multi_worker_dirs.join();

    _file_link_finished = true;
    _file_link_CV.notify_all();
    dir_link_adder.join();

    std::cout << "finished dirs" << '\n';

    _file_link_finished = false;
    std::thread file_link_adder(&SyncHandler::async_db_add_file_link, this);

    _files_curl_finished = false;
    std::thread multi_worker_files(&SyncHandler::async_curl_multi_files_worker, this);

    cloud_id = 1;
    for (const auto& cloud : _clouds) {
        for (const auto& dir_id_path_pair : dirs_to_map) {
            std::string cloud_file_id = _db->get_cloud_file_id_by_cloud_id(cloud_id, dir_id_path_pair.first);
            std::unique_ptr<CurlEasyHandle> patch_handle = cloud->patch_change_parent(cloud_file_id, dir_id_path_pair.second);
            _small_curl_queue.emplace(std::move(patch_handle));
            _db->update_file_link_one_field(cloud_id, dir_id_path_pair.first, "cloud_parent_id", cloud->get_path_id_mapping(dir_id_path_pair.second));
        }
        cloud_id++;
    }

    _files_curl_finished = true;
    _files_curl_CV.notify_all();
    multi_worker_files.join();
    std::cout << "finished files" << '\n';

    std::thread multi_worker_patch(&SyncHandler::async_curl_multi_small_worker, this);

    _small_curl_CV.notify_all();
    multi_worker_patch.join();


    for (auto& shared_handle : shared_handles) {
        curl_share_cleanup(shared_handle);
    }
    curl_global_cleanup();

    cloud_id = 1;
    for (const auto& cloud : _clouds) {
        std::string pt = cloud->post_upload();
        nlohmann::json cloud_data = _db->get_cloud_config(cloud_id);
        cloud_data["start_page_token"] = pt;
        cloud_data["dir"] = cloud->get_home_dir_id();
        _db->update_cloud_data(cloud_id, cloud_data);
        cloud_id++;
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
    while (!_small_curl_finished || !_small_curl_queue.empty() || still_running > 0) {
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
                    _active_handles_map[easy]->_responce = "";
                    _active_handles_map[easy]->retry_count++;
                    schedule_retry(std::move(_active_handles_map[easy]));
                    _active_handles_map.erase(easy);
                }
                /* else if (upload_incomplete) {                                             <-------- TODO resumable upload
                    ...
                } */
                else if (_active_handles_map[easy]->_file_info.has_value()) {
                    nlohmann::json json_rsp = nlohmann::json::parse(_active_handles_map[easy]->_responce);
                    FileLinkRecord file_link_data(std::move(*_active_handles_map[easy]->_file_info));
                    file_link_data.cloud_file_id = json_rsp["id"];
                    file_link_data.parent_id = "root";
                    file_link_data.modified_time = convert_google_time(json_rsp["modifiedTime"]);

                    _clouds[file_link_data.cloud_id - 1]->insert_path_id_mapping(
                        std::filesystem::relative(_db->find_path_by_global_id(file_link_data.global_id),_loc_path),
                            file_link_data.cloud_file_id);


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
    while (!_files_curl_finished || !_files_curl_queue.empty() || still_running > 0) {
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
                    _active_handles_map.erase(easy);
                }
                else if (http_code == 403 || http_code == 429 || http_code == 408 || http_code >= 500 && http_code < 600) {
                    _active_handles_map[easy]->_responce = "";
                    _active_handles_map[easy]->retry_count++;
                    schedule_retry(std::move(_active_handles_map[easy]));
                    _active_handles_map.erase(easy);
                }
                else if (_active_handles_map[easy]->_file_info.has_value()) {
                    nlohmann::json json_rsp = nlohmann::json::parse(_active_handles_map[easy]->_responce);
                    FileLinkRecord file_link_data(std::move(*_active_handles_map[easy]->_file_info));
                    file_link_data.cloud_file_id = json_rsp["id"];
                    file_link_data.modified_time = convert_google_time(json_rsp["modifiedTime"]);
                    std::filesystem::path path(_db->find_path_by_global_id(file_link_data.global_id));
                    std::string rel_parent_path = std::filesystem::relative(path.parent_path(), _loc_path).string();

                    std::unique_ptr<CurlEasyHandle> patch_handle = _clouds[file_link_data.cloud_id-1]->patch_change_parent(file_link_data.cloud_file_id, rel_parent_path);
                    _small_curl_queue.emplace(std::move(patch_handle));

                    file_link_data.parent_id = _clouds[file_link_data.cloud_id-1]->get_path_id_mapping(rel_parent_path);
                    {
                        std::lock_guard<std::mutex> lock(_file_link_mutex);
                        _file_link_queue.emplace(std::move(file_link_data));
                    }
                    _file_link_CV.notify_one();
                    _active_handles_map.erase(easy);
                }
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
    _changes_finished = false;
    std::thread changes_thread(&SyncHandler::async_procces_changes, this);
    int cloud_id = 1;
    for (const auto& cloud : _clouds) {
        std::vector<nlohmann::json> pages = cloud->list_changes();
        {
            std::lock_guard<std::mutex> lock(_file_link_mutex);
            for (const auto& page : pages) {
                for (const auto& change : page) {
                    nlohmann::json file_change;
                    if (change.contains("file")) {
                        file_change = change["file"];
                    }
                    else {
                        throw std::runtime_error("weird change file: " + change.dump());
                    }
                    nlohmann::json tmp;
                    if (file_change.contains("id")) {
                        tmp["cloud_file_id"] = file_change["id"];
                    }
                    if (file_change.contains("mimeType")) {
                        tmp["type"] = file_change["mimeType"] == "application/vnd.google-apps.folder" ? "dir" : "file";
                    }
                    if (file_change.contains("modifiedTime")) {
                        tmp["cloud_file_modified_time"] = convert_google_time(file_change["modifiedTime"]);
                    }
                    if (file_change.contains("name")) {
                        tmp["name"] = file_change["name"];
                    }
                    if (file_change.contains("parents")) {
                        tmp["cloud_parent_id"] = file_change["parents"][0];
                    }
                    if (file_change.contains("trashed")) {
                        tmp["deleted"] = file_change["trashed"];
                    }
                    tmp["cloud_id"] = cloud_id;
                    std::cout << change << '\n' << tmp << '\n';
                    _changes_queue.emplace(std::move(tmp));
                    tmp.clear();
                }
            }
        }
        _changes_CV.notify_one();
        pages.clear();
        cloud_id++;
    }

    {
        std::lock_guard<std::mutex> lock(_changes_mutex);
        _changes_finished = true;
    }
    _changes_CV.notify_one();
    changes_thread.join();
}

void SyncHandler::async_procces_changes() {
    std::unique_ptr<Database> db_conn = std::make_unique<Database>(_db_file_name);
    nlohmann::json curr_change;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(_changes_mutex);
            _changes_CV.wait(lock, [this] { return !_changes_queue.empty() || _changes_finished; });

            if (!_changes_queue.empty()) {
                curr_change = std::move(_changes_queue.front());
                _changes_queue.pop();
            }

        }
        if (!curr_change.empty()) {
            nlohmann::json cloud_file_info = db_conn->get_cloud_file_info(curr_change["cloud_file_id"], curr_change["cloud_id"]);
            if (cloud_file_info.empty()) {
                // new file
                // not our file
            }
            else { // can be different changes at the same time!!!!!!!!!
                // file changed
                    // file renamed
                        // adding ?global_id? to ??MAP?? with rename tag + name + cloud
                    // file REALLY changed
                        // adding ?global_id? to ??MAP?? with changed tag + time + cloud
                // file moved
                    // adding ?global_id? to ??MAP?? with moved tag + parent + cloud
                // file deleted
                    // adding ?global_id? to ??MAP?? with deleted tag + cloud (time not changing at least in google)
                // Nothing happend what really matters
                    // just skipping
            }

            
            curr_change.clear();
        }
        else if (_changes_finished) {
            
            break;
        }
    }
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
    int delay = BASE_DELAY * (1 << easy_handle->retry_count);
    
    auto delay_timer = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
    easy_handle->timer = delay_timer;
    _delayed_vec.emplace_back(std::move(easy_handle));
    std::cout << "Запрос отложен на " << delay << " мс, будет выполнен в "
        << std::chrono::duration_cast<std::chrono::milliseconds>(delay_timer.time_since_epoch()).count()
        << " мс\n";
}

