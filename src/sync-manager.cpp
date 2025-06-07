#include "sync-manager.h"


SyncManager::SyncManager(
    const std::string& config_path,
    const std::string& db_file,
    Mode mode
) :
    _config_path(config_path),
    _db_file(db_file),
    _should_exit(false),
    _mode(mode)
{
    _db = std::make_shared<Database>(_db_file);
    if (mode == Mode::InitialSync) {
        loadConfig();
    }
}

SyncManager::SyncManager(
    const std::string& db_file,
    Mode mode
) :
    _db_file(db_file),
    _should_exit(false),
    _mode(mode)
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
    setupClouds();

    _clouds.emplace(0, _local);

    for (auto& [cloud_id, cloud] : _clouds) {
        cloud->setOnChange([this]() {
            _signal_dirty.store(true, std::memory_order_release);
            _signal_cv.notify_one();
            });
    }

    ChangeFactory::initClouds(_clouds);

    CallbackDispatcher::get().setDB(_db_file);
    CallbackDispatcher::get().setClouds(_clouds);
    CallbackDispatcher::get().start();
    HttpClient::get().setClouds(_clouds);
    HttpClient::get().start();
}

void SyncManager::loadConfig() {
    std::ifstream config_file(_config_path);
    if (!config_file) {
        throw std::runtime_error("Config file not found");
    }
    nlohmann::json config_json;
    config_file >> config_json;
    _cloud_configs = config_json["clouds"];
    _local_dir = config_json["local"].get<std::string>();

    for (const auto& cloud : _cloud_configs) {
        std::string name = cloud["name"];
        std::string type_str = cloud["type"];
        CloudProviderType type = cloud_type_from_string(type_str);
        nlohmann::json cloud_data = cloud["data"];
        std::filesystem::path cloud_home_path(cloud_data["dir"].get<std::string>());
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

    _num_clouds = _clouds.size();

    _local = std::make_shared<LocalStorage>(_local_dir, 0, _db);
    CloudResolver::registerCloud(0, "LocalStorage");
}

void SyncManager::setupClouds() {
    auto clouds = _db->get_clouds();

    this->setLocalDir(_db->getLocalDir());

    for (auto& cloud : clouds) {
        int cloud_id = cloud["config_id"];
        std::string type_str = cloud["type"].get<std::string>();
        CloudProviderType type = cloud_type_from_string(type_str);
        nlohmann::json cloud_data = cloud["config_data"];
        std::filesystem::path cloud_home_path(cloud_data["dir"].get<std::string>());

        std::string client_id = cloud_data["client_id"].get<std::string>();
        std::string client_secret = cloud_data["client_secret"].get<std::string>();
        std::string refresh_token = cloud_data["refresh_token"].get<std::string>();
        std::string delta_token = cloud_data["start_page_token"].get<std::string>();

        _clouds.emplace(
            cloud_id,
            CloudFactory::create(
                type,
                client_id,
                client_secret,
                refresh_token,
                cloud_home_path,
                _local_dir,
                _db,
                cloud_id,
                delta_token
            )
        );
        CloudResolver::registerCloud(cloud_id, to_cstr(type));
    }

    _num_clouds = _clouds.size();

    _local = std::make_shared<LocalStorage>(_local_dir, 0, _db);
    CloudResolver::registerCloud(0, "Local");
}

void SyncManager::setLocalDir(const std::string& path) {
    std::filesystem::path input_path = path;
    std::filesystem::path absolute_path = std::filesystem::absolute(input_path);

    if (std::filesystem::exists(absolute_path)) {
        if (!std::filesystem::is_directory(absolute_path)) {
            throw std::runtime_error("This is not a directory: " + input_path.string());
        }
    }
    else {
        std::filesystem::create_directories(absolute_path);
    }

    _local_dir = std::filesystem::canonical(absolute_path);
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
    LOG_INFO("SyncManager", "Starting initialSync() with %i clouds", _clouds.size());
    auto clouds = _clouds;
    clouds.emplace(0, _local);

    CallbackDispatcher::get().setDB(_db_file);
    CallbackDispatcher::get().setClouds(clouds);
    CallbackDispatcher::get().start();
    HttpClient::get().setClouds(clouds);
    HttpClient::get().start();

    _db->addLocalDir(_local_dir.string());

    _num_clouds = _clouds.size();

    LOG_INFO("SyncManager", "Scanning local initialFiles()");

    std::unordered_map<std::filesystem::path, std::unique_ptr<FileRecordDTO>> local_files_map;
    std::vector<std::unique_ptr<FileRecordDTO>> tmp_files;
    tmp_files = _local->initialFiles();
    LOG_DEBUG("SyncManager", "LocalStorage returned %i items", tmp_files.size());
    for (auto& file : tmp_files) {
        LOG_DEBUG("SyncManager", "  LOCAL: %s", file->rel_path.string().c_str());
        int global_id = _db->add_file(*file);
        file->global_id = global_id;
        local_files_map.emplace(file->rel_path, std::move(file));
    }

    LOG_INFO("SyncManager", "Scanning clouds initialFiles()");
    std::unordered_multimap<std::filesystem::path, std::unique_ptr<FileRecordDTO>> initial_files;
    for (auto& [cloud_id, cloud] : _clouds) {
        auto cloud_name = CloudResolver::getName(cloud_id);

        LOG_INFO("SyncManager", "  Querying initialFiles() on cloud %s (id=%d)",
            cloud_name.c_str(), cloud_id);

        tmp_files = cloud->initialFiles();

        LOG_DEBUG("SyncManager", "  Cloud %s returned %i items", cloud_name.c_str(), tmp_files.size());

        for (auto& file : tmp_files) {

            LOG_DEBUG("SyncManager", "    CLOUD: %s with file: %s",
                cloud_name,
                file->rel_path.string().c_str());

            /* if (local_files_map.contains(file->rel_path)) {
                file->global_id = local_files_map[file->rel_path]->global_id;
                _db->add_file_link(*file);

                LOG_DEBUG("SyncManager", "      linked to global_id=%i",
                    file->global_id);
            } */
            auto rp = file->rel_path;
            initial_files.emplace(
                rp,
                std::move(file)
            );
        }
    }

    LOG_INFO("SyncManager", "initialSync preparation done: local_files_map=%i, initial_files=%i",
        local_files_map.size(), initial_files.size());

    LOG_INFO("SyncManager", "Reconciling cloud vs local");
    for (auto it = initial_files.begin(); it != initial_files.end();) {
        auto range = initial_files.equal_range(it->first);
        auto const& path = it->first;

        LOG_DEBUG("SyncManager", "Processing path: %s (%i candidates)",
            path.string().c_str(),
            std::distance(range.first, range.second));

        FileRecordDTO* best = nullptr;
        for (auto itr = range.first; itr != range.second; ++itr) {
            FileRecordDTO* dto = itr->second.get();
            if (!best
                || dto->cloud_file_modified_time > best->cloud_file_modified_time) {
                best = dto;
            }
        }

        LOG_DEBUG("SyncManager", "  Best cloud_id=%i mtime=%lli",
            best->cloud_id,
            static_cast<long long>(best->cloud_file_modified_time));


        int cloud_id = best->cloud_id;
        if (!local_files_map.contains(path)) {

            LOG_INFO("SyncManager", "  -> NEW remote-only: %s", path.string().c_str());

            auto change = std::make_shared<Change>(
                ChangeType::New,
                path,
                std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()),
                cloud_id
            );
            std::unique_ptr<ICommand> first_cmd = nullptr;
            int cmds_count = 0;
            if (best->type == EntryType::Directory) {

                LOG_DEBUG("SyncManager", "    Directory -> LocalUploadCommand");

                first_cmd = std::make_unique<LocalUploadCommand>(0);
                auto dto_clone = std::make_unique<FileRecordDTO>(*best);
                first_cmd->setDTO(std::move(dto_clone));
                first_cmd->setOwner(change);
                change->setCmdChain(std::move(first_cmd));
            }

            else {

                LOG_DEBUG("SyncManager", "    File -> CloudDownloadNewCommand + LocalUploadCommand");

                first_cmd = std::make_unique<CloudDownloadNewCommand>(cloud_id);
                auto dto_clone = std::make_unique<FileRecordDTO>(*best);
                first_cmd->setDTO(std::move(dto_clone));
                first_cmd->setOwner(change);
                auto next_cmd = std::make_unique<LocalUploadCommand>(0);
                next_cmd->setOwner(change);
                first_cmd->addNext(std::move(next_cmd));
                change->setCmdChain(std::move(first_cmd));
            }
            handleChange(std::move(change));

            local_files_map.emplace(path, std::make_unique<FileRecordDTO>(*best));
        }
        else if (std::filesystem::is_regular_file(_local_dir / path) && local_files_map[path]->cloud_file_modified_time < best->cloud_file_modified_time) {
            auto change = std::make_shared<Change>(
                ChangeType::Update,
                path,
                std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()),
                cloud_id
            );

            LOG_INFO("SyncManager", "  -> UPDATE remote-newer: %s", path.string().c_str());

            auto first_cmd = std::make_unique<CloudDownloadUpdateCommand>(cloud_id);
            first_cmd->setOwner(change);

            auto new_dto = std::make_unique<FileUpdatedDTO>(
                best->type,
                local_files_map[path]->global_id,
                cloud_id,
                best->cloud_file_id,
                std::get<std::string>(best->cloud_hash_check_sum),
                best->cloud_file_modified_time,
                path,
                best->cloud_parent_id,
                best->size
            );

            first_cmd->setDTO(std::move(new_dto));
            auto next_cmd = std::make_unique<LocalUpdateCommand>(0);
            next_cmd->setOwner(change);
            first_cmd->addNext(std::move(next_cmd));
            change->setCmdChain(std::move(first_cmd));

            handleChange(std::move(change));

            best->global_id = local_files_map[path]->global_id;
            local_files_map[path] = std::make_unique<FileRecordDTO>(*best);
        }

        it = range.second;
    }

    std::unordered_map<int, bool> ids;
    for (const auto& [id, storage] : _clouds) {
        ids.insert({ id, false });
    }

    HttpClient::get().waitUntilIdle();
    CallbackDispatcher::get().waitUntilIdle();
    HttpClient::get().waitUntilIdle();

    LOG_INFO("SyncManager", "Pushing local variants to clouds");

    for (auto& [rel_path, local_dto] : local_files_map) {

        if (local_dto->global_id == 0) {
            local_dto->global_id = _db->getGlobalIdByPath(rel_path);
        }

        LOG_DEBUG("SyncManager", "Local path: %s", rel_path.string().c_str());

        auto range = initial_files.equal_range(rel_path);

        std::unordered_map<int, bool> have_it_or_not = ids;
        for (auto itr = range.first; itr != range.second; ++itr) {
            have_it_or_not[itr->second->cloud_id] = true;
        }

        if (std::filesystem::is_directory(rel_path)) {

            LOG_DEBUG("SyncManager", "  Directory -> uploading to missing clouds");

            std::shared_ptr<Change> change = nullptr;
            std::unique_ptr<ICommand> first_cmd = nullptr;
            int cmds_count = 0;
            for (const auto& [cloud_id, have] : have_it_or_not) {
                if (!have) {
                    auto dto_clone = std::make_unique<FileRecordDTO>(*local_dto);
                    first_cmd = std::make_unique<CloudUploadCommand>(cloud_id);
                    change = std::make_shared<Change>(
                        ChangeType::New,
                        rel_path,
                        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()),
                        0
                    );
                    change->setCmdChain(std::move(first_cmd));
                    break;
                }
            }
            if (change) {
                handleChange(std::move(change));
            }
        }
        else {

            LOG_DEBUG("SyncManager", "  File -> upload/update to clouds");

            auto change = std::make_shared<Change>(
                ChangeType::New,
                rel_path,
                std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()),
                0
            );
            std::vector<std::unique_ptr<ICommand>> first_cmd = {};
            int cmds_count = 0;

            for (const auto& [cloud_id, have] : have_it_or_not) {
                if (!have) {
                    LOG_DEBUG("SyncManager", "Cloud: %s dont have file: %s", CloudResolver::getName(cloud_id), rel_path.c_str());
                    auto dto_clone = std::make_unique<FileRecordDTO>(*local_dto);
                    auto cmd = std::make_unique<CloudUploadCommand>(cloud_id);
                    cmd->setDTO(std::move(dto_clone));
                    cmd->setOwner(change);
                    first_cmd.push_back(std::move(cmd));
                }

                else {
                    if (cloud_id == local_dto->cloud_id) {
                        LOG_DEBUG("SyncManager", "Cloud: %s has best file: %s, dont upload here", CloudResolver::getName(cloud_id), rel_path.c_str());
                        _db->add_file_link(*local_dto);
                        continue;
                    }
                    FileRecordDTO* cloud_dto = nullptr;
                    for (auto itr = range.first; itr != range.second; ++itr) {
                        if (itr->second->cloud_id == cloud_id) {
                            cloud_dto = itr->second.get();
                        }
                    }

                    LOG_DEBUG("SyncManager", "Cloud: %s has old file: %s, upload here", CloudResolver::getName(cloud_id), rel_path.c_str());

                    auto dto_clone = std::make_unique<FileUpdatedDTO>(
                        local_dto->type,
                        local_dto->global_id,
                        cloud_dto->cloud_id,
                        cloud_dto->cloud_file_id,
                        std::get<std::string>(cloud_dto->cloud_hash_check_sum),
                        local_dto->cloud_file_modified_time,
                        rel_path,
                        cloud_dto->cloud_parent_id,
                        local_dto->size
                    );

                    cloud_dto->global_id = local_dto->global_id;
                    _db->add_file_link(*cloud_dto);

                    auto cmd = std::make_unique<CloudUpdateCommand>(cloud_id);
                    cmd->setDTO(std::move(dto_clone));
                    cmd->setOwner(change);
                    first_cmd.push_back(std::move(cmd));
                    cmds_count++;
                }
            }
            if (!first_cmd.empty()) {
                change->setCmdChain(std::move(first_cmd));
                handleChange(std::move(change));
            }
        }

    }

    LOG_INFO("SyncManager", "Waiting for all HTTP and callbacks to finish");

    HttpClient::get().waitUntilIdle();

    CallbackDispatcher::get().waitUntilIdle();

    HttpClient::get().waitUntilIdle();

    for (const auto& [cloud_id, cloud] : _clouds) {
        nlohmann::json cloud_data = _db->get_cloud_config(cloud_id);
        cloud_data["start_page_token"] = cloud->getDeltaToken();
        _db->update_cloud_data(cloud_id, cloud_data);
    }

    _db->markInitialSyncDone();
    LOG_INFO("SyncManager", "=== initialSync() complete ===");
}

