#include "google_cloud.hpp"

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

GoogleDrive::GoogleDrive(const std::string& client_id, const std::string& client_secret, const std::string& refresh_token, const std::filesystem::path& home_dir, const std::string& start_page_token)
    : _client_id(client_id), _client_secret(client_secret), _refresh_token(refresh_token), _page_token(start_page_token) {
    
    //_refresh_token = first_time_auth();
    _access_token = getAccessToken();
    _home_dir_id = get_dir_id_by_path(home_dir);
    _dir_id_map.emplace(".", _home_dir_id);
    //get_start_page_token();
    //list_changes();
}
GoogleDrive::GoogleDrive(const std::string& client_id, const std::string& client_secret, const std::string& refresh_token, const std::string& home_dir, const std::string& start_page_token)
    : _client_id(client_id), _client_secret(client_secret), _refresh_token(refresh_token), _page_token(start_page_token) {

    _access_token = getAccessToken();
}


std::string GoogleDrive::get_dir_id_by_path(const std::filesystem::path& path) {
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
            std::cerr << "Ошибка cURL: " << curl_easy_strerror(res) << '\n';
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return "";
        }

        if (http_code != 200) {
            std::cerr << "Ошибка HTTP: " << http_code << '\n';
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return "";
        }

        auto json_curr_resp = nlohmann::json::parse(response);
        parent_id = json_curr_resp["files"][0]["id"];
        response = "";
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return std::move(parent_id);
}

std::unique_ptr<CurlEasyHandle> GoogleDrive::create_file_upload_handle(const std::filesystem::path& path) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();
    if (!easy_handle->_curl) {
        throw std::runtime_error("Error initializing curl create_file_upload_handle");
    }
    nlohmann::json j;
    j["name"] = path.filename().string();
    std::string metadata = j.dump();

    easy_handle->_mime = curl_mime_init(easy_handle->_curl);
    curl_mimepart* part = curl_mime_addpart(easy_handle->_mime);
    curl_mime_name(part, "metadata");
    curl_mime_data(part, metadata.c_str(), CURL_ZERO_TERMINATED);
    curl_mime_type(part, "application/json; charset=UTF-8");

    easy_handle->type = "file_upload";

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

std::unique_ptr<CurlEasyHandle> GoogleDrive::create_dir_upload_handle(const std::filesystem::path& path) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();
    if (!easy_handle->_curl) {
        throw std::runtime_error("Error initializing curl create_dir_upload_handle");
    }
    const std::string url = "https://www.googleapis.com/drive/v3/files?fields=id,modifiedTime,md5Checksum";
    
    nlohmann::json j;
    j["name"] = path.filename().string();
    j["mimeType"] = "application/vnd.google-apps.folder";
    std::string json_data = j.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=UTF-8");
    easy_handle->_headers = headers;

    easy_handle->type = "dir_upload";

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

std::unique_ptr<CurlEasyHandle> GoogleDrive::create_file_delete_handle(const std::string& id) {
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

    return std::move(easy_handle);
}

std::unique_ptr<CurlEasyHandle> GoogleDrive::create_metadata_update_handle(const std::string& id, const std::string& parent, const std::string& name) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();
    if (!easy_handle->_curl) {
        throw std::runtime_error("Error initializing curl create_metadata_update_handle");
    }

    std::string parent_id = _dir_id_map.empty() ? parent : _dir_id_map[parent];

    std::string url = "https://www.googleapis.com/drive/v3/files/" + id + "?fields=parents";

    nlohmann::json j;
    j["parents"] = { parent_id };
    if (name != "") {
        j["name"] = name;
        url += ",name";
    }
    std::string data = j.dump();
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json; charset=UTF-8");
    easy_handle->_headers = headers;

    easy_handle->type = "metadata_update";

    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTPHEADER, easy_handle->_headers);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(easy_handle->_curl, CURLOPT_COPYPOSTFIELDS, data.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_FOLLOWLOCATION, 1L);

    return std::move(easy_handle);
}

GoogleDrive::~GoogleDrive() {}

void GoogleDrive::generateAuthURL() {
    std::string url = "https://accounts.google.com/o/oauth2/v2/auth?"
        "response_type=code&"
        "client_id=" + _client_id +
        "&redirect_uri=http://localhost" +
        "&scope=https://www.googleapis.com/auth/drive" +
        "&access_type=offline";
    std::cout << url << std::endl;
}

