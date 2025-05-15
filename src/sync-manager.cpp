/* 
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
    for (const auto& [cloud_id, cloud] : _clouds) {
        std::string pt = cloud->post_upload();
        nlohmann::json cloud_data = _db->get_cloud_config(cloud_id);
        cloud_data["start_page_token"] = pt;
        cloud_data["dir"] = cloud->get_home_dir_id();
        _db->update_cloud_data(cloud_id, cloud_data);
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

*/


#include "sync-manager.h"



SyncManager::SyncManager(
    const std::string& config_path,
    const std::string& db_file,
    const std::filesystem::path& local_dir,
    Mode mode
) :
    _config_path(config_path),
    _db_file(db_file),
    _local_dir(local_dir),
    _mode(mode),
    _should_exit(false)
{
    _db = std::make_shared<Database>(_db_file);
    loadConfig();

    setRawSignal();

    HttpClient::get().setClouds(_clouds);
    CallbackDispatcher::get().setDB(_db_file);
    CallbackDispatcher::get().setClouds(_clouds);
}

SyncManager::SyncManager(
    const std::string& db_file,
    Mode mode
) :
    _db_file(db_file),
    _mode(mode),
    _should_exit(false)
{
    _db = std::make_shared<Database>(_db_file);
}

void SyncManager::run() {
    
    if (_mode == Mode::InitialSync) {
        initialSync();
    }
    else {
        init();
        daemonMode();
    }
}

void SyncManager::shutdown() {
    _local->stopWatching();

    _should_exit = true;
    if (_polling_worker && _polling_worker->joinable()) {
        _polling_worker->join();
    }
    _changes_buff.close();
    if (_changes_worker && _changes_worker->joinable()) {
        _changes_worker->join();
    }

    HttpClient::get().shutdown();
}

void SyncManager::init() {
    loadConfig();
    setupClouds();
    setRawSignal();

    HttpClient::get().setClouds(_clouds);
    CallbackDispatcher::get().setDB(_db_file);
    CallbackDispatcher::get().setClouds(_clouds);
}

void SyncManager::loadConfig() {
    std::ifstream config_file(_config_path);
    if (!config_file) {
        throw std::runtime_error("Config file not found");
    }
    nlohmann::json config_json;
    config_file >> config_json;
    _cloud_configs = config_json["clouds"];
}

void SyncManager::loadDBConfig() {
    std::ifstream config_file(_config_path);
    if (!config_file) {
        throw std::runtime_error("Config file not found");
    }
    nlohmann::json config_json;
    config_file >> config_json;
    _cloud_configs = config_json["clouds"];
}

void SyncManager::setupClouds() {
    for (const auto& cloud : _cloud_configs) {
        std::string name = cloud["name"];
        std::string type_str = cloud["type"];
        CloudProviderType type = cloud_type_from_string(type_str);
        nlohmann::json cloud_data = cloud["data"];
        std::filesystem::path cloud_home_path(cloud_data["dir"]);
        int cloud_id = _db->add_cloud(name, type, cloud_data);
        if (type == CloudProviderType::GoogleDrive) {
            _clouds.emplace(
                cloud_id,
                std::make_shared<GoogleDrive>(
                    cloud_data["client_id"],
                    cloud_data["client_secret"],
                    cloud_data["refresh_token"],
                    cloud_home_path,
                    _local_dir,
                    _db,
                    cloud_id));
            CloudResolver::registerCloud(cloud_id, "GoogleDrive");
        }
        else if (type == CloudProviderType::Dropbox) {
            _clouds.emplace(
                cloud_id,
                std::make_shared<Dropbox>(
                    cloud_data["client_id"],
                    cloud_data["client_secret"],
                    cloud_data["refresh_token"],
                    cloud_home_path,
                    _local_dir,
                    _db,
                    cloud_id));
            CloudResolver::registerCloud(cloud_id, "Dropbox");
        }
    }

    _local = std::make_shared<LocalStorage>(_local_dir, 0, _db);
    _clouds.emplace(0, _local);
    CloudResolver::registerCloud(0, "LocalStorage");
}