void SyncManager::handleChange(std::shared_ptr<Change> incoming) {
    auto path = incoming->getTargetPath();
    auto type = incoming->getType();
    auto entry_type = incoming->getTargetType();

    LOG_INFO("SyncManager", "handleChange() called for path=%s",
        path.string().c_str());

    if (entry_type == EntryType::Directory && type == ChangeType::New) {
        auto missing = _db->getMissingPathPart(path, _num_clouds);
        if (!missing.empty()) {
            LOG_DEBUG("SyncManager", "Missing parts found for directory: %s", missing.c_str());
            createPath(path, missing);
        }
        return;
    }

    if (!_current_changes.contains(path)) {
        if (type == ChangeType::New) {
            auto missing = _db->getMissingPathPart(path.parent_path(), _num_clouds);
            if (!missing.empty()) {
                LOG_DEBUG("SyncManager", "Missing parent path: %s", missing.c_str());
                createPath(path.parent_path(), missing);
            }
        }
        LOG_DEBUG("SyncManager", "Starting new change for path: %s", path.c_str());
        startChange(path, std::move(incoming));
    }
    else {
        LOG_DEBUG("SyncManager", "Change already exists for path: %s", path.c_str());

        if (_current_changes[path]->getCloudId() == incoming->getCloudId() &&
            _current_changes[path]->getType() == ChangeType::Update && incoming->getType() == ChangeType::Move)
        {
            LOG_DEBUG("SyncManager", "Merging Move into existing Update change for path: %s", path.c_str());
            _current_changes[path]->addDependent(std::move(incoming));
        }
        else if (_current_changes[path]->getType() == ChangeType::New && incoming->getType() == ChangeType::New) {
            return;
        }
    }
}