std::string GoogleDrive::getRefreshToken(const std::string& auth_code) {
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

void GoogleDrive::insert_path_id_mapping(const std::string& path, const std::string& id) {
    if (!_dir_id_map.contains(path)) {
        _dir_id_map.emplace(path, id);
    }
}
const std::string GoogleDrive::get_path_id_mapping(const std::string& path) const {
    if(_dir_id_map.contains(path)) {
        return _dir_id_map.at(path);
    }
    else {
        return "";
    }
}

std::string GoogleDrive::getAccessToken() {
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

std::string GoogleDrive::first_time_auth() {
    generateAuthURL();
    std::string auth_code = "";
    std::cin >> auth_code;
    std::string refresh_token = getRefreshToken(auth_code);

    return refresh_token;
}

void GoogleDrive::get_start_page_token() {
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

std::vector<nlohmann::json> GoogleDrive::get_changes(const int cloud_id, const std::shared_ptr<Database> db_conn) {
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
            data["cloud_hash_check_sum"] = "NULL";
            if (cloud_file_info.empty()) {
                // new file
                // find where and what
                    // find path + type + make global id
                    // ...
                    nlohmann::json cloud_parent_info;
                if (file_change.contains("parents")) {
                    cloud_parent_info = db_conn->get_cloud_file_info(file_change["parents"][0], data["cloud_id"]);
                }
                if (!cloud_parent_info.empty()) {
                    data["tag"] = "NEW";
                    data["type"] = tmp_type;
                    data["cloud_parent_id"] = file_change["parents"][0];
                    data["cloud_hash_check_sum"] = file_change.contains("md5Checksum") ? file_change["md5Checksum"] : "NULL";
                    std::string tmp_str = db_conn->find_path_by_global_id(cloud_parent_info["global_id"]).string();
                    change_template["file"] = tmp_str + "/" + file_change["name"].get<std::string>();
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
                data["global_id"] = cloud_file_info["global_id"];
            }
            else if (mod_time > cloud_file_info["cloud_file_modified_time"]) {
                data["tag"] = "CHANGED";
                data["global_id"] = cloud_file_info["global_id"];
                data["cloud_parent_id"] = file_change["parents"][0];
                data["name"] = file_change["name"];
                if (file_change.contains("md5Checksum")) {
                    if (file_change["md5Checksum"] != cloud_file_info["cloud_hash_check_sum"]) {
                        data["changed"] = true;
                        data["cloud_hash_check_sum"] = file_change["md5Checksum"];
                    }
                }
                else if (tmp_type == "file") {
                    data["changed"] = true;
                }

                if (file_change["name"] != (db_conn->find_path_by_global_id(cloud_file_info["global_id"])).filename().string()) {
                    data["renamed"] = true;
                }
                if (file_change["parents"][0] != cloud_file_info["cloud_parent_id"]) {
                    nlohmann::json cloud_parent_info = db_conn->get_cloud_file_info(file_change["parents"][0], file_change["cloud_id"]);
                    if (!cloud_parent_info.empty()) {
                        data["moved"] = true;
                    }
                }
            }                                                                                           // TODO same as before: if new parent is not ours
            else if (file_change["parents"][0] != cloud_file_info["cloud_parent_id"]) {                 // check if its new or we moved file elsewhere
                nlohmann::json cloud_parent_info = db_conn->get_cloud_file_info(file_change["parents"][0], file_change["cloud_id"]);
                if (!cloud_parent_info.empty()) {
                    data["global_id"] = cloud_file_info["global_id"];
                    data["tag"] = "CHANGED";
                    data["moved"] = true;
                    data["cloud_parent_id"] = file_change["parents"][0];
                    data["name"] = file_change["name"];
                }
            }
            else {
                continue;
            }
            if (data["tag"] != "NULL") {
                file_change.clear();
                change_template["data"] = data;
                change_template["file"] = data["tag"] == "NEW" ?
                    change_template["file"].get<std::string>()
                    : std::to_string(cloud_file_info["global_id"].get<int>());
                changes_vec.emplace_back(std::move(change_template));
                change_template.clear();
            }
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return std::move(changes_vec);
}

std::unique_ptr<CurlEasyHandle> GoogleDrive::create_file_download_handle(const std::string& id, const std::filesystem::path& path) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();
    if (!easy_handle->_curl) {
        throw std::runtime_error("Error initializing curl create_file_download_handle");
    }

    std::string url = "https://www.googleapis.com/drive/v3/files/" + id + "?alt=media";
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + _access_token).c_str());
    easy_handle->_headers = headers;
    easy_handle->type = "file_download";

    easy_handle->_ofc.emplace(path.parent_path().string() + "-tmp-copy-cloudsync-" + path.filename().string(), std::ios::binary);
    if (easy_handle->_ofc && easy_handle->_ofc->is_open()) {
        curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEDATA, &(*easy_handle->_ofc));
    }
    else {
        throw std::runtime_error("Error opening file for upload create_file_download_handle: " + path.string());
    }

    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_handle->_curl, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");

    curl_easy_setopt(easy_handle->_curl, CURLOPT_HTTPHEADER, easy_handle->_headers);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(easy_handle->_curl, CURLOPT_FOLLOWLOCATION, 1L);

    curl_easy_setopt(easy_handle->_curl, CURLOPT_WRITEFUNCTION, write_data);

    return std::move(easy_handle);
}

std::unique_ptr<CurlEasyHandle> GoogleDrive::create_file_update_handle(const std::string& id, const std::filesystem::path& path, const std::string& name, const std::string& parent_id) {
    std::unique_ptr<CurlEasyHandle> easy_handle = std::make_unique<CurlEasyHandle>();
    if (!easy_handle->_curl) {
        throw std::runtime_error("Error initializing curl create_file_update_handle");
    }
    std::string url = "https://www.googleapis.com/upload/drive/v3/files/" + id + "?uploadType=multipart";

    nlohmann::json j;
    j["name"] = name;
    j["parents"] = { parent_id };
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

    return std::move(easy_handle);
}

std::string GoogleDrive::post_upload() {
    get_start_page_token();
    return std::move(_page_token);
}

const std::string& GoogleDrive::get_home_dir_id() const {
    return _home_dir_id;
}
