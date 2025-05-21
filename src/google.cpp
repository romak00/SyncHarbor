#include "google.h"
#include "Networking.h"
#include "logger.h"
#include "change-factory.h"

GoogleDrive::GoogleDrive(
    const std::string& client_id,
    const std::string& client_secret,
    const std::string& refresh_token,
    const std::filesystem::path& home_dir,
    const std::filesystem::path& local_home_dir,
    const std::shared_ptr<Database>& db_conn,
    const int cloud_id
)
    :
    _client_id(client_id),
    _client_secret(client_secret),
    _refresh_token(refresh_token),
    _local_home_path(local_home_dir),
    _db(db_conn),
    _id(cloud_id),
    _home_path(home_dir)
{
    this->refreshAccessToken();
    this->ensureRootExists();
}

GoogleDrive::GoogleDrive(
    const std::string& client_id,
    const std::string& client_secret,
    const std::filesystem::path& home_dir,
    const std::filesystem::path& local_home_dir,
    const std::shared_ptr<Database>& db_conn,
    const int cloud_id
)
    :
    _client_id(client_id),
    _client_secret(client_secret),
    _local_home_path(local_home_dir),
    _db(db_conn),
    _id(cloud_id),
    _home_path(home_dir)
{
}

GoogleDrive::GoogleDrive(
    const std::string& client_id,
    const std::string& client_secret,
    const std::string& refresh_token,
    const std::filesystem::path& home_dir,
    const std::filesystem::path& local_home_dir,
    const std::shared_ptr<Database>& db_conn,
    const int cloud_id,
    const std::string& start_page_token
) :
    _client_id(client_id),
    _client_secret(client_secret),
    _refresh_token(refresh_token),
    _local_home_path(local_home_dir),
    _page_token(start_page_token),
    _db(db_conn),
    _id(cloud_id),
    _home_path(home_dir)
{
    this->refreshAccessToken();
    this->ensureRootExists();
}

std::optional<GoogleDocMimeInfo> GoogleDrive::getGoogleDocMimeByExtension(const std::filesystem::path& path) const {
    std::string ext = path.extension().string();

    if (ext == ".docx" || ext == ".doc" || ext == ".odt" || ext == ".rtf" || ext == ".txt") {
        return GoogleDocMimeInfo{
            "application/vnd.google-apps.document",
            "application/vnd.openxmlformats-officedocument.wordprocessingml.document"
        };
    }

    if (ext == ".xlsx" || ext == ".xls" || ext == ".ods" || ext == ".csv" || ext == ".tsv") {
        return GoogleDocMimeInfo{
            "application/vnd.google-apps.spreadsheet",
            "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"
        };
    }

    if (ext == ".pptx" || ext == ".ppt" || ext == ".odp") {
        return GoogleDocMimeInfo{
            "application/vnd.google-apps.presentation",
            "application/vnd.openxmlformats-officedocument.presentationml.presentation"
        };
    }

    return std::nullopt;
}

