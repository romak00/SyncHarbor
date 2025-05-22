#include "dropbox.h"
#include "logger.h"
#include "Networking.h"
#include "change-factory.h"

Dropbox::Dropbox(
    const std::string& client_id,
    const std::string& client_secret,
    const std::string& refresh_token,
    const std::filesystem::path& home_path,
    const std::filesystem::path& local_home_path,
    const std::shared_ptr<Database>& db_conn,
    const int cloud_id
)
    :
    _client_id(client_id),
    _client_secret(client_secret),
    _refresh_token(refresh_token),
    _home_path(home_path),
    _local_home_path(local_home_path),
    _db(db_conn),
    _id(cloud_id)
{
    this->refreshAccessToken();
    this->ensureRootExists();
}

Dropbox::Dropbox(
    const std::string& client_id,
    const std::string& client_secret,
    const std::filesystem::path& home_path,
    const std::filesystem::path& local_home_path,
    const std::shared_ptr<Database>& db_conn,
    const int cloud_id
)
    :
    _client_id(client_id),
    _client_secret(client_secret),
    _home_path(home_path),
    _local_home_path(local_home_path),
    _db(db_conn),
    _id(cloud_id)
{
}

Dropbox::Dropbox(
    const std::string& client_id,
    const std::string& client_secret,
    const std::string& refresh_token,
    const std::filesystem::path& home_path,
    const std::filesystem::path& local_home_path,
    const std::shared_ptr<Database>& db_conn,
    const int cloud_id,
    const std::string& start_page_token
)
    :
    _client_id(client_id),
    _client_secret(client_secret),
    _refresh_token(refresh_token),
    _page_token(start_page_token),
    _home_path(home_path),
    _local_home_path(local_home_path),
    _db(db_conn),
    _id(cloud_id)
{
    this->refreshAccessToken();
    this->ensureRootExists();
}

bool Dropbox::isDropboxShortcutJsonFile(const std::filesystem::path& path) const {
    static const std::unordered_set<std::string> shortcut_exts = {
        ".gdoc", ".gsheet", ".gslide", ".gdraw", ".gform", ".gsite", ".jam", ".paper"
    };

    std::string ext = path.extension().string();

    return shortcut_exts.contains(ext);
}

void Dropbox::setupUploadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const {

    nlohmann::json j;
    j["path"] = _home_path.string() + "/" + dto->rel_path.string();
    j["mode"] = "overwrite";
    j["mute"] = true;
    j["strict_conflict"] = false;

    handle->addHeaders("Authorization: Bearer " + _access_token);

    std::string dropbox_api_arg = j.dump();
    handle->addHeaders("Dropbox-API-Arg: " + dropbox_api_arg);

    if (dto->type == EntryType::Document && this->isDropboxShortcutJsonFile(dto->rel_path)) {
        handle->addHeaders("Content-Type: application/json");
    }
    else {
        handle->addHeaders("Content-Type: application/octet-stream");
    }

    handle->setFileStream((_local_home_path.string() + "/" + dto->rel_path.string()), std::ios::in);

    curl_easy_setopt(handle->_curl, CURLOPT_URL, "https://content.dropboxapi.com/2/files/upload");
    curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);

    handle->setCommonCURLOpt();

    _expected_events.add(dto->rel_path, ChangeType::New);
}

void Dropbox::setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const {
    std::string remote_path = (_home_path / dto->rel_path).string();
    std::string url;

    if (dto->type == EntryType::Document && this->isDropboxShortcutJsonFile(dto->rel_path)) {
        url = "https://content.dropboxapi.com/2/files/export";
    }
    else {
        url = "https://content.dropboxapi.com/2/files/download";
    }

    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->addHeaders("Dropbox-API-Arg: {\"path\": \"" + remote_path + "\"}");

    auto local_tmp_path = _local_home_path / dto->rel_path.parent_path() /
        (".-tmp-SyncHarbor-" + dto->rel_path.filename().string());

    curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());
    handle->setCommonCURLOpt();
    handle->setFileStream(local_tmp_path, std::ios::out);
}

