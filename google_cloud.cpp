#include "google_cloud.hpp"

inline static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

GoogleCloud::GoogleCloud(const std::string& client_id, const std::string& client_secret, const std::string& refresh_token, const std::filesystem::path& home_dir, const std::string& start_page_token)
    : _client_id(client_id), _client_secret(client_secret), _refresh_token(refresh_token), _page_token(start_page_token) {
    
    //_refresh_token = first_time_auth();
    _access_token = getAccessToken();
    _home_dir_id = get_dir_id_by_path(home_dir);
    _dir_id_map.emplace(".", _home_dir_id);
    //get_start_page_token();
    //list_changes();
}
GoogleCloud::GoogleCloud(const std::string& client_id, const std::string& client_secret, const std::string& refresh_token, const std::string& home_dir, const std::string& start_page_token)
    : _client_id(client_id), _client_secret(client_secret), _refresh_token(refresh_token), _page_token(start_page_token) {

    _access_token = getAccessToken();
}


std::string GoogleCloud::get_dir_id_by_path(const std::filesystem::path& path) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Ошибка инициализации cURL" << std::endl;
        return "";
    }
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    std::string parent_id = "root";

    for (const auto& part : path.relative_path()) {
        std::string query = "https://www.googleapis.com/drive/v3/files?"
            "q=name%20%3D%20%27" + part.string() +
            "%27%20and%20mimeType%20%3D%20%27application%2Fvnd.google-apps.folder%27"
            "%20and%20%27" + parent_id + "%27%20in%20parents"
            "%20and%20trashed%3Dfalse&"
            "fields=files(id)";

        curl_easy_setopt(curl, CURLOPT_URL, query.c_str());

        CURLcode res = curl_easy_perform(curl);

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (res != CURLE_OK) {
            std::cerr << "Ошибка cURL: " << curl_easy_strerror(res) << std::endl;
            return "";
        }

        if (http_code != 200) {
            std::cerr << "Ошибка HTTP: " << http_code << std::endl;
            return "";
        }

        auto json_curr_resp = nlohmann::json::parse(response);
        parent_id = json_curr_resp["files"][0]["id"];
        response = "";
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    return std::move(parent_id);
}

std::unique_ptr<CurlEasyHandle> GoogleCloud::create_file_upload_handle(const std::filesystem::path& path) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();
    
    nlohmann::json j;
    j["name"] = path.filename().string();
    std::string metadata = j.dump();

    easy_handle->_mime = curl_mime_init(easy_handle->_curl);
    curl_mimepart* part = curl_mime_addpart(easy_handle->_mime);
    curl_mime_name(part, "metadata");
    curl_mime_data(part, metadata.c_str(), CURL_ZERO_TERMINATED);
    curl_mime_type(part, "application/json; charset=UTF-8");
    
    
    part = curl_mime_addpart(easy_handle->_mime);
    curl_mime_name(part, "media");
    curl_mime_filedata(part, path.c_str());
    curl_mime_type(part, "application/octet-stream");
    
    std::string url("https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart&fields=id,modifiedTime");
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

    return std::move(easy_handle);
}

std::unique_ptr<CurlEasyHandle> GoogleCloud::create_dir_upload_handle(const std::filesystem::path& path) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();

    const std::string url = "https://www.googleapis.com/drive/v3/files?fields=id,modifiedTime";
    
    nlohmann::json j;
    j["name"] = path.filename().string();
    j["mimeType"] = "application/vnd.google-apps.folder";
    std::string json_data = j.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=UTF-8");
    easy_handle->_headers = headers;

    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");

    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTPHEADER, easy_handle->_headers);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_POST, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_COPYPOSTFIELDS, json_data.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_FOLLOWLOCATION, 1L);

    return std::move(easy_handle);
}

std::unique_ptr<CurlEasyHandle> GoogleCloud::patch_change_parent(const std::string& id, const std::string& path) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();

    const std::string url = "https://www.googleapis.com/drive/v3/files/" + id +
        "?fields=parents&addParents=" + _dir_id_map[path] + "&removeParents=root";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=UTF-8");
    easy_handle->_headers = headers;

    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTPHEADER, easy_handle->_headers);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(easy_handle->_curl, CURLOPT_POSTFIELDS, "");
    curl_easy_setopt(easy_handle->_curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_FOLLOWLOCATION, 1L);

    return std::move(easy_handle);
}

void GoogleCloud::add_to_batch(const std::string& id, const std::string& path) {
    _batch_body += "--batch_boundary\r\n"
        "Content-Type: application/http\r\n"
        "Content-Transfer-Encoding: binary\r\n\r\n"
        "PATCH /drive/v3/files/" + id +
        "?fields=parents&addParents=" + _dir_id_map[path] + "&removeParents=root HTTP/1.1\r\n"
        "Content-Type: application/json; charset=UTF-8\r\n\r\n"
        "\r\n";
}