std::vector<std::unique_ptr<FileRecordDTO>> GoogleDrive::createPath(const std::filesystem::path& path, const std::filesystem::path& missing) {
    LOG_INFO("GoogleDrive", "createPath() start for path=%s, missing=%s", path.string().c_str(), missing.string().c_str());

    std::vector<std::unique_ptr<FileRecordDTO>> created;
    auto norm_path = normalizePath(path);

    LOG_DEBUG("GoogleDrive", "createPath() normalized path: %s", norm_path.c_str());

    std::vector<std::filesystem::path> segs;
    for (const auto& s : norm_path) {
        segs.push_back(s);
    }
    int keep = int(segs.size() - std::distance(missing.begin(), missing.end()));
    std::filesystem::path prefix;
    for (int i = 0; i < keep; ++i) {
        prefix /= segs[i];
    }

    LOG_DEBUG("GoogleDrive", "createPath() prefix=%s", prefix.c_str());

    std::string parent_id = prefix.empty() ? _home_dir_id : _db->getCloudFileIdByPath(prefix, _id);

    auto handle = std::make_unique<RequestHandle>();
    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->addHeaders("Accept: application/json");
    handle->setCommonCURLOpt();

    std::filesystem::path accum = prefix;
    bool creating = false;

    LOG_DEBUG("GoogleDrive", "Normalized prefix=%s, parent_id=%s", prefix.c_str(), parent_id.c_str());

    for (const auto& seg : missing) {
        accum /= seg;
        if (!creating) {
            std::string q = "name='" + seg.string() +
                "' and mimeType='application/vnd.google-apps.folder'"
                " and '" + parent_id + "' in parents and trashed=false";
            char* eq = curl_easy_escape(handle->_curl, q.c_str(), 0);
            std::string url =
                "https://www.googleapis.com/drive/v3/files"
                "?q=" + std::string(eq) +
                "&fields=files(id,name,mimeType,modifiedTime)";
            curl_free(eq);

            LOG_DEBUG("GoogleDrive",
                "Checking folder '%s' with URL: %s",
                seg.string().c_str(), url.c_str());

            curl_easy_setopt(handle->_curl, CURLOPT_HTTPGET, 1L);
            curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());
            handle->_response.clear();
            HttpClient::get().syncRequest(handle);

            LOG_DEBUG("GoogleDrive",
                "Response for check '%s': %s",
                seg.string().c_str(),
                handle->_response.c_str());

            auto j = nlohmann::json::parse(handle->_response);
            if (j.contains("files") && !j["files"].empty()) {
                auto& info = j["files"][0];
                std::string fid = info["id"].get<std::string>();
                std::time_t mtime = convertCloudTime(info["modifiedTime"].get<std::string>());

                auto dto = std::make_unique<FileRecordDTO>(
                    EntryType::Directory,
                    parent_id,
                    accum,
                    fid,
                    0ULL,
                    mtime,
                    std::string{},
                    _id
                );
                LOG_INFO("GoogleDrive",
                    "Folder exists id=%s, path=%s",
                    fid.c_str(),
                    accum.string().c_str());
                created.push_back(std::move(dto));
                parent_id = fid;
                continue;
            }
            creating = true;

        }

        LOG_INFO("GoogleDrive", "Creating folder: %s", accum.c_str());

        nlohmann::json body = {
            {"name",      seg.string()},
            {"mimeType",  "application/vnd.google-apps.folder"},
            {"parents",   { parent_id }}
        };
        std::string payload = body.dump();

        handle->clearHeaders();
        handle->addHeaders("Authorization: Bearer " + _access_token);
        handle->addHeaders("Content-Type: application/json; charset=UTF-8");
        handle->addHeaders("Accept: application/json");

        curl_easy_setopt(handle->_curl, CURLOPT_HTTPGET, 0L);
        curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);

        curl_easy_setopt(handle->_curl, CURLOPT_URL,
            "https://www.googleapis.com/drive/v3/files?fields=id,modifiedTime");
        curl_easy_setopt(handle->_curl, CURLOPT_POSTFIELDS,
            payload.c_str());

        handle->setCommonCURLOpt();
        handle->_response.clear();

        _expected_events.add(accum, ChangeType::New);

        HttpClient::get().syncRequest(handle);

        LOG_DEBUG("GoogleDrive", "Response for folder '%s': %s", seg.string().c_str(), handle->_response.c_str());

        auto j2 = nlohmann::json::parse(handle->_response);

        std::string cloud_file_id = j2["id"].get<std::string>();

        auto dto = std::make_unique<FileRecordDTO>(
            EntryType::Directory,
            parent_id,
            accum,
            cloud_file_id,
            0ULL,
            convertCloudTime(j2["modifiedTime"].get<std::string>()),
            std::string{},
            _id
        );

        LOG_INFO("GoogleDrive", "Created directory id=%s, path=%s", cloud_file_id.c_str(), accum.string().c_str());

        parent_id = cloud_file_id;

        created.push_back(std::move(dto));
    }

    LOG_INFO("GoogleDrive", "createPath() done, total created = %zu", created.size());

    return created;
}

void GoogleDrive::refreshAccessToken() {
    auto handle = std::make_unique<RequestHandle>();

    std::string post =
        "client_id=" + _client_id +
        "&client_secret=" + _client_secret +
        "&refresh_token=" + _refresh_token +
        "&grant_type=refresh_token";

    curl_easy_setopt(handle->_curl, CURLOPT_URL,
        "https://oauth2.googleapis.com/token");
    curl_easy_setopt(handle->_curl, CURLOPT_POSTFIELDS, post.c_str());
    curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);
    handle->setCommonCURLOpt();

    HttpClient::get().syncRequest(handle);
    proccessAuth(handle->_response);
}

