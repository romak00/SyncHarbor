#include "LocalStorage.h"

uint64_t LocalStorage::getFileId(const std::filesystem::path& path) const {
#ifdef _WIN32
    HANDLE h = CreateFileW(
        path.wstring().c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );
    if (h == INVALID_HANDLE_VALUE) return 0;
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(h, &info)) {
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    return (uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
#else
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return (uint64_t(st.st_dev) << 32) | uint64_t(st.st_ino);
    }
    return 0;
#endif
}

LocalStorage::LocalStorage(const std::filesystem::path& home_dir, const int cloud_id, const std::shared_ptr<Database>& db_conn) {
    _local_home_dir = home_dir;
    _id = cloud_id;
    _db = db_conn;
}

LocalStorage::~LocalStorage() noexcept {
    stopWatching();
}

void LocalStorage::proccesUpdate(std::unique_ptr<FileModifiedDTO>& dto, const std::string& response) const {
    if (dto->cloud_id != 0) {

        if (dto->change_flags.contains(ChangeType::Update)) {
            LOG_DEBUG(
                "LOCAL STORAGE",
                dto->old_rel_path.string(),
                "trying to delete: %s",
                (_local_home_dir.string() + "/" + dto->old_rel_path.string())
            );
            _expected_events.add(_local_home_dir / dto->old_rel_path, ChangeType::Delete);
            std::filesystem::remove(_local_home_dir / dto->old_rel_path);
            LOG_DEBUG(
                "LOCAL STORAGE",
                dto->new_rel_path.string(),
                "trying to rename from %s to %s",
                (_local_home_dir.string() + "/" + dto->new_rel_path.parent_path().string() + "/.-tmp-cloudsync-" + dto->new_rel_path.filename().string()),
                _local_home_dir.string() + "/" + dto->new_rel_path.string()
            );
            std::filesystem::rename(
                _local_home_dir.string() + "/" + dto->new_rel_path.parent_path().string() + "/.-tmp-cloudsync-" + dto->new_rel_path.filename().string(),
                _local_home_dir / dto->new_rel_path
            );

            std::filesystem::path full = _local_home_dir / dto->new_rel_path;
            if (!std::filesystem::is_directory(full)) {
                dto->file_id = this->getFileId(full);
                dto->size = std::filesystem::file_size(full);
                dto->cloud_hash_check_sum = this->computeFileHash(full);
            }
            dto->cloud_file_modified_time = convertSystemTime(full);
        }
        else if (dto->change_flags.contains(ChangeType::Move)) {
            LOG_DEBUG(
                "LOCAL STORAGE",
                dto->old_rel_path.string(),
                "trying to rename from %s to %s",
                _local_home_dir.string() + "/" + dto->old_rel_path.string(),
                _local_home_dir.string() + "/" + dto->new_rel_path.string()
            );
            _expected_events.add(_local_home_dir.string() / dto->old_rel_path, ChangeType::Move);
            std::filesystem::rename(
                _local_home_dir.string() + "/" + dto->old_rel_path.string(),
                _local_home_dir.string() + "/" + dto->new_rel_path.string()
            );
            std::filesystem::path full = _local_home_dir.string() / dto->new_rel_path;
            dto->cloud_file_modified_time = convertSystemTime(full);
        }
        dto->cloud_id = _id;
    }
}

bool LocalStorage::ignoreTmp(const std::filesystem::path& path) {
    constexpr std::string_view tmp_prefix{ ".-tmp-cloudsync-" };
    auto fn = path.filename().string();
    if (fn.size() >= tmp_prefix.size()
        && std::string_view(fn).starts_with(tmp_prefix)) {
        return true;
    }
    return false;
}

void LocalStorage::startWatching() {
    if (_watching) return;
    LOG_INFO("LocalStorage", "Starting watcher on %s", _local_home_dir.string());
    _watching = true;

    _watcher = std::make_unique<wtr::watch>(_local_home_dir.string(),
        [this](wtr::event ev) { this->onFsEvent(ev); }
    );
}

void LocalStorage::stopWatching() {
    if (!_watching) return;

    _watching = false;
    _watcher->close();
    _events_buff.close();
}

std::string LocalStorage::getHomeDir() const {
    return _local_home_dir;
}