void Dropbox::setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileUpdatedDTO>& dto) const {
    std::string remote_path = (_home_path / dto->rel_path).string();
    std::string url;

    if (dto->type == EntryType::Document && this->isDropboxShortcutJsonFile(dto->rel_path)) {
        url = "https://content.dropboxapi.com/2/files/export";
    }
    else {
        url = "https://content.dropboxapi.com/2/files/download";
    }

    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->addHeaders("Dropbox-API-Arg: {\"path\": \"" + remote_path + "\"}");

    auto local_tmp_path = _local_home_path / dto->rel_path.parent_path() /
        (".-tmp-SyncHarbor-" + dto->rel_path.filename().string());

    curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());
    handle->setCommonCURLOpt();
    handle->setFileStream(local_tmp_path, std::ios::out);
}

void Dropbox::setupUpdateHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileUpdatedDTO>& dto) const {
    std::filesystem::path file_path = _local_home_path / dto->rel_path;

    std::string url = "https://content.dropboxapi.com/2/files/upload";
    curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);

    nlohmann::json j;
    j["path"] = "/" + dto->rel_path.string();
    j["mode"] = "overwrite";
    j["mute"] = true;
    j["strict_conflict"] = false;

    std::string api_arg = j.dump();

    if (dto->type == EntryType::Document && this->isDropboxShortcutJsonFile(dto->rel_path)) {
        handle->addHeaders("Content-Type: application/json");
    }
    else {
        handle->addHeaders("Content-Type: application/octet-stream");
    }

    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->addHeaders("Dropbox-API-Arg: " + api_arg);

    handle->setFileStream(file_path.string(), std::ios::in);
    handle->setCommonCURLOpt();

    _expected_events.add(dto->cloud_file_id, ChangeType::Update);
}

void Dropbox::setupMoveHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileMovedDTO>& dto) const {
    auto makeRemote = [this](const std::filesystem::path& rel) {
        std::filesystem::path p = _home_path / rel;
        std::string s = p.generic_string();
        if (s.empty() || s[0] != '/')
            s = "/" + s;
        return s;
        };

    const std::string from_path = makeRemote(dto->old_rel_path);
    const std::string to_path = makeRemote(dto->new_rel_path);

    nlohmann::json body = {
        {"from_path", from_path},
        {"to_path",   to_path},
        {"allow_shared_folder", true},
        {"autorename", false},
        {"allow_ownership_transfer", false}
    };

    std::string body_str = body.dump();

    curl_easy_setopt(handle->_curl, CURLOPT_URL,
        "https://api.dropboxapi.com/2/files/move_v2");
    curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);
    curl_easy_setopt(handle->_curl, CURLOPT_COPYPOSTFIELDS, body_str.c_str());

    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->addHeaders("Content-Type: application/json");
    handle->setCommonCURLOpt();

    _expected_events.add(dto->old_rel_path, ChangeType::Delete);
    _expected_events.add(dto->cloud_file_id, ChangeType::Move);
}