void SyncManager::startChange(const std::filesystem::path& path, std::shared_ptr<Change> change) {
    change->setOnComplete([this, path](auto&& deps) {
        this->onChangeCompleted(path, std::move(deps));
        });
    _current_changes[path] = std::move(change);
    _current_changes[path]->dispatch();
}

void SyncManager::onChangeCompleted(const std::filesystem::path& path,
    std::vector<std::shared_ptr<Change>>&& dependents)
{
    LOG_INFO("SyncManager", "Change completed for path: %s", path.c_str());

    std::shared_ptr<Change> life_holder;
    if (auto it = _current_changes.find(path); it != _current_changes.end()) {
        life_holder = std::move(it->second);
        _current_changes.erase(it);
    }

    for (auto& dep : dependents) {
        handleChange(std::move(dep));
    }
}

void SyncManager::createPath(const std::filesystem::path& path, const std::filesystem::path& missing) {
    LOG_INFO("SyncManager", "createPath() start for path=%s, missing=%s", path.string().c_str(), missing.string().c_str());

    std::vector<std::unique_ptr<FileRecordDTO>> db_entry;
    std::vector<std::unique_ptr<FileRecordDTO>> tmp;

    LOG_DEBUG("SyncManager", "Creating local path...");

    tmp = _local->createPath(path, missing);
    db_entry.reserve(db_entry.size() + tmp.size());
    db_entry.insert(
        db_entry.end(),
        std::make_move_iterator(tmp.begin()),
        std::make_move_iterator(tmp.end())
    );

    CallbackDispatcher::get().syncDbWrite(db_entry);

    LOG_DEBUG("SyncManager", "Wrote %i local entries to DB", db_entry.size());

    std::unordered_map<std::filesystem::path, int> path_to_global_id;

    for (const auto& e : db_entry) {
        path_to_global_id.insert({ e->rel_path, e->global_id });
        LOG_DEBUG("SyncManager", "Wrote %s with id %i", e->rel_path.c_str(), e->global_id);
    }

    db_entry.clear();

    for (auto& [cloud_id, cloud] : _clouds) {
        if (cloud_id != 0) {
            LOG_INFO("SyncManager", "Creating path in %s", CloudResolver::getName(cloud_id));

            tmp = cloud->createPath(path, missing);

            for (auto& e : tmp) {
                if (path_to_global_id.contains(e->rel_path)) {
                    e->global_id = path_to_global_id[e->rel_path];
                }
                else {
                    e->global_id = _db->getGlobalIdByPath(e->rel_path);
                }
                LOG_DEBUG("SyncManager", "Changed %s global id to %i", e->rel_path.c_str(), e->global_id);
            }

            db_entry.reserve(db_entry.size() + tmp.size());
            db_entry.insert(
                db_entry.end(),
                std::make_move_iterator(tmp.begin()),
                std::make_move_iterator(tmp.end())
            );
        }
    }

    CallbackDispatcher::get().syncDbWrite(db_entry);
    LOG_INFO("SyncManager", "createPath() complete. Total entries written: %i", db_entry.size());
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

    std::string refresh_token = _clouds[cloud_id]->getRefreshToken(code, _http_server->getPort());

    nlohmann::json saved = _db->get_cloud_config(cloud_id);
    saved["refresh_token"] = refresh_token;
    _db->update_cloud_data(cloud_id, saved);

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

    _local->startWatching();

    _polling_worker = std::make_unique<std::thread>(&SyncManager::pollingLoop, this);
    _changes_worker = std::make_unique<std::thread>(&SyncManager::proccessLoop, this);
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
    std::shared_ptr<Change> change;
    while (_changes_buff.pop(change)) {
        handleChange(std::move(change));
    }
}

void SyncManager::pollingLoop() {
    auto now = std::chrono::steady_clock::now();
    auto next_poll = now;
    auto next_flush = now + std::chrono::seconds(8);

    while (!_should_exit) {
        now = std::chrono::steady_clock::now();

        {
            std::unique_lock lk(_signal_mtx);
            _signal_cv.wait_until(
                lk, next_poll,
                [this] { return _should_exit
                || _signal_dirty.exchange(false); });
        }

        now = std::chrono::steady_clock::now();

        if (now >= next_poll) {
            for (auto& [id, cloud] : _clouds) {
                if (id != 0) {
                    cloud->getChanges();
                }
            }
            next_poll = now + std::chrono::seconds(10);
        }

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