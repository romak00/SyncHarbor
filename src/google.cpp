#include "google.h"
#include "Networking.h"
#include "logger.h"

inline static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

inline static size_t write_data(void* ptr, size_t size, size_t nmemb, void* userdata) {
    std::ofstream* ofs = static_cast<std::ofstream*>(userdata);
    ofs->write(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

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
    //_refresh_token(refresh_token),
    _local_home_path(local_home_dir),
    _db(db_conn),
    _id(cloud_id),
    _home_path(home_dir)
{
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
    //_refresh_token(refresh_token),
    _local_home_path(local_home_dir),
    _page_token(start_page_token),
    _db(db_conn),
    _id(cloud_id),
    _home_path(home_dir)
{
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

void GoogleDrive::setupUpdateHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileModifiedDTO>& dto) const {}

void GoogleDrive::setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) const {}

std::vector<std::unique_ptr<FileRecordDTO>> GoogleDrive::initialFiles() {
    return {};
}

std::filesystem::path GoogleDrive::normalizePath(const std::filesystem::path& p) {
    std::string s = p.generic_string();

    if (s == "/") {
        return std::filesystem::path{};
    }

    if (!s.empty() && s.front() == '/') {
        s.erase(0, 1);
    }

    return std::filesystem::path{s};
}

void GoogleDrive::ensureRootExists() {
    auto handle = std::make_unique<RequestHandle>();

    handle->addHeaders("Authorization: Bearer " + _access_token);
    curl_easy_setopt(handle->_curl, CURLOPT_HTTPGET, 1L);
    handle->setCommonCURLOpt();

    auto path = this->normalizePath(_home_path);

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
    if (!handle->_curl) {
        throw std::runtime_error("Error initializing curl Google::UploadOperation");
    }

    nlohmann::json j;
    j["name"] = dto->rel_path.filename().string();
    j["parents"] = { _home_dir_id };
    handle->addHeaders("Authorization: Bearer " + _access_token);

    if (dto->type == EntryType::File) {
        const std::string metadata = j.dump();
        handle->_mime = curl_mime_init(handle->_curl);
        curl_mimepart* part = curl_mime_addpart(handle->_mime);
        curl_mime_name(part, "metadata");
        curl_mime_data(part, metadata.c_str(), CURL_ZERO_TERMINATED);
        curl_mime_type(part, "application/json; charset=UTF-8");
        part = curl_mime_addpart(handle->_mime);
        curl_mime_name(part, "media");
        curl_mime_filedata(part, (_local_home_path.string() + "/" + dto->rel_path.string()).c_str());
        curl_mime_type(part, "application/octet-stream");

        curl_easy_setopt(handle->_curl, CURLOPT_MIMEPOST, handle->_mime);
        curl_easy_setopt(handle->_curl, CURLOPT_URL, "https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart&fields=id,modifiedTime,md5Checksum,parents");
    }
    else {
        j["mimeType"] = "application/vnd.google-apps.folder";
        const std::string metadata = j.dump();

        curl_easy_setopt(handle->_curl, CURLOPT_URL, "https://www.googleapis.com/drive/v3/files?fields=id,modifiedTime,md5Checksum,parents");
        curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);
        curl_easy_setopt(handle->_curl, CURLOPT_COPYPOSTFIELDS, metadata.c_str());
    }
    handle->setCommonCURLOpt();

    _expected_events.add(dto->rel_path, ChangeType::New);
}

void GoogleDrive::setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const {
    std::string url = "https://www.googleapis.com/drive/v3/files/" + dto->cloud_file_id + "?alt=media";

    handle->addHeaders("Authorization: Bearer " + _access_token);

    auto path = _local_home_path / dto->rel_path;

    handle->setFileStream(path, std::ios::out);

    curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());

    handle->setCommonCURLOpt();
}

void GoogleDrive::setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileModifiedDTO>& dto) const {
    std::string url = "https://www.googleapis.com/drive/v3/files/" + dto->cloud_file_id + "?alt=media";

    handle->addHeaders("Authorization: Bearer " + _access_token);

    auto path = _local_home_path / dto->new_rel_path.parent_path() / (".-tmp-cloudsync-" + dto->new_rel_path.filename().string());

    handle->setFileStream(path, std::ios::out);

    curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());

    handle->setCommonCURLOpt();
}