std::vector<std::unique_ptr<FileRecordDTO>> Dropbox::createPath(const std::filesystem::path& path, const std::filesystem::path& missing) {
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

    auto handle = std::make_unique<RequestHandle>();
    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->addHeaders("Content-Type: application/json");
    handle->setCommonCURLOpt();

    std::filesystem::path accum = _home_path / prefix;

    for (auto const& seg : missing) {
        accum /= seg;
        nlohmann::json cf_req = {
            { "path",     accum.string() },
            { "autorename", false }
        };
        std::string cf_body = cf_req.dump();

        curl_easy_setopt(handle->_curl, CURLOPT_URL,
            "https://api.dropboxapi.com/2/files/create_folder_v2");
        curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);
        curl_easy_setopt(handle->_curl, CURLOPT_COPYPOSTFIELDS,
            cf_body.c_str());
        handle->_response.clear();

        _expected_events.add(std::filesystem::relative(accum, _home_path), ChangeType::New);

        HttpClient::get().syncRequest(handle);

        nlohmann::json j = nlohmann::json::parse(handle->_response);

        std::string folder_id;
        if (j.contains("error_summary")) {
            auto summary = j["error_summary"].get<std::string>();
            if (summary.rfind("path/conflict/folder", 0) == 0) {
                nlohmann::json md_req = {
                    { "path", accum.string() }
                };
                std::string md_body = md_req.dump();

                curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);
                curl_easy_setopt(handle->_curl, CURLOPT_URL,
                    "https://api.dropboxapi.com/2/files/get_metadata");
                curl_easy_setopt(handle->_curl, CURLOPT_COPYPOSTFIELDS,
                    md_body.c_str());
                handle->_response.clear();

                HttpClient::get().syncRequest(handle);
                auto md = nlohmann::json::parse(handle->_response);
                folder_id = md["id"].get<std::string>();
            }
            else {
                throw std::runtime_error(
                    "Dropbox create_folder_v2 error: " + summary
                );
            }
            _expected_events.check(std::filesystem::relative(accum, _home_path), ChangeType::New);
        }
        else {
            auto meta = j["metadata"];
            folder_id = meta["id"].get<std::string>();
        }

        auto dto = std::make_unique<FileRecordDTO>(
            EntryType::Directory,
            std::filesystem::relative(accum, _home_path),
            folder_id,
            0ULL,
            0,
            std::string{},
            _id
        );
        created.push_back(std::move(dto));
    }

    return created;
}

void Dropbox::setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) const {
    auto makeRemote = [this](const std::filesystem::path& rel) {
        std::filesystem::path p = _home_path / rel;
        std::string s = p.generic_string();
        if (s.empty() || s[0] != '/')
            s = "/" + s;
        return s;
        };

    nlohmann::json body = {
        {"path", makeRemote(dto->rel_path)}
    };

    std::string body_str = body.dump();

    curl_easy_setopt(handle->_curl, CURLOPT_URL,
        "https://api.dropboxapi.com/2/files/delete_v2");
    curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);
    curl_easy_setopt(handle->_curl, CURLOPT_POSTFIELDS, body_str.c_str());

    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->addHeaders("Content-Type: application/json");
    handle->setCommonCURLOpt();

    _expected_events.add(dto->rel_path, ChangeType::Delete);
}

