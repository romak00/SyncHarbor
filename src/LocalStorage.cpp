#include "LocalStorage.h"
#include "logger.h"

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
        LOG_DEBUG("LocalStorage", "File id for file: %s  ->  %i", path.string(), (uint64_t(st.st_dev) << 32) | uint64_t(st.st_ino));
        return (uint64_t(st.st_dev) << 32) | uint64_t(st.st_ino);
    }
    LOG_DEBUG("LocalStorage", "Error gettig id for file: %s", path.string());
    return 0;
#endif
}

LocalStorage::LocalStorage(const std::filesystem::path& home_dir, const int cloud_id, const std::shared_ptr<Database>& db_conn) {
    _local_home_dir = std::filesystem::canonical(home_dir);
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
            (_local_home_dir / dto->rel_path.parent_path() / (".-tmp-SyncHarbor-" + dto->rel_path.filename().string())).string(),
            (_local_home_dir  / dto->rel_path).string()
        );
        std::filesystem::rename(
            _local_home_dir / dto->rel_path.parent_path() / (".-tmp-SyncHarbor-" + dto->rel_path.filename().string()),
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
    _db->update_file(*dto);
}

void LocalStorage::proccesMove(std::unique_ptr<FileMovedDTO>& dto, const std::string& response) const {
    if (dto->cloud_id == 0) {
        _db->update_file(*dto);
        return;
    }
    LOG_DEBUG(
        "LOCAL STORAGE",
        dto->old_rel_path.string(),
        "trying to rename from %s to %s",
        _local_home_dir.string() + "/" + dto->old_rel_path.string(),
        _local_home_dir.string() + "/" + dto->new_rel_path.string()
    );
    _expected_events.add(_local_home_dir / dto->old_rel_path, ChangeType::Move);
    std::filesystem::rename(
        _local_home_dir / dto->old_rel_path,
        _local_home_dir / dto->new_rel_path
    );

    _db->update_file_link(*dto);

    std::filesystem::path full = _local_home_dir / dto->new_rel_path;
    dto->cloud_file_modified_time = convertSystemTime(full);
    dto->cloud_id = _id;

    _db->update_file(*dto);

    if (std::filesystem::is_directory(full)) {
        for (auto& entry : std::filesystem::recursive_directory_iterator(full)) {
            full = entry.path();
            auto new_rel_path = full.lexically_relative(_local_home_dir);
            auto file_id = this->getFileId(full);
            auto rec = _db->getFileByFileId(file_id);
            if (rec) {
                rec->rel_path = full.lexically_relative(_local_home_dir);
                auto dto = std::make_unique<FileMovedDTO>(
                    rec->type,
                    rec->global_id,
                    convertSystemTime(full),
                    rec->rel_path,
                    new_rel_path
                );
                _db->update_file(*dto);
            }
        }
    }
}

void LocalStorage::proccesDelete(std::unique_ptr<FileDeletedDTO>& dto, const std::string& response) const {
    if (dto->cloud_id != 0) {
        _expected_events.add(_local_home_dir / dto->rel_path, ChangeType::Delete);
    }
}

bool LocalStorage::ignoreTmp(const std::filesystem::path& path) {
    std::string fn = path.filename().string();

    static constexpr std::array<std::string_view, 5> prefixes = {
        ".-tmp-SyncHarbor-",
        ".goutputstream-",
        ".kate-swp",
        ".#",
        ".~lock."
    };

    static constexpr std::array<std::string_view, 8> suffixes = {
        ".swp", ".swo", ".swx",
        ".tmp", ".temp",
        ".bak", ".orig",
        "~"
    };

    for (auto p : prefixes) {
        if (fn.size() >= p.size() && fn.rfind(p, 0) == 0) {
            return true;
        }
    }

    for (auto s : suffixes) {
        if (fn.size() >= s.size() &&
            fn.compare(fn.size() - s.size(), s.size(), s) == 0) {
            return true;
        }
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

    LOG_INFO("LocalStorage", "Stopping watcher...");
    _watching = false;
    _watcher->close();
    _events_buff.close();
}

std::string LocalStorage::getHomeDir() const {
    return _local_home_dir.string();
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
                accum.lexically_relative(_local_home_dir),
                0ULL,
                convertSystemTime(accum),
                0,
                getFileId(accum)
            );

            _expected_events.add(accum, ChangeType::New);

            created.push_back(std::move(dto));
        }

        else {
            continue;
        }
    }

    return created;
}

