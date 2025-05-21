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
#include <variant>
#include <unordered_set>
#include <semaphore>
#include <ctime>
#include <iterator>


class ActiveCount {
public:

    void increment() noexcept {
        _count.fetch_add(1, std::memory_order_acq_rel);
    }

    void decrement() noexcept {
        if (_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard<std::mutex> lk(_mtx);
            _cv.notify_all();
        }
    }

    int get() const noexcept {
        return _count.load(std::memory_order_acquire);
    }

    bool isIdle() const noexcept {
        return get() == 0;
    }

    void waitUntilIdle() const {
        std::unique_lock<std::mutex> lk(_mtx);
        _cv.wait(lk, [&] {
            return _count.load(std::memory_order_acquire) == 0;
            });
    }

private:
    mutable std::mutex _mtx;
    mutable std::condition_variable _cv;
    std::atomic<int> _count{ 0 };
};

inline std::filesystem::path normalizePath(const std::filesystem::path& p) {
    std::string s = p.generic_string();

    if (s == "/") {
        return std::filesystem::path{};
    }

    if (!s.empty() && s.front() == '/') {
        s.erase(0, 1);
    }

    return std::filesystem::path{ s };
}

inline std::string generate_uuid() {
    std::random_device rd;
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
        << std::setw(8) << dist(rd) << '-'
        << std::setw(4) << (dist(rd) & 0xFFFF) << '-'
        << std::setw(4) << ((dist(rd) & 0x0FFF) | 0x4000) << '-'
        << std::setw(4) << ((dist(rd) & 0x3FFF) | 0x8000) << '-'
        << std::setw(12)
        << (static_cast<uint64_t>(dist(rd)) << 32 | dist(rd));
    return ss.str();
}

enum class CloudProviderType {
    LocalStorage,
    GoogleDrive,
    Dropbox,
    OneDrive,
    Yandex,
    MailRu
};

inline const char* to_cstr(CloudProviderType type) {
    switch (type)
    {
    case CloudProviderType::LocalStorage: return "LocalStorage";
    case CloudProviderType::GoogleDrive: return "GoogleDrive";
    case CloudProviderType::Dropbox: return "Dropbox";
    case CloudProviderType::OneDrive: return "OneDrive";
    case CloudProviderType::Yandex: return "Yandex";
    case CloudProviderType::MailRu: return "MailRu";
    default: return "Unknown";
    }
}

inline std::string_view to_string(CloudProviderType type) {
    return to_cstr(type);
}

inline CloudProviderType cloud_type_from_string(std::string_view str) {
    static const std::unordered_map<std::string_view, CloudProviderType> map =
    {
        {"LocalStorage", CloudProviderType::LocalStorage},
        {"GoogleDrive", CloudProviderType::GoogleDrive},
        {"Dropbox", CloudProviderType::Dropbox},
        {"OneDrive", CloudProviderType::OneDrive},
        {"Yandex", CloudProviderType::Yandex},
        {"MailRu", CloudProviderType::MailRu}
    };

    return map.at(str);
}

inline std::time_t convertCloudTime(std::string datetime) {
    if (!datetime.empty() && datetime.back() == 'Z')
        datetime.pop_back();
    auto dotPos = datetime.find('.');
    if (dotPos != std::string::npos)
        datetime.resize(dotPos);

    std::tm tm = {};
    std::istringstream iss(datetime);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    tm.tm_isdst = 0;

#if defined(_WIN32) || defined(_WIN64)
    return _mkgmtime(&tm);
#else
    return timegm(&tm);
#endif
}

inline std::time_t convertSystemTime(const std::filesystem::path& path) {
    const auto ftime = std::filesystem::last_write_time(path);
    auto fs_tp = std::chrono::file_clock::to_sys(ftime);
    auto stime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(fs_tp);
    return std::chrono::system_clock::to_time_t(stime);
}



enum class EntryType : uint8_t {
    File = 0,
    Directory = 1,
    Document = 2,
    Null = 3
};