void GoogleCloud::return_dir_structure_batch() {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Ошибка инициализации cURL" << std::endl;
        return;
    }
    std::string boundary = "batch_boundary";
    std::string batch_url = "https://www.googleapis.com/batch/drive/v3";
    _batch_body += "--" + boundary + "--\r\n";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    headers = curl_slist_append(headers, ("Content-Type: multipart/mixed; boundary=" + boundary).c_str());

    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(curl, CURLOPT_URL, batch_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, _batch_body.c_str());
    curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "Ошибка cURL: " << curl_easy_strerror(res) << std::endl;
        return;
    }

    if (http_code != 200) {
        std::cerr << "Ошибка HTTP: " << http_code << std::endl;
        return;
    }
}

GoogleCloud::~GoogleCloud() {}

void GoogleCloud::generateAuthURL() {
    std::string url = "https://accounts.google.com/o/oauth2/v2/auth?"
        "response_type=code&"
        "client_id=" + _client_id +
        "&redirect_uri=http://localhost" +
        "&scope=https://www.googleapis.com/auth/drive" +
        "&access_type=offline";
    std::cout << url << std::endl;
}

std::string GoogleCloud::getRefreshToken(const std::string& auth_code) {
    CURL* curl = curl_easy_init();
    std::string response;

    if (curl) {
        std::string postFields = "code=" + auth_code +
            "&client_id=" + _client_id +
            "&client_secret=" + _client_secret +
            "&redirect_uri=http://localhost" +
            "&grant_type=authorization_code";

        curl_easy_setopt(curl, CURLOPT_URL, "https://oauth2.googleapis.com/token");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields.c_str());

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "Error: " << curl_easy_strerror(res) << std::endl;
        }

        curl_easy_cleanup(curl);
    }

    auto json_response = nlohmann::json::parse(response);
    std::string refresh_token = json_response["refresh_token"];

    return refresh_token;
}

void GoogleCloud::insert_path_id_mapping(const std::string& path, const std::string& id) {
    if (!_dir_id_map.contains(path)) {
        _dir_id_map.emplace(path, id);
    }
}
const std::string GoogleCloud::get_path_id_mapping(const std::string& path) const {
    if(_dir_id_map.contains(path)) {
        return _dir_id_map.at(path);
    }
    else {
        return "";
    }
}

std::string GoogleCloud::getAccessToken() {
    CURL* curl = curl_easy_init();
    std::string response;

    if (curl) {
        std::string postFields = "&client_id=" + _client_id +
            "&client_secret=" + _client_secret +
            "&refresh_token=" + _refresh_token +
            "&grant_type=refresh_token";

        curl_easy_setopt(curl, CURLOPT_URL, "https://oauth2.googleapis.com/token");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields.c_str());

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "Error: " << curl_easy_strerror(res) << std::endl;
        }

        curl_easy_cleanup(curl);
    }

    auto json_response = nlohmann::json::parse(response);
    std::string access_token = json_response["access_token"];

    return access_token;
}

std::string GoogleCloud::first_time_auth() {
    generateAuthURL();
    std::string auth_code = "";
    std::cin >> auth_code;
    std::string refresh_token = getRefreshToken(auth_code);

    return refresh_token;
}

void GoogleCloud::get_start_page_token() {
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

std::vector<nlohmann::json> GoogleCloud::list_changes() {
    CURL* curl = curl_easy_init();
    std::string response;
    std::vector<nlohmann::json> list;

    std::string url = "https://www.googleapis.com/drive/v3/changes?"
        "pageToken=" + _page_token + "&fields=newStartPageToken%2CnextPageToken%2C"
        "changes%28file%28id%2Ctrashed%2Cname%2CmodifiedTime%2CmimeType%2Cparents%29%29"
        "&includeItemsFromAllDrives=false"
        "&includeRemoved=false"
        "&restrictToMyDrive=false"
        "&supportsAllDrives=false";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    auto changes = nlohmann::json::parse(response);
    while (changes.contains("nextPageToken")) {
        list.push_back(changes["changes"]);
        response = "";
        changes.clear();
        _page_token = changes["nextPageToken"];
        std::string url = "https://www.googleapis.com/drive/v3/changes?"
            "pageToken=" + _page_token + "&fields=newStartPageToken%2CnextPageToken%2C"
            "changes%28file%28id%2Ctrashed%2Cname%2CmodifiedTime%2CmimeType%2Cparents%29%29"
            "&includeItemsFromAllDrives=false"
            "&includeRemoved=false"
            "&restrictToMyDrive=false"
            "&supportsAllDrives=false";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        CURLcode res = curl_easy_perform(curl);
        auto changes = nlohmann::json::parse(response);
    }
    _page_token = changes["newStartPageToken"];
    list.push_back(changes["changes"]);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return std::move(list);
}

std::string GoogleCloud::post_upload() {
    get_start_page_token();
    return std::move(_page_token);
}

const std::string& GoogleCloud::get_home_dir_id() const {
    return _home_dir_id;
}