void LocalStorage::onFsEvent(const wtr::event& e) {
    if ((e.effect_type == wtr::event::effect_type::create
        || e.effect_type == wtr::event::effect_type::modify)
        && !std::filesystem::exists(e.path_name))
    {
        return;
    }
    if (e.path_type == wtr::event::path_type::watcher) {
        LOG_INFO("LocalStorage", "Watcher Event: %s", e.path_name.string());
        return;
    }

    std::filesystem::path p = e.path_name;
    std::error_code ec;
    auto real_path = std::filesystem::weakly_canonical(p, ec);
    if (ec) real_path = p;

    auto rel = real_path.lexically_relative(_local_home_dir);
    if (rel.empty() || rel == ".") return;

    std::time_t time = fromWatcherTime(e.effect_time);
    switch (e.effect_type) {
    case wtr::event::effect_type::rename:
        LOG_INFO("LocalStorage", "FS EVENT RENAME %s  ->  %s", e.path_name.string(), e.associated ? e.associated->path_name.string() : "NULL");
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
        LOG_INFO("LocalStorage", "FS EVENT CREATE %s", e.path_name.string());
        _events_buff.push(
            FileEvent(
                e.path_name,
                time,
                ChangeType::New)
        );
        break;

    case wtr::event::effect_type::destroy:
        LOG_INFO("LocalStorage", "FS EVENT DESTROY %s", e.path_name.string());
        _events_buff.push(
            FileEvent(
                e.path_name,
                time,
                ChangeType::Delete)
        );
        break;

    case wtr::event::effect_type::modify:
        if (std::filesystem::is_directory(e.path_name)) {
            LOG_DEBUG("LocalStorage", "Ignore directory MODIFY: %s", e.path_name.string());
            break;
        }
        LOG_INFO("LocalStorage", "FS EVENT MODIFY %s", e.path_name.string());
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

    _onChange();
}

void LocalStorage::setOnChange(std::function<void()> cb) {
    _onChange = std::move(cb);
}

std::vector<std::shared_ptr<Change>> LocalStorage::proccessChanges() {
    std::vector<std::shared_ptr<Change>> out;

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

    std::shared_ptr<Change> ch;
    while (_changes_queue.try_pop(ch)) {
        LOG_DEBUG("LocalStorage", "Popping events buff: %s on file: %s", to_string(ch->getType()), ch->getTargetPath().string());
        out.push_back(std::move(ch));
    }

    return out;
}

uint64_t LocalStorage::computeFileHash(const std::filesystem::path& path, uint64_t seed) const {
    constexpr size_t BUF_SIZE = 4 * 1024 * 1024;
    std::vector<char> buf(BUF_SIZE);

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return 0;
    }

    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, seed);

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