inline const char* to_cstr(EntryType type) {
    switch (type)
    {
    case EntryType::File: return "File";
    case EntryType::Directory: return "Directory";
    case EntryType::Document: return "Document";
    default: return "Null";
    }
}

inline std::string_view to_string(EntryType type) {
    return to_cstr(type);
}

inline EntryType entry_type_from_string(std::string_view str) {
    static const std::unordered_map<std::string_view, EntryType> map =
    {
        {"File", EntryType::File},
        {"Directory", EntryType::Directory},
        {"Document", EntryType::Document},
        {"Null", EntryType::Null}
    };

    auto it = map.find(str);
    if (it != map.end()) {
        return it->second;
    }
    return EntryType::Null;
}

class FileRecordDTO {
public:
    FileRecordDTO(                          // LocalStorage NEW
        const EntryType t,
        const std::filesystem::path& rp,
        const uint64_t s,
        const std::time_t cfmt,
        const uint64_t hash,
        const uint64_t fid
    ) :
        type(t),
        rel_path(rp),
        size(s),
        cloud_file_modified_time(cfmt),
        cloud_hash_check_sum(hash),
        file_id(fid),
        cloud_id(0),
        global_id(0)
    {
    }

    FileRecordDTO(                          // Cloud info from db
        const int g_id,
        const int c_id,
        const std::string& parent,
        const std::string& cf_id,
        const uint64_t s,
        const std::string& hash,
        const std::time_t cfmt
    ) :
        cloud_parent_id(parent),
        cloud_id(c_id),
        cloud_file_id(cf_id),
        size(s),
        cloud_file_modified_time(cfmt),
        cloud_hash_check_sum(hash),
        global_id(g_id),
        file_id(0)
    {
    }

    FileRecordDTO(                          // LocalStorage info from db
        const int g_id,
        const EntryType t,
        const std::filesystem::path& rp,
        const uint64_t s,
        const uint64_t hash,
        const std::time_t cfmt,
        const uint64_t fid
    ) :
        type(t),
        rel_path(rp),
        size(s),
        cloud_file_modified_time(cfmt),
        cloud_hash_check_sum(hash),
        file_id(fid),
        global_id(g_id),
        cloud_id(0)
    {
    }

    FileRecordDTO(                          // NOT GoogleDrive Cloud NEW info
        const EntryType t,
        const std::filesystem::path& rp,
        const std::string& cf_id,
        const uint64_t s,
        const std::time_t cfmt,
        const std::string& hash,
        const int cid
    ) :
        type(t),
        rel_path(rp),
        cloud_file_id(cf_id),
        size(s),
        cloud_file_modified_time(cfmt),
        cloud_hash_check_sum(hash),
        cloud_id(cid),
        file_id(0),
        global_id(0)
    {
    }

    FileRecordDTO(                          // GoogleDrive Cloud NEW info
        const EntryType t,
        const std::string& parent,
        const std::filesystem::path& rp,
        const std::string& cf_id,
        const uint64_t s,
        const std::time_t cfmt,
        const std::string& hash,
        const int cid
    ) :
        type(t),
        cloud_parent_id(parent),
        rel_path(rp),
        cloud_file_id(cf_id),
        size(s),
        cloud_file_modified_time(cfmt),
        cloud_hash_check_sum(hash),
        cloud_id(cid),
        file_id(0),
        global_id(0)
    {
    }


    FileRecordDTO() = default;
    ~FileRecordDTO() = default;

    FileRecordDTO(const FileRecordDTO& other) = default;
    FileRecordDTO(FileRecordDTO&& other) noexcept = default;

    FileRecordDTO& operator=(const FileRecordDTO& other) = default;
    FileRecordDTO& operator=(FileRecordDTO&& other) noexcept = default;

    std::filesystem::path rel_path;
    std::string cloud_parent_id;
    std::string cloud_file_id;
    std::variant<std::string, uint64_t> cloud_hash_check_sum;
    uint64_t size;
    uint64_t file_id;
    std::time_t cloud_file_modified_time;
    int global_id;
    int cloud_id;
    EntryType type;
};