void GoogleDrive::setupUpdateHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileUpdatedDTO>& dto) const {
    std::filesystem::path file_path = _local_home_path / dto->rel_path;

    std::string url = "https://www.googleapis.com/upload/drive/v3/files/" + dto->cloud_file_id +
        "?uploadType=media&fields=id,modifiedTime,md5Checksum,parents,size";

    curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());

    curl_easy_setopt(handle->_curl, CURLOPT_CUSTOMREQUEST, "PATCH");

    curl_easy_setopt(handle->_curl, CURLOPT_UPLOAD, 1L);

    handle->setFileStream(file_path, std::ios::in);

    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->addHeaders("Content-Type: application/octet-stream");

    handle->setCommonCURLOpt();

    _expected_events.add(dto->cloud_file_id, ChangeType::Update);
}

void GoogleDrive::setupMoveHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileMovedDTO>& dto) const {
    std::string url = "https://www.googleapis.com/drive/v3/files/" + dto->cloud_file_id;

    bool parent_changed = (!dto->new_cloud_parent_id.empty() &&
        dto->new_cloud_parent_id != dto->old_cloud_parent_id);

    if (parent_changed) {
        url += "?addParents=" + dto->new_cloud_parent_id +
            "&removeParents=" + dto->old_cloud_parent_id;
    }

    url += (parent_changed ? "&" : "?");
    url += "fields=id,name,parents,modifiedTime,md5Checksum,size";

    curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle->_curl, CURLOPT_CUSTOMREQUEST, "PATCH");

    nlohmann::json body;
    std::string old_name = dto->old_rel_path.filename().string();
    std::string new_name = dto->new_rel_path.filename().string();

    if (old_name != new_name) {
        body["name"] = new_name;
    }

    std::string body_str = body.dump();
    if (!body.empty()) {
        curl_easy_setopt(handle->_curl, CURLOPT_POSTFIELDS, body_str.c_str());
        handle->addHeaders("Content-Type: application/json");
    }

    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->setCommonCURLOpt();

    _expected_events.add(dto->cloud_file_id, ChangeType::Move);
}

void GoogleDrive::setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) const {
    std::string url = "https://www.googleapis.com/drive/v3/files/" + dto->cloud_file_id;

    curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle->_curl, CURLOPT_CUSTOMREQUEST, "DELETE");

    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->setCommonCURLOpt();

    _expected_events.add(dto->cloud_file_id, ChangeType::Delete);
}

std::vector<std::unique_ptr<FileRecordDTO>> GoogleDrive::initialFiles() {
    LOG_INFO("GoogleDrive", "initialFiles() start for cloud_id=%d", _id);

    std::vector<std::unique_ptr<FileRecordDTO>> result;

    std::queue<std::pair<std::string, std::filesystem::path>> dir_queue;

    dir_queue.emplace(_home_dir_id, std::filesystem::path{});

    auto handle = std::make_unique<RequestHandle>();
    curl_easy_setopt(handle->_curl, CURLOPT_HTTPGET, 1L);
    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->addHeaders("Accept: application/json");

    handle->setCommonCURLOpt();


    while (!dir_queue.empty()) {
        auto [parent_id, rel_path] = dir_queue.front();
        dir_queue.pop();

        LOG_DEBUG("GoogleDrive", "Scanning folder id=%s rel_path=%s",
            parent_id.c_str(), rel_path.string().c_str());

        std::string page_token;

        do {
            std::string q = "%27" + parent_id + "%27%20in%20parents%20and%20trashed%3Dfalse";
            std::string url =
                "https://www.googleapis.com/drive/v3/files"
                "?q=" + q +
                "&fields=nextPageToken,files(id,name,mimeType,modifiedTime,size,md5Checksum,parents)"
                "&pageSize=1000";
            if (!page_token.empty()) {
                url += "&pageToken=" + page_token;
            }

            LOG_DEBUG("GoogleDrive", "Request URL: %s", url.c_str());

            curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());

            handle->_response.clear();

            HttpClient::get().syncRequest(handle);

            auto rsp = nlohmann::json::parse(handle->_response);

            for (auto& f : rsp["files"]) {
                bool is_folder = (f["mimeType"] == "application/vnd.google-apps.folder");
                bool is_doc = f["mimeType"].get<std::string>().rfind("application/vnd.google-apps.", 0) == 0;
                std::string id = f["id"].get<std::string>();
                std::string name = f["name"].get<std::string>();
                auto path = rel_path / name;

                uint64_t file_size = 0;
                if (!is_folder && f.contains("size")) {
                    file_size = std::stoull(f["size"].get<std::string>());
                }

                LOG_DEBUG("GoogleDrive",
                    "  Found %s id=%s name=%s",
                    is_folder ? "DIR" : "FILE",
                    id.c_str(),
                    path.c_str());

                auto dto = std::make_unique<FileRecordDTO>(
                    is_folder ? EntryType::Directory : (is_doc ? EntryType::Document : EntryType::File),
                    parent_id,
                    path,
                    id,
                    file_size,
                    convertCloudTime(f["modifiedTime"].get<std::string>()),
                    f.value("md5Checksum", std::string{}),
                    _id
                );

                if (is_folder) {
                    dir_queue.emplace(id, path);
                }

                result.push_back(std::move(dto));
            }
            page_token = rsp.value("nextPageToken", "");

        } while (!page_token.empty());
    }

    LOG_INFO("GoogleDrive", "initialFiles() done, total entries = %i", result.size());
    return result;
}

