#pragma once

#include <fstream>
#include <string>
#include <sstream>
#include <iomanip>
#include <curl/curl.h>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <random>
#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

inline std::time_t convert_cloud_time(std::string datetime) {
    datetime.pop_back();
    size_t dotPos = datetime.find('.');
    datetime = datetime.substr(0, dotPos);
    std::tm tm = {};
    std::istringstream iss(datetime);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return timegm(&tm);
}

enum class EntryType : uint8_t {
    File = 0,
    Directory = 1,
    Document = 2
};

inline const char* to_cstr(EntryType type) {
    switch (type) {
    case EntryType::File: return "File";
    case EntryType::Directory: return "Directory";
    case EntryType::Document: return "Document";
    default: return "Unknown";
    }
}

inline std::string_view to_string(EntryType type) {
    return to_cstr(type);
}

inline EntryType entry_type_from_string(std::string_view str) {
    static const std::unordered_map<std::string_view, EntryType> map = {
        {"File", EntryType::File},
        {"Directory", EntryType::Directory},
        {"Document", EntryType::Document}
    };

    auto it = map.find(str);
    if (it != map.end()) return it->second;
    throw std::invalid_argument("Invalid EntryType string: " + std::string(str));
}


enum class UpdateType {
    Metadata,
    Contents,
    All
};

class FileRecordDTO {
public:
    FileRecordDTO(const EntryType t,
        const std::filesystem::path& rp,
        const size_t s,
        const std::time_t cfmt)
        : type(t),
        rel_path(rp),
        size(s),
        cloud_file_modified_time(cfmt),
        cloud_id(0)
    {
    }

    FileRecordDTO() = default;
    ~FileRecordDTO() = default;

    FileRecordDTO(const FileRecordDTO& other)
        : type(other.type),
        global_id(other.global_id),
        rel_path(other.rel_path),
        cloud_parent_id(other.cloud_parent_id),
        size(other.size),
        cloud_file_id(other.cloud_file_id),
        cloud_hash_check_sum(other.cloud_hash_check_sum),
        cloud_file_modified_time(other.cloud_file_modified_time),
        cloud_id(other.cloud_id)
    {
    }

    FileRecordDTO(FileRecordDTO&& other) noexcept
        : type(other.type),
        global_id(other.global_id),
        rel_path(std::move(other.rel_path)),
        cloud_parent_id(std::move(other.cloud_parent_id)),
        size(other.size),
        cloud_file_id(std::move(other.cloud_file_id)),
        cloud_hash_check_sum(std::move(other.cloud_hash_check_sum)),
        cloud_file_modified_time(other.cloud_file_modified_time),
        cloud_id(other.cloud_id)
    {
    }

    std::filesystem::path rel_path;
    std::string cloud_parent_id;
    std::string cloud_file_id;
    std::string cloud_hash_check_sum;
    size_t size;
    std::time_t cloud_file_modified_time;
    int global_id;
    int cloud_id;
    EntryType type;
};

class ChangeDTO {
public:
    ChangeDTO(const std::string& rp, const EntryType t) : rel_path(rp), type(t)
    {
    }

    ChangeDTO() = default;
    ~ChangeDTO() = default;

    ChangeDTO(const ChangeDTO& other) : rel_path(other.rel_path), type(other.type)
    {
    }

    ChangeDTO(ChangeDTO&& other) noexcept : rel_path(std::move(other.rel_path)), type(other.type)
    {
    }

    std::filesystem::path rel_path;
    EntryType type;
};

class FileModifiedDTO {
public:
    FileModifiedDTO(const std::filesystem::path& rp,
        const int gid,
        const int cid,
        const std::string& cfid,
        const std::string& chcs,
        const std::string& old_cpid,
        const std::string& new_cpid,
        const std::string& new_n,
        const std::time_t cfmt,
        const EntryType t)
        : rel_path(rp),
        global_id(gid),
        cloud_id(cid),
        cloud_file_id(cfid),
        cloud_hash_check_sum(chcs),
        old_cloud_parent_id(old_cpid),
        new_cloud_parent_id(new_cpid),
        new_name(new_n),
        cloud_file_modified_time(cfmt),
        type(t)
    {
    }

    FileModifiedDTO() = default;
    ~FileModifiedDTO() = default;

    FileModifiedDTO(const FileModifiedDTO& other)
        : rel_path(other.rel_path),
        global_id(other.global_id),
        cloud_id(other.cloud_id),
        cloud_file_id(other.cloud_file_id),
        cloud_hash_check_sum(other.cloud_hash_check_sum),
        old_cloud_parent_id(other.old_cloud_parent_id),
        new_cloud_parent_id(other.new_cloud_parent_id),
        new_name(other.new_name),
        cloud_file_modified_time(other.cloud_file_modified_time),
        type(other.type)
    {
    }

    FileModifiedDTO(FileModifiedDTO&& other) noexcept
        : rel_path(std::move(other.rel_path)),
        global_id(other.global_id),
        cloud_id(other.cloud_id),
        cloud_file_id(std::move(other.cloud_file_id)),
        cloud_hash_check_sum(std::move(other.cloud_hash_check_sum)),
        old_cloud_parent_id(std::move(other.old_cloud_parent_id)),
        new_cloud_parent_id(std::move(other.new_cloud_parent_id)),
        new_name(std::move(other.new_name)),
        cloud_file_modified_time(other.cloud_file_modified_time),
        type(other.type)
    {
    }