/* std::unique_ptr<CurlEasyHandle> GoogleDrive::create_file_delete_handle(const std::string& id) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();
    if (!easy_handle->_curl) {
        throw std::runtime_error("Error initializing curl create_file_delete_handle");
    }

    const std::string url = "https://www.googleapis.com/drive/v3/files/" + id;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    easy_handle->_headers = headers;

    easy_handle->type = "delete";

    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");

    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTPHEADER, easy_handle->_headers);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(easy_handle->_curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_FOLLOWLOCATION, 1L);

    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEDATA, &(easy_handle->_response));

    return std::move(easy_handle);
} */

/* std::unique_ptr<CurlEasyHandle> GoogleDrive::create_file_delete_handle(const std::string& id) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();
    if (!easy_handle->_curl) {
        throw std::runtime_error("Error initializing curl create_file_delete_handle");
    }

    const std::string url = "https://www.googleapis.com/drive/v3/files/" + id;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    easy_handle->_headers = headers;

    easy_handle->type = "delete";

    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");

    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTPHEADER, easy_handle->_headers);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(easy_handle->_curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_FOLLOWLOCATION, 1L);

    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEDATA, &(easy_handle->_response));

    return std::move(easy_handle);
}

std::unique_ptr<CurlEasyHandle> GoogleDrive::create_name_update_handle(const std::string& id, const std::string& name) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();
    if (!easy_handle->_curl) {
        throw std::runtime_error("Error initializing curl create_metadata_update_handle");
    }

    std::string url = "https://www.googleapis.com/drive/v3/files/" + id;

    nlohmann::json j;
    j["name"] = name;
    std::string data = j.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=UTF-8");
    easy_handle->_headers = headers;

    easy_handle->type = "metadata_update";

    curl_easy_setopt(easy_handle->_curl, CURLOPT_COPYPOSTFIELDS, data.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTPHEADER, easy_handle->_headers);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(easy_handle->_curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_FOLLOWLOCATION, 1L);

    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEDATA, &(easy_handle->_response));

    return std::move(easy_handle);
}

std::unique_ptr<CurlEasyHandle> GoogleDrive::create_parent_update_handle(const std::string& id, const std::string& parent, const std::string& parent_to_remove) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();
    if (!easy_handle->_curl) {
        throw std::runtime_error("Error initializing curl create_metadata_update_handle");
    }

    std::string parent_id, remove_parent_id = "root";
    if (parent_to_remove == "") {
        parent_id = _dir_id_map[parent];
    }
    else {
        parent_id = parent;
        remove_parent_id = parent_to_remove;
    }

    std::string url = "https://www.googleapis.com/drive/v3/files/" + id +
        "?addParents=" + parent_id +
        "&removeParents=" + remove_parent_id;


    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    easy_handle->_headers = headers;

    easy_handle->type = "metadata_update";

    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTPHEADER, easy_handle->_headers);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(easy_handle->_curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_FOLLOWLOCATION, 1L);

    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEDATA, &(easy_handle->_response));

    return std::move(easy_handle);
} */

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

void GoogleDrive::setDelta(const std::string& response) {
    auto j = nlohmann::json::parse(response);
    _page_token = j["startPageToken"];
}

void GoogleDrive::getDelta(const std::unique_ptr<RequestHandle>& handle) {
    handle->addHeaders("Authorization: Bearer " + _access_token);
    curl_easy_setopt(handle->_curl, CURLOPT_URL,
        "https://www.googleapis.com/drive/v3/changes/startPageToken");
    handle->setCommonCURLOpt();
}

void GoogleDrive::getChanges(const std::unique_ptr<RequestHandle>& handle) {
    std::string url = "https://www.googleapis.com/drive/v3/changes?"
        "pageToken=" + _page_token + "&fields=newStartPageToken%2CnextPageToken%2C"
        "changes%28file%28id%2Ctrashed%2Cname%2CmodifiedTime%2CmimeType%2Cparents%2Cmd5Checksum%29%29"
        "&includeItemsFromAllDrives=false"
        "&includeRemoved=false"
        "&restrictToMyDrive=false"
        "&supportsAllDrives=false";

    curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());
    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->setCommonCURLOpt();
}

void GoogleDrive::setRawSignal(std::shared_ptr<RawSignal> raw_signal) {
    _raw_signal = std::move(raw_signal);
}

