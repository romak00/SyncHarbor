#include "dropbox.h"
#include "logger.h"
#include "Networking.h"

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

inline static size_t ReadCallback(void* ptr, size_t size, size_t nmemb, void* stream) {
    std::ifstream* file = static_cast<std::ifstream*>(stream);
    file->read(static_cast<char*>(ptr), size * nmemb);
    return file->gcount();
}

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
    //_refresh_token(refresh_token),
    _home_path(home_path),
    _local_home_path(local_home_path),
    _db(db_conn),
    _id(cloud_id)
{
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
    //_refresh_token(refresh_token),
    _page_token(start_page_token),
    _home_path(home_path),
    _local_home_path(local_home_path),
    _db(db_conn),
    _id(cloud_id)
{
}

void Dropbox::setupUploadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const {
    if (!handle->_curl) {
        throw std::runtime_error("Error initializing curl Google::UploadOperation");
    }

    nlohmann::json j;
    j["path"] = _home_path.string() + "/" + dto->rel_path.string();
    handle->addHeaders("Authorization: Bearer " + _access_token);

    if (dto->type == EntryType::File) {
        j["mode"] = "overwrite";
        handle->setFileStream((_local_home_path.string() + "/" + dto->rel_path.string()), std::ios::in);
        std::string dropbox_api_arg = j.dump();
        handle->addHeaders("Dropbox-API-Arg: " + dropbox_api_arg);
        handle->addHeaders("Content-Type: application/octet-stream");
        curl_easy_setopt(handle->_curl, CURLOPT_URL, "https://content.dropboxapi.com/2/files/upload");
    }
    else {
        j["autorename"] = false;
        std::string body = j.dump();
        handle->addHeaders("Content-Type: application/json");
        curl_easy_setopt(handle->_curl, CURLOPT_URL, "https://api.dropboxapi.com/2/files/create_folder_v2");
        curl_easy_setopt(handle->_curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(handle->_curl, CURLOPT_POSTFIELDSIZE, body.size());
    }
    curl_easy_setopt(handle->_curl, CURLOPT_POST, 1L);

    handle->setCommonCURLOpt();
}

void Dropbox::setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const {
    std::string url = "https://content.dropboxapi.com/2/files/download";

    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->addHeaders("Dropbox-API-Arg: {\"path\": \"" + (_home_path / dto->rel_path).string() + "\"}");

    auto path = _local_home_path / dto->rel_path;

    handle->setFileStream(path, std::ios::out);

    curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());
    handle->setCommonCURLOpt();
}
void Dropbox::setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileModifiedDTO>& dto) const {
    std::string url = "https://content.dropboxapi.com/2/files/download";

    handle->addHeaders("Authorization: Bearer " + _access_token);
    handle->addHeaders("Dropbox-API-Arg: {\"path\": \"" + (_home_path / dto->new_rel_path).string() + "\"}");

    auto path = _local_home_path / dto->new_rel_path.parent_path() / (".-tmp-cloudsync-" + dto->new_rel_path.filename().string());

    handle->setFileStream(path, std::ios::out);

    curl_easy_setopt(handle->_curl, CURLOPT_URL, url.c_str());
    handle->setCommonCURLOpt();
}

bool Dropbox::isRealTime() const {
    return true;
}

void Dropbox::setupUpdateHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileModifiedDTO>& dto) const {}
void Dropbox::setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) const {}

std::vector<std::unique_ptr<Change>> Dropbox::initialFiles() { return {}; }

int Dropbox::id() const {
    return _id;
}

bool Dropbox::hasChanges() const {
    return !_events_buff.empty();
}