std::vector<std::unique_ptr<FileRecordDTO>> Dropbox::initialFiles() {
    LOG_INFO("Dropbox", "initialFiles() start for cloud_id=%d", _id);

    std::vector<std::unique_ptr<FileRecordDTO>> result;

    auto handle = std::make_unique<RequestHandle>();
    curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);

    curl_easy_setopt(handle->_curl,
        CURLOPT_URL,
        "https://api.dropboxapi.com/2/files/list_folder");

    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->addHeaders("Content-Type: application/json");

    handle->setCommonCURLOpt();

    nlohmann::json body = {
        { "path",         _home_path },
        { "recursive",    true },
        { "include_media_info", false },
        { "include_deleted",    false },
        { "include_non_downloadable_files", true }
    };
    std::string body_str = body.dump();
    curl_easy_setopt(handle->_curl, CURLOPT_COPYPOSTFIELDS, body_str.c_str());

    HttpClient::get().syncRequest(handle);
    auto rsp = nlohmann::json::parse(handle->_response);

    auto proccess_entries = [&](const nlohmann::json& entries) {
        for (auto& e : entries) {
            bool is_folder = (e[".tag"] == "folder");
            bool is_doc = !e.value("is_downloadable", true);
            std::string id = e["id"].get<std::string>();
            std::string path_str = e["path_display"].get<std::string>();

            std::filesystem::path rel_path = std::filesystem::relative(path_str, _home_path);
            if (rel_path.empty() || rel_path == std::filesystem::path(".")) {
                continue;
            }

            LOG_DEBUG("Dropbox", "  Found %s id=%s path=%s",
                is_folder ? "DIR" : "FILE",
                id.c_str(),
                path_str.c_str());

            std::time_t mtime = is_folder ? 0 : convertCloudTime(e["server_modified"].get<std::string>());
            std::string hash = is_folder ? "" : e["content_hash"].get<std::string>();
            uint64_t size = is_folder ? 0ULL : e.value("size", 0ULL);

            auto dto = std::make_unique<FileRecordDTO>(
                is_folder ? EntryType::Directory : (is_doc ? EntryType::Document : EntryType::File),
                rel_path,
                id,
                size,
                mtime,
                hash,
                _id
            );
            result.push_back(std::move(dto));
        }
        };

    proccess_entries(rsp["entries"]);

    bool has_more = rsp.value("has_more", false);
    std::string cursor = rsp.value("cursor", "");

    while (has_more) {
        nlohmann::json cont_body = { { "cursor", cursor } };
        std::string cont_str = cont_body.dump();

        curl_easy_setopt(handle->_curl, CURLOPT_URL,
            "https://api.dropboxapi.com/2/files/list_folder/continue");
        curl_easy_setopt(handle->_curl, CURLOPT_COPYPOSTFIELDS, cont_str.c_str());
        handle->_response.clear();

        HttpClient::get().syncRequest(handle);
        auto cont_rsp = nlohmann::json::parse(handle->_response);

        LOG_DEBUG("Dropbox", "continue response: %s", handle->_response.c_str());
        proccess_entries(cont_rsp["entries"]);

        has_more = cont_rsp.value("has_more", false);
        cursor = cont_rsp.value("cursor", "");
    }

    LOG_INFO("Dropbox", "initialFiles() done, total entries = %zu", result.size());
    return result;
}

int Dropbox::id() const {
    return _id;
}

bool Dropbox::hasChanges() const {
    return !_events_buff.empty();
}

void Dropbox::ensureRootExists() {
    auto handle = std::make_unique<RequestHandle>();

    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->addHeaders("Content-Type: application/json");
    handle->setCommonCURLOpt();

    nlohmann::json body = {
        {"path", _home_path.string()},
        {"autorename", false}
    };

    std::string payload = body.dump();

    curl_easy_setopt(handle->_curl, CURLOPT_URL,
        "https://api.dropboxapi.com/2/files/create_folder_v2");
    curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);
    curl_easy_setopt(handle->_curl, CURLOPT_POSTFIELDS, payload.c_str());

    HttpClient::get().syncRequest(handle);
}

void Dropbox::proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const {
    auto json_response = nlohmann::json::parse(response);
    if (dto->type == EntryType::Directory) {
        dto->cloud_file_id = json_response["metadata"]["id"];
    }
    else {
        dto->cloud_file_id = json_response["id"];
        dto->cloud_file_modified_time = convertCloudTime(json_response["server_modified"]);
        dto->cloud_hash_check_sum = json_response["content_hash"].get<std::string>();
    }
}

void Dropbox::proccesUpdate(std::unique_ptr<FileUpdatedDTO>& dto, const std::string& response) const {
    auto j = nlohmann::json::parse(response);

    if (j.contains("metadata"))
        j = j["metadata"];

    dto->cloud_file_id = j.value("id", std::string{});

    if (dto->type != EntryType::Directory && j.value(".tag", "") == "file")
    {
        dto->cloud_file_modified_time =
            convertCloudTime(j["server_modified"].get<std::string>());

        dto->cloud_hash_check_sum = j.value("content_hash", std::string{});
        dto->size = j.value("size", 0LL);
    }
}

void Dropbox::proccesMove(std::unique_ptr<FileMovedDTO>& dto, const std::string& response) const {
    auto j = nlohmann::json::parse(response);

    if (!j.contains("metadata"))
        return;

    const auto& meta = j["metadata"];

    if (dto->type == EntryType::Directory)
        return;

    if (meta.contains(".tag") &&
        meta[".tag"] == "file" &&
        meta.contains("server_modified"))
    {
        dto->cloud_file_modified_time =
            convertCloudTime(meta["server_modified"].get<std::string>());
    }
}

