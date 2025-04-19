#include "dropbox.h"

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
    const int cloud_id
    )
    :
    _client_id(client_id),
    _client_secret(client_secret),
    _refresh_token(refresh_token),
    _home_path(home_path),
    _local_home_path(local_home_path),
    _id(cloud_id)
{
    //_refresh_token = Dropbox::first_time_auth();
    _access_token = Dropbox::get_access_token();
}
Dropbox::Dropbox(
    const std::string& client_id,
    const std::string& client_secret,
    const std::string& refresh_token,
    const std::filesystem::path& home_path,
    const std::filesystem::path& local_home_path,
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
    _id(cloud_id)
{

    _access_token = Dropbox::get_access_token();
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

void Dropbox::setupUpdateHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileModifiedDTO>& dto) const {}
void Dropbox::setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const {}
void Dropbox::setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) const {}

std::vector<std::unique_ptr<ChangeDTO>> Dropbox::scanForChanges(std::shared_ptr<Database> db_conn) { return{}; }
std::vector<std::unique_ptr<ChangeDTO>> Dropbox::initialFiles() { return {}; }

const int Dropbox::id() const {
    return _id;
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

std::unique_ptr<CurlEasyHandle> Dropbox::create_file_download_handle(const std::string& id, const std::filesystem::path& path) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();
    if (!easy_handle->_curl) {
        throw std::runtime_error("Error initializing curl create_file_download_handle");
    }

    std::string url = "https://content.dropboxapi.com/2/files/download";
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    headers = curl_slist_append(headers, ("Dropbox-API-Arg: {\"path\": \"" + id + "\"}").c_str());
    easy_handle->_headers = headers;
    easy_handle->type = "file_download";

    easy_handle->_ofc = std::ofstream(path.parent_path().string() + "-tmp-copy-cloudsync-" + path.filename().string(), std::ios::binary);
    if (easy_handle->_ofc && easy_handle->_ofc.is_open()) {
        curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEDATA, &easy_handle->_ofc);
    }
    else {
        throw std::runtime_error("Error opening file for download create_file_download_handle: " + path.string());
    }

    curl_easy_setopt(easy_handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTPHEADER, easy_handle->_headers);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEFUNCTION, write_data);

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

std::string Dropbox::first_time_auth() {
    Dropbox::generate_auth_url();
    std::string auth_code = "";
    std::cin >> auth_code;
    std::string refresh_token = Dropbox::get_refresh_token(auth_code);

    return refresh_token;
}

void Dropbox::generate_auth_url() {
    std::string url = "https://www.dropbox.com/oauth2/authorize?"
        "client_id=" + _client_id +
        "&fromDws=True&redirect_uri=http%3A%2F%2Flocalhost%3A8080%2F&response_type=code&token_access_type=offline";

    std::cout << url << std::endl;
}

std::string Dropbox::get_refresh_token(const std::string& auth_code) {
    CURL* curl = curl_easy_init();
    std::string response;
    if (!curl) {
        throw std::runtime_error("Error initialising curl Dropbox::get_refresh_token");
    }

    std::string post_fields = "code=" + auth_code +
        "&grant_type=authorization_code" +
        "&client_id=" + _client_id +
        "&client_secret=" + _client_secret +
        "&redirect_uri=http://localhost:8080/";

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.dropbox.com/oauth2/token");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        throw std::runtime_error("Error getting refresh token GoogleDrive get_refresh_token");
    }
    curl_easy_cleanup(curl);

    auto json_response = nlohmann::json::parse(response);
    std::string refresh_token = json_response["refresh_token"];

    return refresh_token;
}

std::string Dropbox::get_access_token() {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Error initialising curl Dropbox::get_access_token");
    }

    std::string url = "https://api.dropbox.com/oauth2/token";
    std::string post_fields = "grant_type=refresh_token&refresh_token=" + _refresh_token +
        "&client_id=" + _client_id +
        "&client_secret=" + _client_secret;

    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields.c_str());


    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        throw std::runtime_error("Error getting access token Dropbox::get_access_token");
    }

    curl_easy_cleanup(curl);

    auto json_response = nlohmann::json::parse(response);
    std::string access_token = json_response["access_token"];

    return access_token;

}

void Dropbox::get_start_page_token() {
    CURL* curl = curl_easy_init();
    std::string response;
    std::string url = "https://www.googleapis.com/drive/v3/changes/startPageToken";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    const auto parsed_json = nlohmann::json::parse(response);
    _page_token = parsed_json["startPageToken"];

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

void Dropbox::proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const {
    auto json_response = nlohmann::json::parse(response);
    if (dto->type == EntryType::Directory) {
        dto->cloud_file_id = json_response["metadata"]["id"];
    }
    else {
        dto->cloud_file_id = json_response["id"];
        dto->cloud_file_modified_time = convert_cloud_time(json_response["server_modified"]);
        dto->cloud_hash_check_sum = json_response["content_hash"];
    }
}