void GoogleDrive::ensureRootExists() {
    auto handle = std::make_unique<RequestHandle>();

    handle->addHeaders("Authorization: Bearer " + _access_token);
    curl_easy_setopt(handle->_curl, CURLOPT_HTTPGET, 1L);
    handle->setCommonCURLOpt();

    auto path = normalizePath(_home_path);

    std::string parent_id = "root";
    bool found_all = true;

    for (const auto& seg : path) {
        const std::string name = seg.string();

        if (found_all) {
            std::string q = "name='" + name +
                "' and mimeType='application/vnd.google-apps.folder'" +
                " and '" + parent_id + "' in parents and trashed=false";
            char* eq = curl_easy_escape(handle->_curl, q.c_str(), (int)q.size());
            std::string url =
                "https://www.googleapis.com/drive/v3/files"
                "?q=" + std::string(eq) +
                "&fields=files(id)";
            curl_free(eq);

            curl_easy_setopt(handle->_curl, CURLOPT_HTTPGET, 1L);
            curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());
            handle->_response.clear();

            HttpClient::get().syncRequest(handle);

            auto j = nlohmann::json::parse(handle->_response);
            if (j.contains("files") && !j["files"].empty()) {
                parent_id = j["files"][0]["id"].get<std::string>();
                continue;
            }
            found_all = false;
        }

        {
            handle->clearHeaders();

            nlohmann::json body = {
                {"name",     name},
                {"mimeType","application/vnd.google-apps.folder"},
                {"parents", { parent_id }}
            };
            std::string payload = body.dump();

            curl_easy_setopt(handle->_curl, CURLOPT_HTTPGET, 0L);
            curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);

            curl_easy_setopt(handle->_curl, CURLOPT_URL, "https://www.googleapis.com/drive/v3/files");
            curl_easy_setopt(handle->_curl, CURLOPT_POSTFIELDS, payload.c_str());
            handle->_response.clear();

            handle->addHeaders("Authorization: Bearer " + _access_token);
            handle->addHeaders("Content-Type: application/json; charset=UTF-8");
            curl_easy_setopt(handle->_curl, CURLOPT_HTTPHEADER, handle->_headers);


            HttpClient::get().syncRequest(handle);

            auto j2 = nlohmann::json::parse(handle->_response);
            parent_id = j2["id"].get<std::string>();
        }
    }

    _home_dir_id = parent_id;
}