enum class ChangeType : uint8_t {
    Null = 0,
    New = 1 << 0,
    Update = 1 << 1,
    Move = 1 << 2,
    Rename = 1 << 3,
    Delete = 1 << 4
};

inline std::string to_string(ChangeType ch) {
    switch (ch)
    {
    case ChangeType::New: return "New";
    case ChangeType::Rename: return "Rename";
    case ChangeType::Update: return "Update";
    case ChangeType::Move: return "Move";
    case ChangeType::Delete: return "Delete";
    default: return "Null";
    }
}

inline ChangeType operator|(ChangeType a, ChangeType b) {
    return static_cast<ChangeType>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b)
        );
}

inline ChangeType operator&(ChangeType a, ChangeType b) {
    return static_cast<ChangeType>(
        static_cast<uint8_t>(a) & static_cast<uint8_t>(b)
        );
}

inline ChangeType operator~(ChangeType a) {
    return static_cast<ChangeType>(
        ~static_cast<uint8_t>(a)
        );
}

class ChangeTypeFlags {
public:
    ChangeTypeFlags() : value(ChangeType::New) {}
    ChangeTypeFlags(ChangeType t) : value(t) {}

    void add(ChangeType t) {
        value = value | t;
    }

    void remove(ChangeType t) {
        value = value & (~t);
    }

    bool contains(ChangeType t) const {
        return (value & t) == t;
    }

    ChangeType raw() const { return value; }

private:
    ChangeType value;
};

struct RawSignal {
    std::binary_semaphore sem{ 0 };
    std::atomic<bool>     dirty{ false };
};

class FileEvent {
public:
    FileEvent(
        const std::filesystem::path& p,
        const std::time_t tm,
        const ChangeType tp,
        std::shared_ptr<FileEvent> ass
    ) :
        path(p),
        when(tm),
        type(tp),
        associated(ass),
        file_id(0)
    {
    }

    FileEvent(
        const std::filesystem::path& p,
        const std::time_t tm,
        const ChangeType tp
    ) :
        path(p),
        when(tm),
        type(tp),
        associated(nullptr),
        file_id(0)
    {
    }

    FileEvent() = default;
    ~FileEvent() = default;
    FileEvent(const FileEvent& other) = default;
    FileEvent& operator=(const FileEvent& other) = default;

    FileEvent& operator=(FileEvent&& other) noexcept = default;
    FileEvent(FileEvent&& other) noexcept = default;

    std::filesystem::path path;
    std::shared_ptr<FileEvent> associated;
    uint64_t file_id;
    std::time_t when;
    ChangeType type;
};

class FileUpdatedDTO {
public:
    FileUpdatedDTO(                        // LocalStorage Modify
        const EntryType t,
        const int gid,
        const std::uint64_t chcs,
        const std::time_t cfmt,
        const std::filesystem::path& rp,
        const uint64_t s,
        const uint64_t fid
    ) :
        global_id(gid),
        cloud_hash_check_sum(chcs),
        rel_path(rp),
        cloud_file_modified_time(cfmt),
        type(t),
        size(s),
        file_id(fid),
        cloud_id(0)
    {
    }

    FileUpdatedDTO(                        // Cloud Modify
        const EntryType t,
        const int gid,
        const int cid,
        const std::string& cfid,
        const std::string& chcs,
        const std::time_t cfmt,
        const std::filesystem::path& rp,
        const std::string& cpid,
        const uint64_t s
    ) :
        global_id(gid),
        cloud_id(cid),
        cloud_file_id(cfid),
        cloud_hash_check_sum(chcs),
        cloud_parent_id(cpid),
        rel_path(rp),
        cloud_file_modified_time(cfmt),
        type(t),
        size(s),
        file_id(0)
    {
    }

    FileUpdatedDTO() = default;
    ~FileUpdatedDTO() = default;