void Dropbox::setRawSignal(std::shared_ptr<RawSignal> raw_signal) {
    _raw_signal = std::move(raw_signal);
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

void Dropbox::proccesUpdate(std::unique_ptr<FileModifiedDTO>& dto, const std::string& response) const {}
void Dropbox::proccesDownload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const {}
void Dropbox::proccesDelete(std::unique_ptr<FileDeletedDTO>& dto, const std::string& response) const {}


/* std::unique_ptr<CurlEasyHandle> Dropbox::create_file_upload_handle(const std::filesystem::path& path, const std::string& parent) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();

    std::string url = "https://content.dropboxapi.com/2/files/upload";

    nlohmann::json j;
    j["path"] = _home_dir_id + "/" + parent + "/" + path.filename().string();
    j["mode"] = "overwrite";

    std::string dropbox_api_arg = j.dump();

    easy_handle->type = "file_upload";

    easy_handle->_ifc = std::ifstream(path, std::ios::binary);
    if (easy_handle->_ifc && easy_handle->_ifc.is_open()) {
        curl_easy_setopt(easy_handle->_curl, CURLOPT_READDATA, &easy_handle->_ifc);
    }
    else {
        throw std::runtime_error("Error opening file for upload create_file_download_handle: " + path.string());
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, ("Dropbox-API-Arg: " + dropbox_api_arg).c_str());
    easy_handle->_headers = headers;

    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    curl_easy_setopt(easy_handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTPHEADER, easy_handle->_headers);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_POST, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_READFUNCTION, ReadCallback);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEDATA, &(easy_handle->_response));
    curl_easy_setopt(easy_handle->_curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_FOLLOWLOCATION, 1L);

    return std::move(easy_handle);
}

std::unique_ptr<CurlEasyHandle> Dropbox::create_dir_upload_handle(const std::filesystem::path& path, const std::string& parent) {
    std::unique_ptr<CurlEasyHandle> easy_handle;
    return std::move(easy_handle);
}

std::unique_ptr<CurlEasyHandle> Dropbox::create_file_update_handle(const std::string& id, const std::filesystem::path& path, const std::string& name) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();
    if (!easy_handle->_curl) {
        throw std::runtime_error("Error initializing curl create_file_update_handle");
    }

    std::string url = "https://content.dropboxapi.com/2/files/upload";
    std::string dropbox_api_arg = "{\"path\": \"" + id + "\", \"mode\": \"overwrite\", \"autorename\": false, \"mute\": false}";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, ("Dropbox-API-Arg: " + dropbox_api_arg).c_str());
    easy_handle->_headers = headers;

    curl_easy_setopt(easy_handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTPHEADER, easy_handle->_headers);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_POST, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_READDATA, fopen(path.c_str(), "rb"));
    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEDATA, &(easy_handle->_response));

    return std::move(easy_handle);
}

std::unique_ptr<CurlEasyHandle> Dropbox::create_file_delete_handle(const std::string& id) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();
    if (!easy_handle->_curl) {
        throw std::runtime_error("Error initializing curl create_file_delete_handle");
    }

    std::string url = "https://api.dropboxapi.com/2/files/delete_v2";
    std::string post_data = "{\"path\": \"" + id + "\"}";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    easy_handle->_headers = headers;

    curl_easy_setopt(easy_handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTPHEADER, easy_handle->_headers);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_POST, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEDATA, &(easy_handle->_response));

    return std::move(easy_handle);
}

std::unique_ptr<CurlEasyHandle> Dropbox::create_name_update_handle(const std::string& id, const std::string& name) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();
    if (!easy_handle->_curl) {
        throw std::runtime_error("Error initializing curl create_name_update_handle");
    }
    std::string to = std::filesystem::path(id).parent_path().string() + "/" + name;

    std::string url = "https://api.dropboxapi.com/2/files/move_v2";
    nlohmann::json j;
    j["from_path"] = id;
    j["to_path"] = to;

    std::string post_fields = j.dump();
    easy_handle->type = "dropbox parent update";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    easy_handle->_headers = headers;

    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    curl_easy_setopt(easy_handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTPHEADER, easy_handle->_headers);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_POST, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_FOLLOWLOCATION, 1L);

    curl_easy_setopt(easy_handle->_curl, CURLOPT_COPYPOSTFIELDS, post_fields.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEDATA, &(easy_handle->_response));

    return std::move(easy_handle);
}

std::unique_ptr<CurlEasyHandle> Dropbox::create_parent_update_handle(const std::string& id, const std::string& parent, const std::string& parent_to_remove) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();
    if (!easy_handle->_curl) {
        throw std::runtime_error("Error initializing curl create_parent_update_handle");
    }
    std::string to = parent, from = id;
    if (!_dir_id_map.empty()) {
        to = _dir_id_map[parent] + "/" + std::filesystem::path(id).filename().string();
        from = _home_dir_id + "/" + std::filesystem::path(id).filename().string();
    }
    std::cout << "from: " << from << " to: " << to << '\n';
    std::string url = "https://api.dropboxapi.com/2/files/move_v2";
    nlohmann::json j;
    j["from_path"] = from;
    j["to_path"] = to;

    std::string post_fields = j.dump();
    easy_handle->type = "dropbox parent update";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    easy_handle->_headers = headers;

    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    curl_easy_setopt(easy_handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTPHEADER, easy_handle->_headers);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_POST, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_FOLLOWLOCATION, 1L);

    curl_easy_setopt(easy_handle->_curl, CURLOPT_COPYPOSTFIELDS, post_fields.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEDATA, &(easy_handle->_response));

    return std::move(easy_handle);
} */

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

void Dropbox::getDelta(const std::unique_ptr<RequestHandle>& handle) {
    if (!handle->_curl) {
        throw std::runtime_error("Error initializing curl Dropbox::getStartPageToken");
    }

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
}

void Dropbox::setDelta(const std::string& response) {
    auto j = nlohmann::json::parse(response);
    _page_token = j["cursor"];
}

std::string Dropbox::getHomeDir() const {
    return _home_path;
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