void Dropbox::proccesDownload(std::unique_ptr<FileUpdatedDTO>& dto, const std::string& response) const {}
void Dropbox::proccesDelete(std::unique_ptr<FileDeletedDTO>& dto, const std::string& response) const {}


std::string Dropbox::buildAuthURL(int local_port) const {
    std::string redirect = "http://localhost:" +
        std::to_string(local_port) +
        "/oauth2callback";

    char* enc_redirect = curl_easy_escape(nullptr, redirect.c_str(), 0);

    std::ostringstream oss;
    oss << "https://www.dropbox.com/oauth2/authorize?"
        << "client_id=" << _client_id
        << "&redirect_uri=" << enc_redirect
        << "&response_type=code"
        << "&token_access_type=offline";

    curl_free(enc_redirect);
    return oss.str();
}
void Dropbox::getRefreshToken(const std::string& code, const int local_port) {
    auto handle = std::make_unique<RequestHandle>();

    std::string post_fields =
        "code=" + code +
        "&grant_type=authorization_code"
        "&client_id=" + _client_id +
        "&client_secret=" + _client_secret +
        "&redirect_uri=http://localhost:" + std::to_string(local_port) + "/oauth2callback";

    curl_easy_setopt(handle->_curl, CURLOPT_URL, "https://api.dropboxapi.com/oauth2/token");
    curl_easy_setopt(handle->_curl, CURLOPT_POSTFIELDS, post_fields.c_str());
    curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);
    handle->setCommonCURLOpt();

    HttpClient::get().syncRequest(handle);
    proccessAuth(handle->_response);
}

void Dropbox::refreshAccessToken() {
    auto handle = std::make_unique<RequestHandle>();

    std::string post_fields =
        "grant_type=refresh_token"
        "&refresh_token=" + _refresh_token +
        "&client_id=" + _client_id +
        "&client_secret=" + _client_secret;

    curl_easy_setopt(handle->_curl, CURLOPT_URL, "https://api.dropboxapi.com/oauth2/token");
    curl_easy_setopt(handle->_curl, CURLOPT_POSTFIELDS, post_fields.c_str());
    curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);
    handle->setCommonCURLOpt();

    HttpClient::get().syncRequest(handle);
    proccessAuth(handle->_response);
}

void Dropbox::proccessAuth(const std::string& response) {
    auto j = nlohmann::json::parse(response);

    _access_token = j["access_token"].get<std::string>();

    if (j.contains("refresh_token")) {
        _refresh_token = j["refresh_token"].get<std::string>();
    }

    int expires_in = j["expires_in"].get<int>();
    _access_token_expires = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) + expires_in;

    LOG_INFO("AUTH", "DropboxCloud with id: %i tokens updated, expires in %i", _id, expires_in);
}

std::string Dropbox::getDeltaToken() {
    auto handle = std::make_unique<RequestHandle>();

    nlohmann::json j = {
        {"path", _home_path.string()},
        {"recursive", true},
        {"include_media_info", false},
        {"include_deleted", true},
        {"include_has_explicit_shared_members", false}
    };

    std::string body = j.dump();

    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->addHeaders("Content-Type: application/json");

    curl_easy_setopt(handle->_curl, CURLOPT_URL,
        "https://api.dropboxapi.com/2/files/list_folder/get_latest_cursor");
    curl_easy_setopt(handle->_curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);

    handle->setCommonCURLOpt();

    HttpClient::get().syncRequest(handle);

    auto jrsp = nlohmann::json::parse(handle->_response);
    _page_token = jrsp["cursor"];

    return _page_token;
}

std::string Dropbox::getHomeDir() const {
    return _home_path.string();
}