std::vector<std::unique_ptr<Change>> GoogleDrive::proccessChanges() {
    std::vector<std::unique_ptr<Change>> changes;
    std::vector<std::string> raw_pages;

    if (_events_buff.try_pop(raw_pages)) {
        std::unordered_map<std::string, std::filesystem::path> path_map;
        std::unordered_map<std::string, std::unique_ptr<FileRecordDTO>> maybe_new;
        for (const auto& raw_page : raw_pages) {
            auto page = nlohmann::json::parse(raw_page);
            for (auto& change : page) {
                if (!change.contains("file")) {
                    LOG_WARNING("GoogleDrive", "Change without file field: %s", change.dump().c_str());
                    continue;
                }
                auto const& file = change["file"];
                bool trashed = file.value("trashed", false);
                bool is_folder = (file["mimeType"] == "application/vnd.google-apps.folder");
                std::string cloud_file_id = file["id"];
                EntryType type = is_folder ? EntryType::Directory : EntryType::File;
                std::time_t cloud_file_modified_time = convertCloudTime(file["modifiedTime"]);
                uint64_t size = file.value("size", 0ULL);
                std::string name = file.value("name", std::string{});

                if (ignoreTmp(name)) {
                    LOG_DEBUG("GoogleDrive", "Ignoring tmp: %s", change.dump().c_str());
                    continue;
                }

                std::string cloud_parent_id = file.contains("parents") ? file["parents"][0].get<std::string>() : "";

                auto file_link = _db->getFileByCloudIdAndCloudFileId(_id, cloud_file_id);
                ChangeTypeFlags flags{ ChangeType::Null };
                int global_id = 0;
                std::filesystem::path old_rel_path, new_rel_path;
                std::string old_cloud_parent_id, new_cloud_parent_id;
                std::string cloud_hash_check_sum = file.value("md5Checksum", std::string{});
                std::filesystem::path rel_path;

                if (file_link == nullptr) {
                    // NEW
                    if (!trashed && !cloud_parent_id.empty()) {
                        auto parent_link = _db->getFileByCloudIdAndCloudFileId(_id, cloud_parent_id);
                        if (parent_link != nullptr) {
                            int parent_global_id = parent_link->global_id;
                            auto parent_path = _db->getPathByGlobalId(parent_global_id);
                            rel_path = parent_path / name;

                            if (_expected_events.check(rel_path, ChangeType::New)) {
                                LOG_DEBUG("GoogleDrive", "Expected change: %s", change.dump().c_str());
                            }

                            flags.add(ChangeType::New);
                            if (type == EntryType::Directory) {
                                path_map.emplace(
                                    cloud_file_id,
                                    rel_path
                                );
                            }
                        }
                        else {
                            if (path_map.contains(cloud_parent_id)) {
                                auto parent_path = path_map[cloud_parent_id];
                                rel_path = parent_path / name;

                                if (_expected_events.check(rel_path, ChangeType::New)) {
                                    LOG_DEBUG("GoogleDrive", "Expected NEW: %s", change.dump().c_str());
                                    continue;
                                }

                                flags.add(ChangeType::New);
                                if (type == EntryType::Directory) {
                                    path_map.emplace(
                                        cloud_file_id,
                                        rel_path
                                    );
                                }
                            }
                            else {
                                maybe_new.emplace(
                                    cloud_file_id,
                                    std::make_unique<FileRecordDTO>(
                                        type,
                                        cloud_parent_id,
                                        "",
                                        cloud_file_id,
                                        size,
                                        cloud_file_modified_time,
                                        cloud_hash_check_sum,
                                        _id
                                    )
                                );
                            }
                        }
                    }
                    // Certainly not our file
                    else continue;
                }
                else {
                    // Not NEW, we have db record
                    global_id = file_link->global_id;
                    // Deletion
                    if (trashed) {
                        if (_expected_events.check(cloud_file_id, ChangeType::Delete)) {
                            LOG_DEBUG("GoogleDrive", "Expected DELETE: %s", change.dump().c_str());
                            continue;
                        }
                        flags.add(ChangeType::Delete);
                    }
                    else {
                        old_cloud_parent_id = file_link->cloud_parent_id;
                        new_cloud_parent_id = cloud_parent_id;
                        old_rel_path = _db->getPathByGlobalId(global_id);
                        new_rel_path = old_rel_path;

                        if (_expected_events.check(cloud_file_id, ChangeType::Move)) {
                            LOG_DEBUG("GoogleDrive", "Expected MOVE: %s", change.dump().c_str());
                            continue;
                        }

                        // Rename
                        if (name != old_rel_path.filename()) {
                            flags.add(ChangeType::Rename);
                            new_rel_path = old_rel_path.parent_path() / name;
                        }
                        // Move
                        if (old_cloud_parent_id != new_cloud_parent_id) {
                            auto parent_link = _db->getFileByCloudIdAndCloudFileId(_id, new_cloud_parent_id);
                            if (parent_link != nullptr) {
                                flags.add(ChangeType::Move);
                                auto parent_path = _db->getPathByGlobalId(parent_link->global_id);
                                new_rel_path = parent_path / name;
                            }
                            // Move out => Deleteion
                            else {
                                flags.add(ChangeType::Delete);
                            }
                        }
                        // Update
                        if (type != EntryType::Directory) {
                            auto stored_cloud_file_modified_time = file_link->cloud_file_modified_time;
                            auto stored_cloud_hash_check_sum = get<std::string>(file_link->cloud_hash_check_sum);
                            if (cloud_file_modified_time > stored_cloud_file_modified_time
                                && size != file_link->size
                                && cloud_hash_check_sum != stored_cloud_hash_check_sum)
                            {
                                flags.add(ChangeType::Update);
                            }
                        }
                    }
                }
                std::unique_ptr<ICommand> first_cmd = nullptr;
                if (flags.contains(ChangeType::New)) {
                    first_cmd = std::make_unique<CloudDownloadCommand>(_id);
                    first_cmd->setDTO(
                        std::make_unique<FileRecordDTO>(
                            type,
                            cloud_parent_id,
                            rel_path,
                            cloud_file_id,
                            size,
                            cloud_file_modified_time,
                            cloud_hash_check_sum,
                            _id
                        )
                    );
                }
                else if (flags.contains(ChangeType::Delete)) {
                    _pending_deletes.emplace(cloud_file_id,
                        std::make_unique<FileDeletedDTO>(
                            rel_path,
                            global_id,
                            _id,
                            cloud_file_id,
                            cloud_file_modified_time
                        )
                    );
                }
                else if (flags.contains(ChangeType::Rename) || flags.contains(ChangeType::Move) || flags.contains(ChangeType::Update)) {
                    if (flags.contains(ChangeType::Update)) {
                        first_cmd = std::make_unique<CloudDownloadCommand>(_id);
                        first_cmd->setDTO(
                            std::make_unique<FileModifiedDTO>(
                                type,
                                flags,
                                global_id,
                                _id,
                                cloud_file_id,
                                cloud_hash_check_sum,
                                cloud_file_modified_time,
                                old_rel_path,
                                new_rel_path,
                                old_cloud_parent_id,
                                new_cloud_parent_id,
                                size
                            )
                        );
                    }
                    else {
                        first_cmd = std::make_unique<LocalUpdateCommand>(0);
                        first_cmd->setDTO(
                            std::make_unique<FileModifiedDTO>(
                                type,
                                flags,
                                global_id,
                                _id,
                                cloud_file_id,
                                cloud_hash_check_sum,
                                cloud_file_modified_time,
                                old_rel_path,
                                new_rel_path,
                                old_cloud_parent_id,
                                new_cloud_parent_id,
                                size
                            )
                        );
                    }
                }

                if (first_cmd != nullptr) {
                    changes.emplace_back(
                        std::make_unique<Change>(
                            flags,
                            cloud_file_modified_time,
                            std::move(first_cmd)
                        )
                    );
                }

            }
        }
        // TODO : check maybe_new if there is new folder with that file/folder
    }
    return changes;
}

