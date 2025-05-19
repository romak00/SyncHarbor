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

void LocalStorage::proccesUpdate(std::unique_ptr<FileUpdatedDTO>& dto, const std::string& response) const {
    if (dto->cloud_id != 0) {
        LOG_DEBUG(
            "LOCAL STORAGE",
            dto->rel_path.string(),
            "trying to delete: %s",
            (_local_home_dir.string() + "/" + dto->rel_path.string())
        );
        _expected_events.add(_local_home_dir / dto->rel_path, ChangeType::Delete);
        std::filesystem::remove(_local_home_dir / dto->rel_path);
        LOG_DEBUG(
            "LOCAL STORAGE",
            dto->rel_path.string(),
            "trying to rename from %s to %s",
            (_local_home_dir.string() + "/" + dto->rel_path.parent_path().string() + "/.-tmp-cloudsync-" + dto->rel_path.filename().string()),
            _local_home_dir.string() + "/" + dto->rel_path.string()
        );
        std::filesystem::rename(
            _local_home_dir.string() + "/" + dto->rel_path.parent_path().string() + "/.-tmp-cloudsync-" + dto->rel_path.filename().string(),
            _local_home_dir / dto->rel_path
        );

        _db->update_file_link(*dto);

        std::filesystem::path full = _local_home_dir / dto->rel_path;
        dto->file_id = this->getFileId(full);
        dto->size = std::filesystem::file_size(full);
        dto->cloud_hash_check_sum = this->computeFileHash(full);
        dto->cloud_file_modified_time = convertSystemTime(full);
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

std::vector<std::unique_ptr<FileRecordDTO>> LocalStorage::createPath(const std::filesystem::path& path, const std::filesystem::path& missing) {
    std::vector<std::unique_ptr<FileRecordDTO>> created;

    auto norm_path = normalizePath(path);

    std::vector<std::filesystem::path> segs;
    for (const auto& s : norm_path) {
        segs.push_back(s);
    }
    int keep = int(segs.size() - std::distance(missing.begin(), missing.end()));
    std::filesystem::path prefix;
    for (int i = 0; i < keep; ++i) {
        prefix /= segs[i];
    }

    std::filesystem::path accum = _local_home_dir / prefix;

    for (auto const& seg : missing) {
        accum /= seg;

        if (std::filesystem::create_directory(accum)) {
            auto dto = std::make_unique<FileRecordDTO>(
                EntryType::Directory,
                std::filesystem::relative(accum, _local_home_dir),
                0ULL,
                convertSystemTime(accum),
                0,
                getFileId(accum)
            );

            _expected_events.add(std::filesystem::relative(accum, _local_home_dir), ChangeType::New);

            created.push_back(std::move(dto));
        }

        else {
            continue;
        }
    }

    return created;
}

void LocalStorage::onFsEvent(const wtr::event& e) {
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

    _raw_signal->cv.notify_one();
}


std::vector<std::unique_ptr<Change>> LocalStorage::proccessChanges() {
    std::vector<std::unique_ptr<Change>> out;

    FileEvent evt;
    while (_events_buff.try_pop(evt)) {
        switch (evt.type) {
        case ChangeType::New:    handleCreated(evt); break;
        case ChangeType::Delete: handleDeleted(evt); break;
        case ChangeType::Rename: handleRenamed(evt); break;
        case ChangeType::Update: handleUpdated(evt); break;
        default: break;
        }
    }

    std::unique_ptr<Change> ch;
    while (_changes_queue.try_pop(ch)) {
        out.push_back(std::move(ch));
    }

    return out;
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

void LocalStorage::setRawSignal(std::shared_ptr<RawSignal> raw_signal) {
    _raw_signal = std::move(raw_signal);
}

std::vector<std::unique_ptr<FileRecordDTO>> LocalStorage::initialFiles() {
    std::vector<std::unique_ptr<FileRecordDTO>> result;
    for (auto& entry : std::filesystem::recursive_directory_iterator(_local_home_dir)) {
        const auto& p = entry.path();
        auto rel_path = std::filesystem::relative(p, _local_home_dir);
        bool is_dir = std::filesystem::is_directory(p);
        bool is_doc = this->isDoc(p);

        std::time_t mtime = convertSystemTime(p);
        uint64_t    size = is_dir ? 0ULL
            : std::filesystem::file_size(p);
        uint64_t    hash = is_dir ? 0ULL
            : computeFileHash(p);

        uint64_t file_id = getFileId(p);

        auto dto = std::make_unique<FileRecordDTO>(
            is_dir ? EntryType::Directory : (is_doc ? EntryType::Document : EntryType::File),
            rel_path,
            size,
            mtime,
            hash,
            file_id
        );

        result.push_back(std::move(dto));
    }
    return result;
}

bool LocalStorage::isDoc(const std::filesystem::path& path) const {
    static const std::unordered_set<std::string> exts = {
        // Text Documents
        ".doc", ".docx", ".odt", ".rtf", ".txt",

        // Spreadsheets
        ".xls", ".xlsx", ".ods", ".csv", ".tsv",

        // Presentations
        ".ppt", ".pptx", ".odp",

        // Dropbox and other
        ".gdoc", ".gsheet", ".gslide", ".gdraw", ".gform", ".gsite", ".jam", ".paper", ".url"
    };

    std::string ext = path.extension().string();

    return exts.contains(ext);
}

void LocalStorage::handleDeleted(const FileEvent& evt) {
    if (_expected_events.check(evt.path, ChangeType::Delete)) {
        LOG_DEBUG("LocalStorage", "Expected DELETE: %s", evt.path.string());
        return;
    }
    if (ignoreTmp(evt.path)) {
        LOG_DEBUG("LocalStorage", "Ignore tmp DELETE: %s", evt.path.string());
        return;
    }

    auto full = _local_home_dir / evt.path;
    auto rel = std::filesystem::relative(full, _local_home_dir);

    int global_id = 0;
    if (auto rec = _db->getFileByFileId(evt.file_id)) {
        global_id = rec->global_id;
    }

    auto dto = std::make_unique<FileDeletedDTO>(
        rel,
        global_id,
        evt.when
    );
    auto ch = ChangeFactory::makeDelete(std::move(dto));

    _changes_queue.push(std::move(ch));
}

void LocalStorage::handleRenamed(const FileEvent& evt) {
    if (!evt.associated) {
        auto full = _local_home_dir / evt.path;
        if (!std::filesystem::exists(full)) {
            handleDeleted(evt);
        }
        else {
            handleUpdated(evt);
        }
    }
    else {
        handleMoved(evt);
    }
}

void LocalStorage::handleMoved(const FileEvent& evt) {
    if (ignoreTmp(evt.path)) {
        LOG_DEBUG("LocalStorage", "Ignore tmp MOVE: %s", evt.path.string());
        return;
    }
    if (_expected_events.check(evt.path, ChangeType::Move)) {
        LOG_DEBUG("LocalStorage", "Expected MOVE: %s", evt.path.string());
        return;
    }

    auto full_new = _local_home_dir / evt.path;
    auto full_old = _local_home_dir / evt.associated->path;
    auto new_rel = std::filesystem::relative(full_new, _local_home_dir);
    auto old_rel = std::filesystem::relative(full_old, _local_home_dir);

    auto rec = _db->getFileByFileId(evt.file_id);
    if (!rec) {
        LOG_WARNING("LocalStorage", "Move of unknown file_id: %s", evt.path.string());
        handleCreated(*evt.associated.get());
        return;
    }

    auto dto = std::make_unique<FileMovedDTO>(
        rec->type,
        rec->global_id,
        evt.when,
        old_rel,
        new_rel
    );
    auto ch = ChangeFactory::makeLocalMove(std::move(dto));
    _changes_queue.push(std::move(ch));
}

void LocalStorage::proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const {
    if (dto->cloud_id != 0) {
        LOG_DEBUG(
            "LOCAL STORAGE",
            dto->rel_path.string(),
            "trying to rename from %s to %s",
            (_local_home_dir.string() + "/" + dto->rel_path.parent_path().string() + "/.-tmp-cloudsync-" + dto->rel_path.filename().string()),
            _local_home_dir.string() + "/" + dto->rel_path.string()
        );
        std::filesystem::rename(
            _local_home_dir.string() + "/" + dto->rel_path.parent_path().string() + "/.-tmp-cloudsync-" + dto->rel_path.filename().string(),
            _local_home_dir / dto->rel_path
        );

        auto cloud_dto = std::make_unique<FileRecordDTO>(*dto);

        std::filesystem::path full = _local_home_dir / dto->rel_path;
        dto->file_id = this->getFileId(full);
        dto->size = std::filesystem::file_size(full);
        dto->cloud_hash_check_sum = this->computeFileHash(full);
        dto->cloud_file_modified_time = convertSystemTime(full);
        dto->cloud_id = _id;

        int global_id = _db->add_file(*dto);
        cloud_dto->global_id = global_id;
        _db->add_file_link(*cloud_dto);
    }
    else {
        dto->file_id = this->getFileId(_local_home_dir / dto->rel_path);
        dto->cloud_hash_check_sum = this->computeFileHash(_local_home_dir / dto->rel_path);
        dto->cloud_file_modified_time = convertSystemTime(_local_home_dir / dto->rel_path);

        int global_id = _db->add_file(*dto);
        dto->global_id = global_id;
    }
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

    auto full = _local_home_dir / evt.path;
    auto rel = std::filesystem::relative(full, _local_home_dir);
    EntryType t = std::filesystem::is_directory(full) ? EntryType::Directory : (this->isDoc(full) ? EntryType::Document : EntryType::File);
    uint64_t hash = t != EntryType::Directory ? computeFileHash(full) : 0;
    uint64_t sz = t != EntryType::Directory ? std::filesystem::file_size(full) : 0;

    auto dto = std::make_unique<FileRecordDTO>(
        t,
        rel,
        sz,
        evt.when,
        hash,
        evt.file_id
    );
    auto ch = ChangeFactory::makeLocalNew(std::move(dto));
    _changes_queue.push(std::move(ch));
}

void LocalStorage::handleUpdated(const FileEvent& evt) {
    if (ignoreTmp(evt.path)) {
        LOG_DEBUG("LocalStorage", "Ignore tmp UPDATE: %s", evt.path.string());
        return;
    }

    auto full = _local_home_dir / evt.path;
    auto rel = std::filesystem::relative(full, _local_home_dir);

    auto rec = _db->getFileByFileId(evt.file_id);
    if (!rec) {
        LOG_WARNING("LocalStorage", "Update of unknown file_id: %s", evt.path.string());
        return;
    }

    uint64_t hash = computeFileHash(full);
    uint64_t sz = std::filesystem::file_size(full);
    std::time_t tm = fromWatcherTime(evt.when);

    auto dto = std::make_unique<FileUpdatedDTO>(
        rec->type,
        rec->global_id,
        hash,
        tm,
        rel,
        sz,
        evt.file_id    
    );
    auto ch = ChangeFactory::makeLocalUpdate(std::move(dto));
    _changes_queue.push(std::move(ch));
}

bool LocalStorage::hasChanges() const {
    return !_events_buff.empty();
}