void Dropbox::getChanges() {
    if (_page_token.empty()) {
        getDeltaToken();
    }

    auto handle = std::make_unique<RequestHandle>();
    std::vector<std::string> pages;
    std::string cursor = _page_token;

    nlohmann::json body_json = { {"cursor", cursor} };
    std::string body = body_json.dump();

    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->addHeaders("Content-Type: application/json");
    curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);
    curl_easy_setopt(handle->_curl, CURLOPT_URL,
        "https://api.dropboxapi.com/2/files/list_folder/continue");
    handle->setCommonCURLOpt();

    bool has_more = false;
    do {
        curl_easy_setopt(handle->_curl, CURLOPT_POSTFIELDS, body.c_str());

        HttpClient::get().syncRequest(handle);

        pages.push_back(handle->_response);

        auto j = nlohmann::json::parse(handle->_response);
        has_more = j.value("has_more", false);
        cursor = j.value("cursor", cursor);

        if (has_more) {
            body_json["cursor"] = cursor;
            body = body_json.dump();
        }
    } while (has_more);

    _page_token = cursor;
    LOG_INFO("Dropbox", "All changes received");

    bool any_entries = false;
    for (const auto& raw : pages) {
        auto j = nlohmann::json::parse(raw);
        if (!j.value("entries", nlohmann::json::array()).empty()) {
            any_entries = true;
            break;
        }
    }
    if (any_entries) {
        _events_buff.push(pages);
        _onChange();
    }
}

void Dropbox::setOnChange(std::function<void()> cb) {
    _onChange = std::move(cb);
}