void SyncManager::setLocalDir(const std::string& path) {
    std::filesystem::path input_path = path;
    std::filesystem::path absolute_path = std::filesystem::canonical(input_path);

    if (std::filesystem::exists(absolute_path)) {
        if (!std::filesystem::is_directory(absolute_path)) {
            throw std::runtime_error("This is not a directory: " + input_path.string());
        }
    }
    else {
        std::filesystem::create_directories(absolute_path);
    }

    _local_dir = absolute_path;
    _local = std::make_shared<LocalStorage>(_local_dir, 0, _db);
}

bool SyncManager::directoryIsWritable(const std::filesystem::path& dir) const {
    try {
        std::filesystem::path test_file = dir / ".tmp_permission_check";
        std::ofstream tmp(test_file);
        if (!tmp.is_open()) {
            return false;
        }
        tmp.close();
        std::filesystem::remove(test_file);
        return true;
    }
    catch (...) {
        return false;
    }
}

std::vector<std::filesystem::path> SyncManager::checkLocalPermissions() const {
    std::vector<std::filesystem::path> inaccessible;

    if (!std::filesystem::exists(_local_dir) || !std::filesystem::is_directory(_local_dir)) {
        throw std::runtime_error("Error! Path inaccessible or not a directory: " + _local_dir.string());
    }

    if (!this->directoryIsWritable(_local_dir)) {
        throw std::runtime_error("Error! Cannot write to directory: " + _local_dir.string());
    }

    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(_local_dir, std::filesystem::directory_options::none, ec);
    std::filesystem::recursive_directory_iterator end;

    while (it != end) {
        const auto& path = it->path();

        try {
            if (std::filesystem::is_directory(path)) {
                if (!this->directoryIsWritable(path)) {
                    inaccessible.push_back(path);
                }
            }
            else if (std::filesystem::is_regular_file(path)) {
                std::ofstream test(path, std::ios::app);
                if (!test.is_open()) {
                    inaccessible.push_back(path);
                }
            }
        }
        catch (const std::filesystem::filesystem_error&) {
            inaccessible.push_back(path);
        }

        it.increment(ec);
        if (ec) {
            inaccessible.push_back(it->path());
        }
    }

    return inaccessible;
}

void SyncManager::changeFinished() {
    
}

void SyncManager::initialSync() {

    std::unordered_map<std::filesystem::path, std::unique_ptr<FileRecordDTO>> initial_files;
    std::vector<std::unique_ptr<FileRecordDTO>> tmp_files;
    for (auto& [cloud_id, cloud] : _clouds) {
        tmp_files = cloud->initialFiles();
        for (auto& file : tmp_files) {
            auto& existing = initial_files[file->rel_path];
            if (!existing || existing->cloud_file_modified_time < file->cloud_file_modified_time) {
                existing.reset(file.release());
            }
        }
    }

    std::unordered_map<std::filesystem::path, std::unique_ptr<FileRecordDTO>> local_files_map;
    tmp_files = _local->initialFiles();
    for (auto& file : tmp_files) {
        local_files_map.emplace(file->rel_path, std::move(file));
    }

    for (const auto& [path, cloud_dto] : initial_files) {
        if (!local_files_map.contains(path)) {
            auto change = ChangeFactory::createInitial<
                CloudDownloadNewCommand,
                FileRecordDTO
            >(
                ChangeType::New,
                cloud_dto->cloud_file_modified_time,
                cloud_dto->cloud_id,
                [this](std::vector<std::unique_ptr<Change>>&& dependents) {
                    SyncManager::changeFinished();
                },
                std::move(cloud_dto)
            );

            handleChange(std::move(change));
        }
    }

    for (auto& [rel_path, local_dto] : local_files_map) {
        if (!initial_files.count(rel_path)) {
            
        }
    }





    _clouds.emplace(0, _local);
    CloudResolver::registerCloud(0, "LocalStorage");

    _db->markInitialSyncDone();
}