    FileUpdatedDTO(const FileUpdatedDTO& other) = default;
    FileUpdatedDTO(FileUpdatedDTO&& other) noexcept = default;

    FileUpdatedDTO& operator=(const FileUpdatedDTO& other) = default;
    FileUpdatedDTO& operator=(FileUpdatedDTO&& other) noexcept = default;

    std::filesystem::path rel_path;
    std::string cloud_file_id;
    std::variant<std::string, uint64_t> cloud_hash_check_sum;
    std::string cloud_parent_id;
    uint64_t size;
    std::time_t cloud_file_modified_time;
    uint64_t file_id;
    int global_id;
    int cloud_id;
    EntryType type;
};

class FileMovedDTO {
public:
    FileMovedDTO(                        // LocalStorage Modify
        const EntryType t,
        const int gid,
        const std::time_t cfmt,
        const std::filesystem::path& orp,
        const std::filesystem::path& nrp
    ) :
        global_id(gid),
        old_rel_path(orp),
        new_rel_path(nrp),
        cloud_file_modified_time(cfmt),
        type(t),
        cloud_id(0)
    {
    }

    FileMovedDTO(                        // Cloud Modify
        const EntryType t,
        const int gid,
        const int cid,
        const std::string& cfid,
        const std::time_t cfmt,
        const std::filesystem::path& orp,
        const std::filesystem::path& nrp,
        const std::string& old_cpid,
        const std::string& new_cpid
    ) :
        global_id(gid),
        cloud_id(cid),
        cloud_file_id(cfid),
        old_cloud_parent_id(old_cpid),
        new_cloud_parent_id(new_cpid),
        old_rel_path(orp),
        new_rel_path(nrp),
        cloud_file_modified_time(cfmt),
        type(t)
    {
    }

    FileMovedDTO() = default;
    ~FileMovedDTO() = default;

    FileMovedDTO(const FileMovedDTO& other) = default;
    FileMovedDTO(FileMovedDTO&& other) noexcept = default;

    FileMovedDTO& operator=(const FileMovedDTO& other) = default;
    FileMovedDTO& operator=(FileMovedDTO&& other) noexcept = default;

    std::filesystem::path old_rel_path;
    std::filesystem::path new_rel_path;
    std::string cloud_file_id;
    std::string old_cloud_parent_id;
    std::string new_cloud_parent_id;
    std::time_t cloud_file_modified_time;
    int global_id;
    int cloud_id;
    EntryType type;
};

class FileDeletedDTO {
public:
    FileDeletedDTO(                        // LocalStorage Delete
        const std::filesystem::path& rp,
        const int gid,
        const std::time_t t
    ) :
        rel_path(rp),
        global_id(gid),
        when(t),
        cloud_id(0)
    {
    }

    FileDeletedDTO(                         // Cloud Delete
        const std::filesystem::path& rp,
        const int gid,
        const int cid,
        const std::string& cfid,
        const std::time_t t
    ) :
        rel_path(rp),
        global_id(gid),
        cloud_id(cid),
        cloud_file_id(cfid),
        when(t)
    {
    }

    FileDeletedDTO() = default;
    ~FileDeletedDTO() = default;

    FileDeletedDTO(const FileDeletedDTO& other) = default;
    FileDeletedDTO(FileDeletedDTO&& other) noexcept = default;

    FileDeletedDTO& operator=(const FileDeletedDTO& other) = default;
    FileDeletedDTO& operator=(FileDeletedDTO&& other) noexcept = default;

    std::filesystem::path rel_path;
    std::string cloud_file_id;
    std::time_t when;
    int global_id;
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
        const int BASE_DELAY = 1000;
        int delay = BASE_DELAY * (1 << (_retry_count + 1));

        if (_iofd.is_open()) {
            _iofd.clear();
            _iofd.seekg(0, std::ios::beg);
        }

        _response.clear();
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