std::vector<std::shared_ptr<Change>> Dropbox::proccessChanges() {
    std::vector<std::shared_ptr<Change>> changes;
    std::vector<std::string> pages;

    std::unordered_map<std::string, std::shared_ptr<Change>> pending_deletes;

    if (!_events_buff.try_pop(pages))
        return changes;

    PrevEventsRegistry old_expected(_expected_events.copyMap());

    for (const auto& raw : pages) {
        auto j = nlohmann::json::parse(raw);
        for (auto& entry : j.value("entries", nlohmann::json::array())) {
            std::string tag = entry[".tag"].get<std::string>();
            bool is_dir = (tag == "folder");
            bool is_doc = !entry.value("is_downloadable", true);
            std::string cloud_file_id = entry.value("id", std::string{});

            std::string path_str = entry.value("path_display", std::string{});
            std::filesystem::path path = path_str;
            std::filesystem::path rel_path = std::filesystem::relative(path, _home_path);

            if (rel_path.empty() || rel_path == std::filesystem::path(".")) {
                continue;
            }

            if (cloud_file_id == "") {
                cloud_file_id = _db->getCloudFileIdByPath(rel_path, _id);
            }

            std::time_t mtime = 0;
            uint64_t size = 0;
            std::string hash;

            if (tag != "deleted" && !is_dir) {
                mtime = convertCloudTime(entry.value("server_modified", std::string{}));
                size = entry.value("size", 0ULL);
                hash = entry.value("content_hash", std::string{});
            }

            EntryType type = is_dir ? EntryType::Directory : (is_doc ? EntryType::Document : EntryType::File);

            auto link = _db->getFileByCloudIdAndCloudFileId(_id, cloud_file_id);
            int  global_id = link ? link->global_id : 0;
            auto old_path = link ? _db->getPathByGlobalId(global_id) : std::filesystem::path{};
            bool existed = (link != nullptr);

            bool need_new = !existed && tag != "deleted";
            bool need_del = existed && tag == "deleted";
            bool need_move = existed && tag != "deleted" && rel_path != old_path;
            bool need_update = existed && !is_dir && tag != "deleted"
                && mtime > link->cloud_file_modified_time
                && hash != get<std::string>(link->cloud_hash_check_sum);

            LOG_DEBUG("Dropbox",
                "Eval change: old_path=\"%s\", new_path=\"%s\", new=%d, del=%d, move=%d, upd=%d",
                old_path.string().c_str(),
                rel_path.string().c_str(),
                (int)need_new, (int)need_del, (int)need_move, (int)need_update);

            if (need_new && old_expected.check(rel_path, ChangeType::New)) {
                LOG_DEBUG("Dropbox", "Expected NEW: %s", raw.c_str());
                continue;
            }
            if (need_del && old_expected.check(rel_path, ChangeType::Delete)) {
                LOG_DEBUG("Dropbox", "Expected DELETE: %s", raw.c_str());
                continue;
            }
            if (need_move && old_expected.check(cloud_file_id, ChangeType::Move)) {
                LOG_DEBUG("Dropbox", "Expected MOVE: %s", raw.c_str());
                continue;
            }
            if (need_update && old_expected.check(cloud_file_id, ChangeType::Update)) {
                LOG_DEBUG("Dropbox", "Expected UPDATE: %s", raw.c_str());
                continue;
            }

            std::shared_ptr<Change> ch = nullptr;
            if (need_move && need_update) {
                LOG_DEBUG("Dropbox", "  -> emit MOVE+UPDATE for: \"%s\" -> \"%s\"",
                    old_path.string().c_str(),
                    rel_path.string().c_str());
                // MOVE
                auto m_dto = std::make_unique<FileMovedDTO>(
                    type, global_id, _id, cloud_file_id,
                    mtime, old_path, rel_path,
                    /*oldParent=*/"", /*newParent=*/""
                );
                if (pending_deletes.contains(rel_path.string())) {
                    pending_deletes.erase(rel_path.string());
                }
                ch = ChangeFactory::makeCloudMove(std::move(m_dto));

                // UPDATE as dependent
                auto u_dto = std::make_unique<FileUpdatedDTO>(
                    type, global_id, _id, cloud_file_id,
                    hash, mtime, rel_path, /*newParent=*/"", size
                );
                auto updCh = ChangeFactory::makeCloudUpdate(std::move(u_dto));
                ch->addDependent(std::move(updCh));
            }
            else if (need_new) {
                LOG_DEBUG("Dropbox", "  -> emit NEW for: \"%s\"", rel_path.string().c_str());
                auto dto = std::make_unique<FileRecordDTO>(
                    type, rel_path,
                    cloud_file_id, size, mtime, hash, _id
                );
                ch = ChangeFactory::makeCloudNew(std::move(dto));
            }
            else if (need_del) {
                LOG_DEBUG("Dropbox", "  -> emit DELETE for: \"%s\"", rel_path.string().c_str());
                auto dto = std::make_unique<FileDeletedDTO>(
                    rel_path, global_id, _id, cloud_file_id, mtime
                );
                auto mb_del = ChangeFactory::makeDelete(std::move(dto));
                pending_deletes.emplace(rel_path.string(), std::move(mb_del));
            }
            else if (need_move) {
                LOG_DEBUG("Dropbox", "  -> emit MOVE for: \"%s\" -> \"%s\"",
                    old_path.string().c_str(),
                    rel_path.string().c_str());
                auto dto = std::make_unique<FileMovedDTO>(
                    type, global_id, _id, cloud_file_id,
                    mtime, old_path, rel_path,
                    /*oldParent=*/"", /*newParent=*/""
                );
                if (pending_deletes.contains(rel_path.string())) {
                    pending_deletes.erase(rel_path.string());
                }
                ch = ChangeFactory::makeCloudMove(std::move(dto));
            }
            else if (need_update) {
                LOG_DEBUG("GoogleDrive", "  -> emit UPDATE for: \"%s\"", rel_path.string().c_str());
                auto dto = std::make_unique<FileUpdatedDTO>(
                    type, global_id, _id, cloud_file_id,
                    hash, mtime, rel_path, /*newParent=*/"", size
                );
                ch = ChangeFactory::makeCloudUpdate(std::move(dto));
            }

            if (ch) {
                changes.emplace_back(std::move(ch));
            }
        }
    }
    for (auto& [path, del] : pending_deletes) {
        changes.emplace_back(std::move(del));
    }

    return changes;
}