#pragma once

#include <curl/curl.h>
#include <random>
#include <chrono>
#include <filesystem>
#include <fstream>

class RequestHandle {
public:
    RequestHandle();

    RequestHandle(const RequestHandle&) = delete;
    RequestHandle& operator=(const RequestHandle&) = delete;

    RequestHandle& operator=(RequestHandle&& other) noexcept;
    RequestHandle(RequestHandle&& other) noexcept;

    ~RequestHandle();

    void scheduleRetry();

    void setFileStream(const std::filesystem::path& file_path, std::ios::openmode mode);

    void addHeaders(const std::string& header);

    void clearHeaders();

    void setCommonCURLOpt();

    static void addGlobalResolve(
        const std::string& host,
        unsigned short src_port,
        const std::string& ip,
        unsigned short dst_port);

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output);

    static size_t readData(void* ptr, size_t size, size_t nmemb, void* stream);

    static size_t writeData(void* ptr, size_t size, size_t nmemb, void* stream);

    CURL* _curl;
    curl_mime* _mime;
    curl_slist* _headers;

    static curl_slist* _global_resolve;

    std::chrono::steady_clock::time_point _timer;
    std::fstream _iofd;
    std::string _response;
    int _retry_count;
};