    void setFileStream(const std::string& file_path, std::ios::openmode mode) {
        _iofd = std::fstream(file_path, mode | std::ios::binary);
        if (_iofd && _iofd.is_open()) {
            switch (mode) {
            case std::ios::in:
                curl_easy_setopt(_curl, CURLOPT_READDATA, &_iofd);
                curl_easy_setopt(_curl, CURLOPT_READFUNCTION, RequestHandle::readData);
                break;
            case std::ios::out:
                curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &_iofd);
                curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, RequestHandle::writeData);
                break;
            }
        }
        else {
            throw std::runtime_error("Error opening file: " + file_path);
        }
    }

    void addHeaders(const std::string& header) {
        _headers = curl_slist_append(_headers, header.c_str());
    }

    void clearHeaders() {
        curl_slist_free_all(_headers);
        _headers = nullptr;
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
        curl_easy_setopt(_curl, CURLOPT_WRITEDATA, &_response);
        curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, RequestHandle::writeCallback);
        if (_headers) {
            curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, _headers);
        }
    }

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
        size_t total_size = size * nmemb;
        output->append((char*)contents, total_size);
        return total_size;
    }

    static size_t readData(void* ptr, size_t size, size_t nmemb, void* stream) {
        std::ifstream* file = static_cast<std::ifstream*>(stream);
        file->read(static_cast<char*>(ptr), size * nmemb);
        return file->gcount();
    }

    static size_t writeData(void* ptr, size_t size, size_t nmemb, void* stream) {
        std::ofstream* file = static_cast<std::ofstream*>(stream);
        file->write(static_cast<char*>(ptr), size * nmemb);
        return size * nmemb;
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
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;
    ~ThreadSafeQueue() = default;

    template<typename U>
    void push(U&& request) {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.emplace(std::forward<U>(request));
        _cv.notify_one();
    }

    void push(std::vector<T>&& request_vec) {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto& elem : request_vec) {
            _queue.emplace(std::move(elem));
        }
        _cv.notify_one();
    }

    bool try_pop(T& out) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queue.empty()) {
            return false;
        }
        out = std::move(_queue.front());
        _queue.pop();
        return true;
    }

    bool pop(T& out) {
        std::unique_lock<std::mutex> lock(_mutex);
        _cv.wait(lock, [&]() {
            return !_queue.empty() || _closed.load(std::memory_order_acquire);
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

    void close() {
        _closed.store(true, std::memory_order_release);
        _cv.notify_all();
    }

    int size() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.size();
    }

private:
    std::queue<T> _queue;
    mutable std::mutex _mutex;
    mutable std::condition_variable _cv;
    std::atomic<bool> _closed{ false };
};


class ThreadSafeEventsRegistry {
public:

    ThreadSafeEventsRegistry() = default;
    ~ThreadSafeEventsRegistry() = default;

    void add(const std::string& path, ChangeType ct) {
        std::lock_guard<std::mutex> lock(_mutex);
        _map.emplace(path, ct);
    }

    bool check(const std::string& path, ChangeType ct) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_map.contains(path) && _map[path] == ct) {
            _map.erase(path);
            return true;
        }
        return false;
    }

    std::unordered_map<std::string, ChangeType> copyMap() {
        std::lock_guard<std::mutex> lock(_mutex);
        auto map = _map;
        _map.clear();
        return map;
    }


private:
    std::unordered_map<std::string, ChangeType> _map;
    std::mutex _mutex;
};

class PrevEventsRegistry {
public:

    PrevEventsRegistry(const std::unordered_map<std::string, ChangeType>& map) : _map(map)
    {
    }

    ~PrevEventsRegistry() = default;

    void add(const std::string& path, ChangeType ct) {
        _map.emplace(path, ct);
    }

    bool check(const std::string& path, ChangeType ct) {
        if (_map.contains(path) && _map[path] == ct) {
            _map.erase(path);
            return true;
        }
        return false;
    }

private:
    std::unordered_map<std::string, ChangeType> _map;
};