void SyncManager::ensureRootsExist() {
    if (!std::filesystem::exists(_local_dir)) {
        std::filesystem::create_directories(_local_dir);
    }

    for (auto& [cid, cloud] : _clouds) {
        cloud->ensureRootExists();
    }
}

void SyncManager::registerCloud(const std::string& cloud_name, const CloudProviderType type, const std::string& client_id, const std::string& client_secret, const std::filesystem::path& home_path) {
    this->setupLocalHttpServer();

    nlohmann::json json_config_data;
    json_config_data["client_id"] = client_id;
    json_config_data["client_secret"] = client_secret;
    json_config_data["refresh_token"] = "";
    json_config_data["dir"] = home_path;

    int cloud_id = _db->add_cloud(cloud_name, type, json_config_data);

    _clouds.emplace(
        cloud_id,
        CloudFactory::create(
            type,
            client_id,
            client_secret,
            home_path,
            _local_dir,
            _db,
            cloud_id
        )
    );

    std::string url = _clouds[cloud_id]->buildAuthURL(_http_server->getPort());
    std::cout << "If not opened, click the link here:\n" << url << '\n';
    openUrl(url);
    std::string code = _http_server->waitForCode();
    
    _clouds[cloud_id]->getRefreshToken(code, _http_server->getPort());
    _clouds[cloud_id]->ensureRootExists();

    _http_server->stopListening();

}

void SyncManager::openUrl(const std::string& url) {
#if defined(_WIN32)
    std::string cmd = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
    std::string cmd = "open \"" + url + "\"";
#else
    std::string cmd = "xdg-open \"" + url + "\"";
#endif
    std::system(cmd.c_str());
    LOG_INFO("REGISTER", "Opened auth URL: %s", url.c_str());
}

void SyncManager::refreshAccessTokens() {

}

void SyncManager::daemonMode() {
    if (!checkInitialSyncCompleted()) {
        throw std::runtime_error("Initial sync not completed. Please run initial sync first.");
    }

    _polling_worker = std::make_unique<std::thread>(&SyncManager::pollingLoop, this);
    _changes_worker = std::make_unique<std::thread>(&SyncManager::proccessLoop, this);

    _local->startWatching();
}

bool SyncManager::checkInitialSyncCompleted() {
    return _db->isInitialSyncDone();
}

void SyncManager::setupLocalHttpServer() {
    _http_server = std::make_unique<LocalHttpServer>(8443);
    _http_server->startListening();
    LOG_INFO("Main", "Http server listening on port: %i", _http_server->getPort());
}

void SyncManager::proccessLoop() {
    std::unique_ptr<Change> change;
    while (_changes_buff.pop(change)) {

    }
}

void SyncManager::pollingLoop() {
    auto now = std::chrono::steady_clock::now();
    auto next_poll = now;
    auto next_flush = now + std::chrono::seconds(8);

    while (!_should_exit) {

        std::unique_lock lk(_raw_signal->mtx);
        _raw_signal->cv.wait_until(lk,
            std::min(next_poll, next_flush),
            [&] { return _should_exit || anyStorageHasRaw(); });
        lk.unlock();

        now = std::chrono::steady_clock::now();

        if (now >= next_flush) {
            for (auto& [id, cloud] : _clouds) {
                _changes_buff.push(cloud->flushOldDeletes());
            }
            next_flush = now + std::chrono::seconds(8);
        }

        /* if (now >= next_poll) {
            for (auto& [id, cloud] : _clouds) {
                HttpClient::get().submit();
            }
            next_poll = now + std::chrono::seconds(60);
        } */

        for (auto& [id, cloud] : _clouds) {
            if (cloud->hasChanges()) {
                _changes_buff.push(cloud->proccessChanges());
            }
        }
    }
}

bool SyncManager::anyStorageHasRaw() {
    for (auto& [id, cloud] : _clouds) {
        if (cloud->hasChanges()) {
            return true;
        }
    }
    return false;
}


void handleChange(std::unique_ptr<Change> change) {

}

void SyncManager::setRawSignal() {
    for (auto& cloud : _clouds) {
        cloud.second->setRawSignal(_raw_signal);
    }
}