void LocalStorage::onFsEvent(const wtr::event& e) {
    std::cout << wtr::to<std::string>(e.effect_type) + ' '
        + wtr::to<std::string>(e.path_type) + ' '
        + wtr::to<std::string>(e.path_name)
        + (e.associated ? " -> " + wtr::to<std::string>(e.associated->path_name) : "")
        << std::endl;

    if (e.path_type == wtr::event::path_type::watcher) {
        LOG_INFO("LocalStorage", "Watcher Event: %s", e.path_name.string());
        return;
    }

    std::time_t time = fromWatcherTime(e.effect_time);
    switch (e.effect_type) {
    case wtr::event::effect_type::rename:
        _events_buff.push(
            FileEvent(
                e.path_name,
                time,
                ChangeType::Rename,
                (e.associated ? std::make_shared<FileEvent>(
                    e.associated->path_name,
                    time, ChangeType::Rename)
                    : nullptr)
            )
        );
        break;

    case wtr::event::effect_type::create:
        _events_buff.push(
            FileEvent(
                e.path_name,
                time,
                ChangeType::New)
        );
        break;

    case wtr::event::effect_type::destroy:
        _events_buff.push(
            FileEvent(
                e.path_name,
                time,
                ChangeType::Delete)
        );
        break;

    case wtr::event::effect_type::modify:
        _events_buff.push(
            FileEvent(
                e.path_name,
                time,
                ChangeType::Update)
        );
        break;

    default:
        break;
    }
}


std::vector<std::unique_ptr<Change>> LocalStorage::proccessChanges() {
    std::vector<std::unique_ptr<Change>> changes;
    FileEvent evt;
    while (_events_buff.try_pop(evt)) {
        LOG_DEBUG("LocalStorage", "events_buff size: %i", _events_buff.size());
        switch (evt.type) {
        case ChangeType::Rename:
            handleRenamed(evt);
            break;

        case ChangeType::New:
            handleCreated(evt);
            break;

        case ChangeType::Delete:
            handleDeleted(evt);
            break;

        case ChangeType::Update:
            handleUpdated(evt);
            break;

        default:
            break;
        }
    }
    std::unique_ptr<Change> change;
    while (_changes_queue.try_pop(change)) {
        changes.emplace_back(std::move(change));
    }
    return changes;
}

uint64_t LocalStorage::computeFileHash(const std::filesystem::path& path, uint64_t seed) const {
    constexpr size_t BUF_SIZE = 4 * 1024 * 1024;
    std::vector<char> buf(BUF_SIZE);

    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, seed);

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open " + path.string() + " from computeFileHash");
    }
    while (in) {
        in.read(buf.data(), BUF_SIZE);
        auto n = in.gcount();
        if (n > 0) {
            XXH64_update(state, buf.data(), n);
        }
    }
    uint64_t hash = XXH64_digest(state);
    XXH64_freeState(state);
    return hash;
}

std::time_t LocalStorage::fromWatcherTime(const long long effect_ns) {
    using namespace std::chrono;

    nanoseconds ns_effect{ effect_ns };
    system_clock::duration  d_effect = duration_cast<system_clock::duration>(ns_effect);
    system_clock::time_point tp_effect{ d_effect };

    return system_clock::to_time_t(tp_effect);
}

bool LocalStorage::isRealTime() const {
    return true;
}

void LocalStorage::setRawSignal(std::shared_ptr<RawSignal> raw_signal) {
    _raw_signal = std::move(raw_signal);
}

std::vector<std::unique_ptr<Change>> LocalStorage::flushOldDeletes() {
    LOG_DEBUG("LocalStorage", "Flushing Deletes");
    std::vector<std::unique_ptr<Change>> changes;
    std::lock_guard<std::mutex> lk(_cleanup_mtx);
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto it = _pending_deletes.begin();
    while (it != _pending_deletes.end()) {
        const FileEvent& evt = it->second;
        if (_expected_events.check(evt.path, ChangeType::Delete)) {
            LOG_DEBUG("LocalStorage", "Expected DELETE: %s", evt.path.string());
            it = _pending_deletes.erase(it);
            continue;
        }
        std::time_t evt_time = fromWatcherTime(evt.when);
        if ((now - evt.when) > _UNDO_INTERVAL) {
            LOG_DEBUG("LocalStorage", "Event: TRUE DELETE: %s", evt.path.string());
            auto rel_path = std::filesystem::relative(evt.path, _local_home_dir);
            int global_id = _db->getGlobalIdByFileId(it->first);
            changes.emplace_back(ChangeFactory::create<LocalDeleteCommand, FileDeletedDTO>(
                ChangeType::New,
                evt_time,
                this->_id,
                rel_path, global_id, evt_time
            ));

            it = _pending_deletes.erase(it);
        }
        else {
            ++it;
        }
    }
    _cleanup_cv.notify_all();
    return changes;
}