    std::filesystem::path rel_path;
    std::string cloud_file_id;
    std::string cloud_hash_check_sum;
    std::string old_cloud_parent_id;
    std::string new_cloud_parent_id;
    std::string new_name;
    std::time_t cloud_file_modified_time;
    int global_id;
    int cloud_id;
    EntryType type;
};

class FileDeletedDTO {
public:
    FileDeletedDTO(const std::string& rp,
        const std::string& cfid,
        const int cid)
        : rel_path(rp),
        cloud_file_id(cfid),
        cloud_id(cid)
    {
    }

    FileDeletedDTO() = default;
    ~FileDeletedDTO() = default;

    FileDeletedDTO(const FileDeletedDTO& other)
        : rel_path(other.rel_path),
        cloud_file_id(other.cloud_file_id),
        cloud_id(other.cloud_id)
    {
    }

    FileDeletedDTO(FileDeletedDTO&& other) noexcept
        : rel_path(std::move(other.rel_path)),
        cloud_file_id(std::move(other.cloud_file_id)),
        cloud_id(other.cloud_id)
    {
    }

    std::string rel_path;
    std::string cloud_file_id;
    int cloud_id;
};

class RequestHandle {
public:
    RequestHandle()
        : _curl(curl_easy_init()),
        _mime(nullptr),
        _headers(nullptr),
        _retry_count(0)
    {
    }

    RequestHandle(const RequestHandle&) = delete;
    RequestHandle& operator=(const RequestHandle&) = delete;

    RequestHandle(RequestHandle&& other) noexcept
        : _curl(other._curl),
        _mime(other._mime),
        _headers(other._headers),
        _timer(other._timer),
        _retry_count(other._retry_count),
        _iofd(std::move(other._iofd))
    {
        other._curl = nullptr;
        other._mime = nullptr;
        other._headers = nullptr;
    }

    ~RequestHandle() {
        curl_mime_free(_mime);
        curl_slist_free_all(_headers);
        curl_easy_cleanup(_curl);
    }

    void scheduleRetry() {
        const int BASE_DELAY = 500;
        int delay = BASE_DELAY * (1 << (_retry_count + 1));

        if (_iofd.is_open()) {
            _iofd.clear();
            _iofd.seekg(0, std::ios::beg);
        }

        _response = "";
        _retry_count++;
        if (_retry_count > 5) {
            throw std::runtime_error("Too many retry attempts on a request");
        }

        int jitter = _retry_count * BASE_DELAY;
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(-jitter, jitter);
        delay += dist(gen);

        _timer = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
    }

    void addHeaders(const std::string& header) {
        _headers = curl_slist_append(_headers, header.c_str());
    }

    void setCommonCURLOpt() {
        curl_easy_setopt(_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
        curl_easy_setopt(_curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
        curl_easy_setopt(_curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
        curl_easy_setopt(_curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(_curl, CURLOPT_TCP_NODELAY, 1L);
        curl_easy_setopt(_curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(_curl, CURLOPT_BUFFERSIZE, 131072L);
        curl_easy_setopt(_curl, CURLOPT_MAX_SEND_SPEED_LARGE, 0);
        curl_easy_setopt(_curl, CURLOPT_MAX_RECV_SPEED_LARGE, 0);
        curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, RequestHandle::writeCallback);
        curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &_response);
        if (_headers) {
            curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, _headers);
        }
    }

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
        size_t total_size = size * nmemb;
        output->append((char*)contents, total_size);
        return total_size;
    }

    CURL* _curl;
    curl_mime* _mime;
    curl_slist* _headers;

    std::chrono::steady_clock::time_point _timer;
    std::fstream _iofd;
    std::string _response;
    int _retry_count;
};

template<typename T>
class LockFreeQueue {
public:
    LockFreeQueue(size_t capacity)
        : _capacity(capacity), _buffer(new T[capacity]), _head(0), _tail(0)
    {
    }

    ~LockFreeQueue() {
        delete[] _buffer;
    }

    bool push(T&& item) {
        size_t next_tail = (_tail + 1) % _capacity;
        if (next_tail == _head.load(std::memory_order_acquire)) {
            return false;
        }
        _buffer[_tail] = std::move(item);
        _tail.store(next_tail, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        if (_head.load(std::memory_order_acquire) == _tail.load(std::memory_order_acquire)) {
            return false;
        }
        item = std::move(_buffer[_head]);
        _head.store((_head + 1) % _capacity, std::memory_order_release);
        return true;
    }

private:
    const size_t _capacity;
    T* _buffer;
    std::atomic<size_t> _head;
    std::atomic<size_t> _tail;
};


template<typename T>
class RequestQueue {
public:
    template<typename U>
    void push(U&& request) {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push(std::forward<U>(request));
        _cv.notify_all();
    }

    bool pop(T& out, std::function<bool()> externalCondition = []() { return false; }) {
        std::unique_lock<std::mutex> lock(_mutex);
        _cv.wait(lock, [&]() {
            return !_queue.empty() || externalCondition();
            });
        if (!_queue.empty()) {
            out = std::move(_queue.front());
            _queue.pop();
            return true;
        }
        return false;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.empty();
    }

    void notify() const {
        _cv.notify_all();
    }

private:
    std::queue<T> _queue;
    mutable std::mutex _mutex;
    std::condition_variable _cv;
};