std::vector<std::unique_ptr<FileRecordDTO>> LocalStorage::initialFiles() {
    std::vector<std::unique_ptr<FileRecordDTO>> result;
    for (auto& entry : std::filesystem::recursive_directory_iterator(_local_home_dir)) {
        const auto& p = entry.path();
        auto rel_path = p.lexically_relative(_local_home_dir);
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
        ".doc", ".docx", ".odt", ".rtf",

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

bool LocalStorage::thatFileTmpExists(const std::filesystem::path& path) const {
    static constexpr std::string_view our_prefix{ ".-tmp-SyncHarbor-" };

    const std::string fn = path.filename().string();
    const std::filesystem::path pp = path.parent_path();

    static constexpr std::array<std::string_view, 6> prefixes{
        our_prefix,
        ".goutputstream-",
        ".kate-swp",
        ".#",
        ".~lock."
    };

    for (auto p : prefixes)
        if (std::filesystem::exists(pp / (std::string(p) + std::string(fn)))) {
            return true;
        }

    static constexpr std::array<std::string_view, 8> suffixes{
        ".swp", ".swo", ".swx",
        ".tmp", ".temp",
        ".bak", ".orig",
        "~"
    };

    for (auto s : suffixes)
        if (std::filesystem::exists(pp / (std::string(fn) + std::string(s)))) {
            return true;
        }

    return false;
}

void LocalStorage::handleDeleted(const FileEvent& evt) {
    if (_expected_events.check(evt.path, ChangeType::Delete)) {
        LOG_DEBUG("LocalStorage", "Expected DELETE: %s", evt.path.string());
        return;
    }
    if (this->ignoreTmp(evt.path)) {
        LOG_DEBUG("LocalStorage", "Ignore tmp DELETE: %s", evt.path.string());
        return;
    }
    if (std::filesystem::exists(evt.path) || this->thatFileTmpExists(evt.path)) {
        LOG_DEBUG("LocalStorage", "Part of atomic update %s", evt.path.string());
        return;
    }

    LOG_DEBUG("LocalStorage", "True DELETE: %s", evt.path.string());

    auto full = evt.path;
    auto rel = full.lexically_relative(_local_home_dir);

    int global_id = 0;
    auto rec = _db->getFileByPath(rel);

    if (!rec) {
        LOG_WARNING("LocalStorage", "Delete of unknown file: %s", evt.path.string());
        return;
    }
    global_id = rec->global_id;

    auto dto = std::make_unique<FileDeletedDTO>(
        rel,
        global_id,
        evt.when
    );
    auto ch = ChangeFactory::makeDelete(std::move(dto));

    _changes_queue.push(std::move(ch));
}

void LocalStorage::handleRenamed(const FileEvent& evt) {
    if (evt.associated) {
        bool path_tmp = ignoreTmp(evt.path);
        bool assoc_tmp = ignoreTmp(evt.associated->path);
        if (!path_tmp && assoc_tmp) {
            return;
        }
    }
    if (!evt.associated) {
        auto full = evt.path;
        if (!std::filesystem::exists(full)) {
            handleDeleted(evt);
        }
        else if (_db->getFileByPath(full) == nullptr) {
            handleCreated(evt);
            if (std::filesystem::is_directory(full)) {
                for (auto& entry : std::filesystem::recursive_directory_iterator(full)) {
                    if (ignoreTmp(entry.path())) {
                        continue;
                    }
                    if (std::filesystem::is_regular_file(entry.path()) || std::filesystem::is_directory(entry.path())) {
                        FileEvent sub_evt{
                            entry.path(),
                            evt.when,
                            ChangeType::New
                        };
                        handleCreated(sub_evt);
                    }
                }
            }
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
        if (!ignoreTmp(evt.associated->path)) {
            LOG_DEBUG("LocalStorage", "Updated via tmp file: %s", evt.path.string());
            handleUpdated(*evt.associated);
            return;
        }
        LOG_DEBUG("LocalStorage", "Ignore tmp MOVE: %s", evt.path.string());
        return;
    }
    if (_expected_events.check(evt.path, ChangeType::Move)) {
        LOG_DEBUG("LocalStorage", "Expected MOVE: %s", evt.path.string());
        return;
    }


    auto full_old = evt.path;
    auto full_new = evt.associated->path;
    auto new_rel = full_new.lexically_relative(_local_home_dir);
    auto old_rel = full_old.lexically_relative(_local_home_dir);

    uint64_t file_id = this->getFileId(full_new);

    auto rec = _db->getFileByFileId(file_id);
    if (!rec) {
        LOG_WARNING("LocalStorage", "Move of unknown file_id: %s", evt.path.string());
        handleCreated(*evt.associated.get());
        return;
    }

    LOG_DEBUG("LocalStorage", "True moved: %s", evt.path.string());

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
            "trying to rename from %s to %s",
            (_local_home_dir.string() + "/" + dto->rel_path.parent_path().string() + "/.-tmp-SyncHarbor-" + dto->rel_path.filename().string()),
            _local_home_dir.string() + "/" + dto->rel_path.string()
        );
        std::filesystem::rename(
            _local_home_dir.string() + "/" + dto->rel_path.parent_path().string() + "/.-tmp-SyncHarbor-" + dto->rel_path.filename().string(),
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
        dto->global_id = global_id;
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

    auto full = evt.path;
    auto rel = full.lexically_relative(_local_home_dir);
    EntryType t = std::filesystem::is_directory(full) ? EntryType::Directory : (this->isDoc(full) ? EntryType::Document : EntryType::File);
    uint64_t hash = t != EntryType::Directory ? computeFileHash(full) : 0;
    uint64_t sz = t != EntryType::Directory ? std::filesystem::file_size(full) : 0;

    auto dto = std::make_unique<FileRecordDTO>(
        t,
        rel,
        sz,
        evt.when,
        hash,
        evt.file_id == 0 ? getFileId(full) : evt.file_id
    );
    auto ch = ChangeFactory::makeLocalNew(std::move(dto));

    LOG_DEBUG("LocalStorage", "True CREATE: %s", evt.path.string());

    _changes_queue.push(std::move(ch));
}

void LocalStorage::handleUpdated(const FileEvent& evt) {
    if (ignoreTmp(evt.path)) {
        LOG_DEBUG("LocalStorage", "Ignore tmp UPDATE: %s", evt.path.string());
        return;
    }


    auto full = evt.path;
    auto rel = full.lexically_relative(_local_home_dir);
    uint64_t file_id = this->getFileId(full);

    auto rec = _db->getFileByFileId(file_id);
    if (!rec) {
        rec = _db->getFileByPath(rel);
        if (!rec) {
            LOG_WARNING("LocalStorage", "Update of unknown file: %s", rel.string());
            return;
        }
    }
    uint64_t hash = computeFileHash(full);
    std::time_t tm = fromWatcherTime(evt.when);
    if (hash == 0 || !std::filesystem::exists(full) || (rec->cloud_file_modified_time >= tm && std::get<uint64_t>(rec->cloud_hash_check_sum) == hash)) {
        LOG_DEBUG("LocalStorage", "Fake UPDATE: %s", evt.path.string());
        return;
    }


    LOG_DEBUG("LocalStorage", "True UPDATE: %s", evt.path.string());

    uint64_t sz = std::filesystem::file_size(full);

    auto dto = std::make_unique<FileUpdatedDTO>(
        rec->type,
        rec->global_id,
        hash,
        tm,
        rel,
        sz,
        file_id
    );

    CallbackDispatcher::get().syncDbWrite(dto);

    auto ch = ChangeFactory::makeLocalUpdate(std::move(dto));
    _changes_queue.push(std::move(ch));
}

bool LocalStorage::hasChanges() const {
    return !_events_buff.empty();
}