bool GoogleDrive::hasChanges() const {
    return !_events_buff.empty();
}

/* 

std::unique_ptr<CurlEasyHandle> GoogleDrive::create_file_update_handle(const std::string& id, const std::filesystem::path& path, const std::string& name) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();
    if (!easy_handle->_curl) {
        throw std::runtime_error("Error initializing curl create_file_update_handle");
    }
    std::string url = "https://www.googleapis.com/upload/drive/v3/files/" + id + "?uploadType=multipart";

    nlohmann::json j;
    j["name"] = name;
    std::string metadata = j.dump();

    easy_handle->_mime = curl_mime_init(easy_handle->_curl);
    curl_mimepart* part = curl_mime_addpart(easy_handle->_mime);
    curl_mime_name(part, "metadata");
    curl_mime_data(part, metadata.c_str(), CURL_ZERO_TERMINATED);
    curl_mime_type(part, "application/json; charset=UTF-8");

    easy_handle->type = "file_update";

    part = curl_mime_addpart(easy_handle->_mime);
    curl_mime_name(part, "media");
    curl_mime_filedata(part, path.c_str());
    curl_mime_type(part, "application/octet-stream");


    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    curl_easy_setopt(easy_handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_MIMEPOST, easy_handle->_mime);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_BUFFERSIZE, 131072L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    easy_handle->_headers = headers;
    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTPHEADER, easy_handle->_headers);

    curl_easy_setopt(easy_handle->_curl, CURLOPT_VERBOSE, 0L);

    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEDATA, &(easy_handle->_response));

    return std::move(easy_handle);
} */