void GoogleDrive::setupUploadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const {
    std::string parent_id;
    if (dto->cloud_parent_id.empty()) {
        if (dto->rel_path.parent_path().empty()) {
            parent_id = _home_dir_id;
        }
        else {
            parent_id = _db->getCloudFileIdByPath(dto->rel_path.parent_path(), _id);
        }
    }
    else {
        parent_id = dto->cloud_parent_id;
    }

    nlohmann::json j;
    j["name"] = dto->rel_path.filename().string();
    j["parents"] = { parent_id };

    std::string cloud_mime, content_mime = "application/octet-stream";

    if (dto->type == EntryType::Document) {
        if (auto mime = this->getGoogleDocMimeByExtension(dto->rel_path)) {
            cloud_mime = mime->cloud_mime_type;
            content_mime = mime->export_mime_type;
            j["mimeType"] = cloud_mime;
        }
        else {
            LOG_ERROR("GoogleDrive", "Cannot determine cloud MIME type for document file: %s", dto->rel_path.c_str());
        }
    }

    handle->addHeaders("Authorization: Bearer " + _access_token);

    const std::string metadata = j.dump();
    handle->_mime = curl_mime_init(handle->_curl);

    curl_mimepart* part = curl_mime_addpart(handle->_mime);
    curl_mime_name(part, "metadata");
    curl_mime_data(part, metadata.c_str(), CURL_ZERO_TERMINATED);
    curl_mime_type(part, "application/json; charset=UTF-8");

    part = curl_mime_addpart(handle->_mime);
    curl_mime_name(part, "media");
    curl_mime_filedata(part, (_local_home_path.string() + "/" + dto->rel_path.string()).c_str());
    curl_mime_type(part, content_mime.c_str());

    curl_easy_setopt(handle->_curl, CURLOPT_MIMEPOST, handle->_mime);
    curl_easy_setopt(handle->_curl, CURLOPT_URL, "https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart&fields=id,modifiedTime,md5Checksum,parents,parents");

    handle->setCommonCURLOpt();

    _expected_events.add(dto->rel_path, ChangeType::New);
}

void GoogleDrive::setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const {
    std::string url;

    if (dto->type == EntryType::Document) {
        std::string export_mime = "application/vnd.openxmlformats-officedocument.wordprocessingml.document";

        if (auto mime = this->getGoogleDocMimeByExtension(dto->rel_path)) {
            export_mime = mime->export_mime_type;
        }

        url = "https://www.googleapis.com/drive/v3/files/" + dto->cloud_file_id +
            "/export?mimeType=" + curl_easy_escape(handle->_curl, export_mime.c_str(), 0);
    }
    else {
        url = "https://www.googleapis.com/drive/v3/files/" + dto->cloud_file_id + "?alt=media";
    }

    handle->addHeaders("Authorization: Bearer " + _access_token);

    auto path = _local_home_path / dto->rel_path.parent_path() /
        (".-tmp-cloudsync-" + dto->rel_path.filename().string());

    curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());
    handle->setCommonCURLOpt();
    handle->setFileStream(path, std::ios::out);
}

void GoogleDrive::setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileUpdatedDTO>& dto) const {
    std::string url;

    if (dto->type == EntryType::Document) {
        std::string export_mime = "application/vnd.openxmlformats-officedocument.wordprocessingml.document";

        if (auto mime = this->getGoogleDocMimeByExtension(dto->rel_path)) {
            export_mime = mime->export_mime_type;
        }

        url = "https://www.googleapis.com/drive/v3/files/" + dto->cloud_file_id +
            "/export?mimeType=" + curl_easy_escape(handle->_curl, export_mime.c_str(), 0);
    }
    else {
        url = "https://www.googleapis.com/drive/v3/files/" + dto->cloud_file_id + "?alt=media";
    }

    handle->addHeaders("Authorization: Bearer " + _access_token);

    auto path = _local_home_path / dto->rel_path.parent_path() /
        (".-tmp-cloudsync-" + dto->rel_path.filename().string());

    curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());
    handle->setCommonCURLOpt();
    handle->setFileStream(path, std::ios::out);
}

std::string GoogleDrive::buildAuthURL(int local_port) const {
    std::string redirect = "http://localhost:" +
        std::to_string(local_port) +
        "/oauth2callback";

    char* enc_redirect = curl_easy_escape(nullptr, redirect.c_str(), 0);
    char* enc_scope = curl_easy_escape(nullptr,
        "https://www.googleapis.com/auth/drive", 0);

    std::ostringstream oss;
    oss << "https://accounts.google.com/o/oauth2/v2/auth?"
        << "client_id=" << _client_id
        << "&redirect_uri=" << enc_redirect
        << "&response_type=code"
        << "&scope=" << enc_scope
        << "&access_type=offline"
        << "&prompt=consent";

    curl_free(enc_redirect);
    curl_free(enc_scope);
    return oss.str();
}

