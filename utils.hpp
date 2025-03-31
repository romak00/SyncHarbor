#pragma once

#include <fstream>
#include <string>
#include <sstream>
#include <iomanip>
#include <curl/curl.h>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <iostream>

inline std::time_t convert_cloud_time(std::string datetime) {
    datetime.pop_back();
    size_t dotPos = datetime.find('.');
    datetime = datetime.substr(0, dotPos);
    std::tm tm = {};
    std::istringstream iss(datetime);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return timegm(&tm);
}


class FileLinkRecord {
public:
    FileLinkRecord(const std::string& t, const int g_id, const int c_id, const std::string parent, const std::filesystem::path& p)
        : type(t), global_id(g_id), cloud_id(c_id), parent_id(parent), path(p), hash_check_sum("NULL") {
    }

    FileLinkRecord() {}
    ~FileLinkRecord() {}
    FileLinkRecord(const FileLinkRecord&) = delete;
    FileLinkRecord(FileLinkRecord&& other) noexcept : type(other.type), modified_time(other.modified_time),
        global_id(other.global_id), cloud_id(other.cloud_id), parent_id(other.parent_id), cloud_file_id(other.cloud_file_id), hash_check_sum(other.hash_check_sum), path(other.path), info(other.info) {
    }
    std::filesystem::path path;
    std::string info;
    std::string type;
    std::string cloud_file_id;
    std::string parent_id;
    std::string hash_check_sum;
    std::time_t modified_time;
    int cloud_id;
    int global_id;

};

class CurlEasyHandle {
public:
    CurlEasyHandle()
        : _curl(curl_easy_init()),
        _mime(nullptr),
        _headers(nullptr),
        _response("") {
    }
    CurlEasyHandle(const CurlEasyHandle&) = delete;

    ~CurlEasyHandle() {
        if (_ofc.is_open()) {
            _ofc.close();
        }
        if (_ifc.is_open()) {
            _ifc.close();
        }
        if (_mime) {
            curl_mime_free(_mime);
        }
        if (_headers) {
            curl_slist_free_all(_headers);
        }
        curl_easy_cleanup(_curl);
    }

    CURL* _curl;
    curl_mime* _mime;
    curl_slist* _headers;
    std::optional<FileLinkRecord> _file_info;
    std::string _response;
    std::chrono::steady_clock::time_point timer;
    int retry_count = 0;
    std::ofstream _ofc;
    std::ifstream _ifc;
    std::string type;
};