void GoogleDrive::proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const {
    auto json_rsp = nlohmann::json::parse(response);
    dto->cloud_file_modified_time = convertCloudTime(json_rsp["modifiedTime"]);
    dto->cloud_id = _id;
    dto->cloud_parent_id = json_rsp["parents"][0];
    dto->cloud_file_id = json_rsp["id"];
    if (json_rsp.contains("md5Checksum")) {
        dto->cloud_hash_check_sum = json_rsp["md5Checksum"].get<std::string>();
    }
}

void GoogleDrive::proccesUpdate(std::unique_ptr<FileModifiedDTO>& dto, const std::string& response) const {}
void GoogleDrive::proccesDownload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const {}
void GoogleDrive::proccesDelete(std::unique_ptr<FileDeletedDTO>& dto, const std::string& response) const {}


int GoogleDrive::id() const {
    return _id;
}

std::vector<std::unique_ptr<Change>> GoogleDrive::flushOldDeletes() {
    LOG_DEBUG("GoogleDrive", "Flushing Deletes");
    std::vector<std::unique_ptr<Change>> changes;
    std::lock_guard<std::mutex> lk(_cleanup_mtx);
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto it = _pending_deletes.begin();
    while (it != _pending_deletes.end()) {
        if (_expected_events.check(it->first, ChangeType::Delete)) {
            LOG_DEBUG("LocalStorage", "Expected DELETE: %s", it->second->rel_path.string());
            it = _pending_deletes.erase(it);
            continue;
        }
        if ((now - it->second->when) > _UNDO_INTERVAL) {
            LOG_DEBUG("LocalStorage", "Event: TRUE DELETE: %s", it->second->rel_path.string());
            auto first_cmd = std::make_unique<CloudDeleteCommand>(_id);
            first_cmd->setDTO(std::move(it->second));
            changes.emplace_back(
                std::make_unique<Change>(
                    ChangeType::Delete,
                    now,
                    std::move(first_cmd)
                )
            );

            it = _pending_deletes.erase(it);
        }
        else {
            ++it;
        }
    }
    _cleanup_cv.notify_all();
    return changes;
}

bool GoogleDrive::ignoreTmp(const std::string& name) {
    constexpr std::string_view tmp_prefix{ ".-tmp-cloudsync-" };
    if (name.size() >= tmp_prefix.size()
        && std::string_view(name).starts_with(tmp_prefix)) {
        return true;
    }
    return false;
}

bool GoogleDrive::handleChangesResponse(const std::unique_ptr<RequestHandle>& handle, std::vector<std::string>& pages) {
    auto changes = nlohmann::json::parse(handle->_response);
    if (changes.contains("nextPageToken")) {
        LOG_INFO("GoogleDrive", "Not all changes recieved, another request");
        std::string page_token = changes["nextPageToken"];
        std::string url = "https://www.googleapis.com/drive/v3/changes?"
            "pageToken=" + page_token + "&fields=newStartPageToken%2CnextPageToken%2C"
            "changes%28file%28id%2Ctrashed%2Cname%2CmodifiedTime%2CmimeType%2Cparents%29%29"
            "&includeItemsFromAllDrives=false"
            "&includeRemoved=false"
            "&restrictToMyDrive=false"
            "&supportsAllDrives=false";
        curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());
        return true;
    }
    else {
        LOG_INFO("GoogleDrive", "All changes recieved");
        _page_token = changes["newStartPageToken"];
        pages.push_back(handle->_response);
        _events_buff.push(pages);
        _raw_signal->cv.notify_one();
        return false;
    }
}

std::string GoogleDrive::getHomeDir() const {
    return _home_dir_id;
}