void GoogleDrive::getRefreshToken(const std::string& code, const int local_port) {
    auto handle = std::make_unique<RequestHandle>();

    std::string post_fields =
        "code=" + code +
        "&client_id=" + _client_id +
        "&client_secret=" + _client_secret +
        "&redirect_uri=http://localhost:" + std::to_string(local_port) + "/oauth2callback"
        "&grant_type=authorization_code";

    curl_easy_setopt(handle->_curl, CURLOPT_URL, "https://oauth2.googleapis.com/token");
    curl_easy_setopt(handle->_curl, CURLOPT_POSTFIELDS, post_fields.c_str());
    curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);

    handle->setCommonCURLOpt();

    HttpClient::get().syncRequest(handle);
    proccessAuth(handle->_response);
}

void GoogleDrive::proccessAuth(const std::string& responce) {
    auto j = nlohmann::json::parse(responce);

    _access_token = j["access_token"].get<std::string>();

    if (j.contains("refresh_token")) {
        std::string new_refresh = j["refresh_token"].get<std::string>();
        _refresh_token = new_refresh;
    }

    int expires_in = j["expires_in"].get<int>();
    _access_token_expires = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) + expires_in;

    LOG_INFO("AUTH", "GoogleCloud with id: %i tokens updated, expires in %i", _id, expires_in);
}

std::string GoogleDrive::getDeltaToken() {
    auto handle = std::make_unique<RequestHandle>();
    handle->addHeaders("Authorization: Bearer " + _access_token);
    curl_easy_setopt(handle->_curl, CURLOPT_URL,
        "https://www.googleapis.com/drive/v3/changes/startPageToken");
    handle->setCommonCURLOpt();

    HttpClient::get().syncRequest(handle);

    auto j = nlohmann::json::parse(handle->_response);
    _page_token = j["startPageToken"].get<std::string>();

    return _page_token;
}

void GoogleDrive::getChanges() {
    auto handle = std::make_unique<RequestHandle>();

    std::string page_token = _page_token;

    std::vector<nlohmann::json> pages;

    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->setCommonCURLOpt();

    nlohmann::json changes;

    while (!page_token.empty()) {
        std::string url = "https://www.googleapis.com/drive/v3/changes?"
            "pageToken=" + page_token + "&fields=newStartPageToken%2CnextPageToken%2C"
            "changes%28file%28id%2Ctrashed%2Cname%2CmodifiedTime%2CmimeType%2Cparents%2Cmd5Checksum%29%29"
            "&includeItemsFromAllDrives=false"
            "&includeRemoved=false"
            "&restrictToMyDrive=false"
            "&supportsAllDrives=false";

        curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());

        HttpClient::get().syncRequest(handle);

        changes = nlohmann::json::parse(handle->_response);

        page_token = changes.value("nexPageToken", "");

        pages.push_back(changes);
    }

    _page_token = changes["newStartPageToken"].get<std::string>();
    LOG_INFO("GoogleDrive", "All changes recieved");

    if (!changes["changes"].empty()) {
        _events_buff.push(pages);

        _onChange();
    }
}

void GoogleDrive::setOnChange(std::function<void()> cb) {
    _onChange = std::move(cb);
}

