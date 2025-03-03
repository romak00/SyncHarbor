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
    
    std::string url("https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart&fields=id,modifiedTime,md5Checksum");
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

    const std::string url = "https://www.googleapis.com/drive/v3/files?fields=id,modifiedTime,md5Checksum";
    
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

std::vector<nlohmann::json> GoogleCloud::get_changes(const int cloud_id, const std::shared_ptr<Database> db_conn) {
    CURL* curl = curl_easy_init();
    std::string response;
    std::vector<nlohmann::json> pages;

    std::string url = "https://www.googleapis.com/drive/v3/changes?"
        "pageToken=" + _page_token + "&fields=newStartPageToken%2CnextPageToken%2C"
        "changes%28file%28id%2Ctrashed%2Cname%2CmodifiedTime%2CmimeType%2Cparents%2Cmd5Checksum%29%29"
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
        pages.push_back(changes["changes"]);
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
        auto page = nlohmann::json::parse(response);
    }

    _page_token = changes["newStartPageToken"];
    pages.push_back(changes["changes"]);

    std::vector<nlohmann::json> changes_vec;
    for (const auto& page : pages) {
        for (const auto& change : page) {
            nlohmann::json file_change;
            
            if (change.contains("file")) {
                file_change = change["file"];
            }
            else {
                throw std::runtime_error("weird change file: " + change.dump());
            }
            
            nlohmann::json cloud_file_info = db_conn->get_cloud_file_info(file_change["id"], cloud_id);
            nlohmann::json change_template, data;
            data["tag"] = "NULL";
            data["cloud_id"] = cloud_id;
            data["cloud_file_id"] = file_change["id"];
            std::string tmp_type = file_change["mimeType"] == "application/vnd.google-apps.folder" ? "dir" : "file";
            int mod_time = convert_google_time(file_change["modifiedTime"]);
            data["cloud_file_modified_time"] = mod_time;
            if (cloud_file_info.empty()) {
                // new file
                    // find where and what
                    // find path + type + make global id
                    // ...
                nlohmann::json cloud_parent_info = db_conn->get_cloud_file_info(file_change["parents"][0], file_change["cloud_id"]);
                if (!cloud_parent_info.empty()) {
                    data["tag"] = "NEW";
                    data["type"] = tmp_type;
                    data["cloud_parent_id"] = file_change["parents"][0];
                    data["cloud_hash_check_sum"] = file_change.contains("md5Checksum") ? file_change["md5Checksum"] : "NULL";
                }
                // not our file
                    // just skipping
                else {
                    // TODO: consider where some "older" parent might be ours (need to make some kind of chain map??)
                    continue;
                }
            }
            // can be different changes at the same time!!!!!!!!!
                // file changed
                    // file renamed
                        // adding ?global_id? to ??MAP?? with rename tag + name + time + cloud
                    // file REALLY changed
                        // adding ?global_id? to ??MAP?? with changed tag + time + hash + cloud
                // file moved
                    // adding ?global_id? to ??MAP?? with moved tag + parent + cloud
                // file deleted
                    // adding ?global_id? to ??MAP?? with deleted tag + cloud (time not changing at least in google)
                // Nothing happend what really matters
                    // just skipping

            else if (file_change["trashed"] == true) {
                // delete
                data["tag"] = "DELETED";
            }
            else if (mod_time > cloud_file_info["cloud_file_modified_time"]) {
                std::cout << "bebra" << '\n';
                data["tag"] = "CHANGED";
                if (file_change.contains("md5Checksum")) {
                    if (file_change["md5Checksum"] != cloud_file_info["cloud_hash_check_sum"]) {
                        data["changed"] = true;
                        data["cloud_hash_check_sum"] = file_change["md5Checksum"];
                    }
                }
                else {
                    data["changed"] = true;
                }
                if (file_change["name"] != (db_conn->find_path_by_global_id(cloud_file_info["global_id"])).filename().string()) {
                    data["renamed"] = true;
                    data["name"] = file_change["name"];
                }
                if (file_change["parents"][0] != cloud_file_info["cloud_parent_id"]) {
                    nlohmann::json cloud_parent_info = db_conn->get_cloud_file_info(file_change["parents"][0], file_change["cloud_id"]);
                    if (!cloud_parent_info.empty()) {
                        data["moved"] = true;
                        data["cloud_parent_id"] = file_change["parents"][0];
                    }
                }
            }                                                                                           // TODO same as before: if new parent is not ours
            else if (file_change["parents"][0] != cloud_file_info["cloud_parent_id"]) {                 // check if its new or we moved file elsewhere
                nlohmann::json cloud_parent_info = db_conn->get_cloud_file_info(file_change["parents"][0], file_change["cloud_id"]);
                if (!cloud_parent_info.empty()) {
                    data["tag"] = "CHANGED";
                    data["moved"] = true;
                    data["cloud_parent_id"] = file_change["parents"][0];
                }
            }
            else {
                continue;
            }
            if (data["tag"] != "NULL") {
                file_change.clear();
                change_template["data"] = data;
                change_template["file"] = data["tag"] == "NEW" ?
                    db_conn->find_path_by_global_id(cloud_file_info["global_id"]).string()
                    : std::to_string(cloud_file_info["global_id"].get<int>());
                std::cout << change_template << '\n';
                changes_vec.emplace_back(std::move(change_template));
                change_template.clear();
            }
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return std::move(changes_vec);
}

std::string GoogleCloud::post_upload() {
    get_start_page_token();
    return std::move(_page_token);
}

const std::string& GoogleCloud::get_home_dir_id() const {
    return _home_dir_id;
}