void LocalStorage::handleDeleted(const FileEvent& evt) {
    {
        std::lock_guard<std::mutex> lk(_cleanup_mtx);
        _pending_deletes.emplace(evt.file_id, evt);
    }
    _cleanup_cv.notify_all();
}

void LocalStorage::handleRenamed(const FileEvent& evt) {
    if (evt.associated == nullptr) {
        if (!std::filesystem::exists(evt.path)) {
            handleDeleted(evt);
        }
        else {
            handleUpdated(evt);
        }
    }
    else {
        if (evt.path != evt.associated->path) {
            handleMoved(evt);
        }
    }
}

void LocalStorage::handleMoved(const FileEvent& evt) {
    if (_expected_events.check(evt.path, ChangeType::Move)) {
        LOG_DEBUG("LocalStorage", "Expected move: %s", evt.path.string());
        return;
    }
    std::unique_ptr<FileRecordDTO> dto = _db->getFileByFileId(evt.file_id);
    auto new_rel_path = std::filesystem::relative(_local_home_dir, evt.path);
    auto old_rel_path = std::filesystem::relative(_local_home_dir, evt.associated->path);
    LOG_DEBUG("LocalStorage", "Trying to make move from %s to %s", evt.path.string(), evt.associated->path.string());
    _changes_queue.push(ChangeFactory::create<LocalUpdateCommand, FileModifiedDTO>(
        ChangeType::Move,
        evt.when,
        this->_id,
        dto->type, ChangeType::Move, dto->global_id, std::get<uint64_t>(dto->cloud_hash_check_sum), evt.when, old_rel_path, new_rel_path, dto->size, dto->file_id
    ));
}



void LocalStorage::handleCreated(const FileEvent& evt) {
    if (ignoreTmp(evt.path)) {
        LOG_DEBUG("LocalStorage", "Ignore tmp NEW: %s", evt.path.string());
        return;
    }
    if (_expected_events.check(evt.path, ChangeType::New)) {
        LOG_DEBUG("LocalStorage", "Expected NEW: %s", evt.path.string());
        return;
    }
    if (LocalStorage::checkUndo(evt)) {
        LOG_DEBUG("LocalStorage", "Undo on DELETE: %s", evt.path.string());
        return;
    }
    auto rel_path = std::filesystem::relative(_local_home_dir, evt.path);
    EntryType type;
    uint64_t cloud_hash_check_sum = 0;
    uint64_t size = 0;
    if (std::filesystem::is_directory(evt.path)) {
        type = EntryType::Directory;
    }
    else {
        cloud_hash_check_sum = LocalStorage::computeFileHash(evt.path);
        size = std::filesystem::file_size(evt.path);
        type = EntryType::File;
    }
    LOG_DEBUG("LocalStorage", "Trying to do NEW: %s", evt.path.string());
    _changes_queue.push(ChangeFactory::create<LocalUploadCommand, FileRecordDTO>(
        ChangeType::New,
        evt.when,
        this->_id,
        type, rel_path, size, evt.when, cloud_hash_check_sum, evt.file_id
    ));
}

bool LocalStorage::checkUndo(const FileEvent& evt) {
    std::lock_guard<std::mutex> lk(_cleanup_mtx);
    if (_pending_deletes.contains(evt.file_id) && _pending_deletes[evt.file_id].path == evt.path) {
        LOG_DEBUG("LocalStorage", "Undo check done: %s", evt.path.string());
        _pending_deletes.erase(evt.file_id);
        return true;
    }
    return false;
}

void LocalStorage::handleUpdated(const FileEvent& evt) {
    if (ignoreTmp(evt.path)) {
        LOG_DEBUG("LocalStorage", "Ignore tmp UPDATE: %s", evt.path.string());
        return;
    }
    std::unique_ptr<FileRecordDTO> dto = _db->getFileByFileId(evt.file_id);
    uint64_t local_hash = computeFileHash(evt.path);
    uint64_t size = std::filesystem::file_size(evt.path);
    LOG_DEBUG("LocalStorage", "Trying to do UPDATE: %s", evt.path.string());
    std::time_t evt_time = fromWatcherTime(evt.when);
    _changes_queue.push(ChangeFactory::create<LocalUpdateCommand, FileModifiedDTO>(
        ChangeType::Update,
        evt_time,
        this->_id,
        dto->type, ChangeType::Update, dto->global_id, local_hash, evt_time, dto->rel_path, dto->rel_path, size, dto->file_id
    ));
}

bool LocalStorage::hasChanges() const {
    return !_events_buff.empty();
}