std::vector<std::shared_ptr<Change>> GoogleDrive::proccessChanges() {
    std::vector<std::shared_ptr<Change>> changes;
    std::vector<nlohmann::json> raw_pages;

    if (!_events_buff.try_pop(raw_pages))
        return changes;

    PrevEventsRegistry old_expected(_expected_events.copyMap());

    std::unordered_map<std::string, std::filesystem::path> path_map;
    std::unordered_map<std::string, std::unique_ptr<FileRecordDTO>> maybe_new;

    for (const auto& page_json : raw_pages) {
        for (auto& jchange : page_json["changes"]) {

            if (!jchange.contains("file")) {
                LOG_WARNING("GoogleDrive", "Change without file field: %s", jchange.dump().c_str());
                continue;
            }

            auto const& file = jchange["file"];
            bool trashed = file.value("trashed", false);
            bool is_folder = (file["mimeType"] == "application/vnd.google-apps.folder");
            bool is_doc = file["mimeType"].get<std::string>().rfind("application/vnd.google-apps.", 0) == 0;

            std::string cloud_file_id = file["id"];
            EntryType   type = is_folder ? EntryType::Directory : (is_doc ? EntryType::Document : EntryType::File);
            auto        mod_time = convertCloudTime(file["modifiedTime"]);
            uint64_t    size = file.value("size", 0ULL);
            auto        name = file.value("name", std::string{});
            std::string parent_id_str = file.contains("parents")
                ? file["parents"][0].get<std::string>()
                : "";

            auto link = _db->getFileByCloudIdAndCloudFileId(_id, cloud_file_id);
            std::filesystem::path old_path, new_path, rel_path;
            std::string old_parent, new_parent, hash = file.value("md5Checksum", "");
            int global_id = link ? link->global_id : 0;

            if (!link) {
                // NEW only if not trashed and parent known
                if (trashed || parent_id_str.empty()) continue;
                // compute rel_path from DB or path_map if parent known
                auto parent_link = _db->getFileByCloudIdAndCloudFileId(_id, parent_id_str);
                if (parent_link) {
                    rel_path = _db->getPathByGlobalId(parent_link->global_id) / name;
                }
                else if (path_map.contains(parent_id_str)) {
                    rel_path = path_map[parent_id_str] / name;
                }
                else {
                    // postpone until folder appears
                    maybe_new.emplace(
                        cloud_file_id,
                        std::make_unique<FileRecordDTO>(
                            type, parent_id_str, "",
                            cloud_file_id, size, mod_time, hash, _id
                        )
                    );
                }
                if (type == EntryType::Directory)
                    path_map[cloud_file_id] = rel_path;
            }
            else {
                // EXISTING: compute old/new paths for Move/Update/Delete
                old_parent = link->cloud_parent_id;
                old_path = _db->getPathByGlobalId(global_id);
                new_parent = parent_id_str;
                new_path = old_path;
                rel_path = old_path; // default for delete

                if (!trashed) {
                    // Rename?
                    if (name != old_path.filename()) {
                        new_path = old_path.parent_path() / name;
                    }
                    // Move?
                    if (old_parent != new_parent) {
                        auto p_link = _db->getFileByCloudIdAndCloudFileId(_id, new_parent);
                        if (p_link) {
                            new_path = _db->getPathByGlobalId(p_link->global_id) / name;
                        }
                        else {
                            // moved out => treat as delete
                            trashed = true;
                        }
                    }
                    rel_path = new_path;
                }
            }

            bool need_new = !link && !trashed;
            bool need_del = link && trashed;
            bool need_move = link && !trashed && (new_path != old_path);
            bool need_update = link && !trashed
                && type != EntryType::Directory
                && mod_time > link->cloud_file_modified_time
                && hash != get<std::string>(link->cloud_hash_check_sum);

            LOG_DEBUG("GoogleDrive",
                "Eval change: old_path=\"%s\", new_path=\"%s\", new=%d, del=%d, move=%d, upd=%d",
                old_path.string().c_str(),
                new_path.string().c_str(),
                (int)need_new, (int)need_del, (int)need_move, (int)need_update);

            if (need_new) {
                if (old_expected.check(rel_path, ChangeType::New)) {
                    LOG_DEBUG("GoogleDrive", "Expected NEW: %s", jchange.dump().c_str());
                    continue;
                }
            }
            if (need_del) {
                if (old_expected.check(cloud_file_id, ChangeType::Delete)) {
                    LOG_DEBUG("GoogleDrive", "Expected DELETE: %s", jchange.dump().c_str());
                    continue;
                }
            }
            if (need_move) {
                if (old_expected.check(cloud_file_id, ChangeType::Move)) {
                    LOG_DEBUG("GoogleDrive", "Expected MOVE: %s", jchange.dump().c_str());
                    continue;
                }
            }
            if (need_update) {
                if (old_expected.check(cloud_file_id, ChangeType::Update)) {
                    LOG_DEBUG("GoogleDrive", "Expected UPDATE: %s", jchange.dump().c_str());
                    continue;
                }
            }

            if (need_move && need_update) {
                LOG_DEBUG("GoogleDrive", "  → emit MOVE+UPDATE for: \"%s\" → \"%s\"",
                    old_path.string().c_str(),
                    new_path.string().c_str());

                auto m_dto = std::make_unique<FileMovedDTO>(
                    type, global_id, _id, cloud_file_id,
                    mod_time, old_path, new_path, old_parent, new_parent
                );
                auto move_ch = ChangeFactory::makeCloudMove(std::move(m_dto));

                auto u_dto = std::make_unique<FileUpdatedDTO>(
                    type, global_id, _id, cloud_file_id,
                    hash, mod_time, new_path,
                    new_parent, size
                );
                auto upd_ch = ChangeFactory::makeCloudUpdate(std::move(u_dto));
                move_ch->addDependent(std::move(upd_ch));

                changes.push_back(std::move(move_ch));
            }
            else if (need_new) {
                LOG_DEBUG("GoogleDrive", "  → emit NEW for: \"%s\"", rel_path.string().c_str());
                auto dto = std::make_unique<FileRecordDTO>(
                    type, new_parent, rel_path, cloud_file_id,
                    size, mod_time, hash, _id
                );
                changes.push_back(ChangeFactory::makeCloudNew(std::move(dto)));
            }
            else if (need_del) {
                LOG_DEBUG("GoogleDrive", "  → emit DELETE for: \"%s\"", rel_path.string().c_str());
                auto dto = std::make_unique<FileDeletedDTO>(
                    rel_path, global_id, _id, cloud_file_id, mod_time
                );
                changes.push_back(ChangeFactory::makeDelete(std::move(dto)));
            }
            else if (need_move) {
                LOG_DEBUG("GoogleDrive", "  → emit MOVE for: \"%s\" → \"%s\"",
                    old_path.string().c_str(),
                    new_path.string().c_str());
                auto dto = std::make_unique<FileMovedDTO>(
                    type, global_id, _id, cloud_file_id,
                    mod_time, old_path, new_path, old_parent, new_parent
                );
                changes.push_back(ChangeFactory::makeCloudMove(std::move(dto)));
            }
            else if (need_update) {
                LOG_DEBUG("GoogleDrive", "  → emit UPDATE for: \"%s\"", rel_path.string().c_str());
                auto dto = std::make_unique<FileUpdatedDTO>(
                    type, global_id, _id,
                    cloud_file_id, hash, mod_time,
                    new_path, new_parent, size
                );
                changes.push_back(ChangeFactory::makeCloudUpdate(std::move(dto)));
            }
        }
    }

    // TODO : check maybe_new if there is new folder with that file/folder
    // TODO : same logic with move: if move to a new dir, we need to check it and not delete

    return changes;
}

bool GoogleDrive::hasChanges() const {
    return !_events_buff.empty();
}

void GoogleDrive::proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const {
    auto json_rsp = nlohmann::json::parse(response);
    dto->cloud_file_modified_time = convertCloudTime(json_rsp["modifiedTime"]);
    dto->cloud_id = _id;
    dto->cloud_parent_id = json_rsp["parents"][0];
    dto->cloud_file_id = json_rsp["id"];
    if (json_rsp.contains("size")) {
        dto->size = std::stoull(json_rsp["size"].get<std::string>());
    }
    if (json_rsp.contains("md5Checksum")) {
        dto->cloud_hash_check_sum = json_rsp["md5Checksum"].get<std::string>();
    }
}

void GoogleDrive::proccesMove(std::unique_ptr<FileMovedDTO>& dto, const std::string& response) const {
    auto json_rsp = nlohmann::json::parse(response);
    dto->cloud_file_modified_time = convertCloudTime(json_rsp["modifiedTime"]);
    dto->cloud_id = _id;
    dto->new_cloud_parent_id = json_rsp["parents"][0];
}

void GoogleDrive::proccesUpdate(std::unique_ptr<FileUpdatedDTO>& dto, const std::string& response) const {
    auto json_rsp = nlohmann::json::parse(response);
    dto->cloud_file_modified_time = convertCloudTime(json_rsp["modifiedTime"]);
    dto->cloud_hash_check_sum = json_rsp.value("md5Checksum", std::string{});
    if (json_rsp.contains("md5Checksum")) {
        dto->cloud_hash_check_sum = json_rsp["md5Checksum"].get<std::string>();
    }
    if (json_rsp.contains("size")) {
        dto->size = std::stoull(json_rsp["size"].get<std::string>());
    }
}
void GoogleDrive::proccesDelete(std::unique_ptr<FileDeletedDTO>& dto, const std::string& response) const {}

void GoogleDrive::proccesDownload(std::unique_ptr<FileUpdatedDTO>& dto, const std::string& response) const {}


int GoogleDrive::id() const {
    return _id;
}

bool GoogleDrive::ignoreTmp(const std::string& name) {
    constexpr std::string_view tmp_prefix{ ".-tmp-cloudsync-" };
    if (name.size() >= tmp_prefix.size()
        && std::string_view(name).starts_with(tmp_prefix)) {
        return true;
    }
    return false;
}

std::string GoogleDrive::getHomeDir() const {
    return _home_dir_id;
}