#include "request-handle.h"

curl_slist* RequestHandle::_global_resolve = nullptr;

RequestHandle::RequestHandle()
    : _curl(curl_easy_init()),
    _mime(nullptr),
    _headers(nullptr),
    _retry_count(0)
{
#ifdef TESTING
    curl_easy_setopt(_curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(_curl, CURLOPT_SSL_VERIFYHOST, 0L);
#endif
    if (_global_resolve)
        curl_easy_setopt(_curl, CURLOPT_RESOLVE, _global_resolve);
}

RequestHandle& RequestHandle::operator=(RequestHandle&& other) noexcept {
    if (this != &other) {
        _curl = other._curl;
        _mime = other._mime;
        _headers = other._headers;

        other._curl = nullptr;
        other._mime = nullptr;
        other._headers = nullptr;
        
        _timer = other._timer;
        _retry_count = other._retry_count;
        _iofd = std::move(other._iofd);
    }
    return *this;
}

RequestHandle::RequestHandle(RequestHandle&& other) noexcept
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

RequestHandle::~RequestHandle() {
    curl_mime_free(_mime);
    curl_slist_free_all(_headers);
    curl_easy_cleanup(_curl);
}

void RequestHandle::scheduleRetry() {
    const int BASE_DELAY = 1000;

    _retry_count++;
    int delay = BASE_DELAY * (1 << (_retry_count + 1));

    if (_iofd.is_open()) {
        _iofd.clear();
        _iofd.seekg(0, std::ios::beg);
    }

    _response.clear();
    if (_retry_count > 6) {
        throw std::runtime_error("Too many retry attempts on a request");
    }

    int jitter = _retry_count * BASE_DELAY;
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(-jitter, jitter);
    delay += dist(gen);

    _timer = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay);
}

void RequestHandle::setFileStream(const std::filesystem::path& file_path, std::ios::openmode mode) {
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
        throw std::runtime_error("Error opening file: " + file_path.string());
    }
}

void RequestHandle::addHeaders(const std::string& header) {
    _headers = curl_slist_append(_headers, header.c_str());
}

void RequestHandle::clearHeaders() {
    curl_slist_free_all(_headers);
    _headers = nullptr;
}

void RequestHandle::setCommonCURLOpt() {
#ifdef TESTING
    curl_easy_setopt(_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
#else
    curl_easy_setopt(_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
#endif
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

size_t RequestHandle::writeCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total_size = size * nmemb;
    output->append((char*)contents, total_size);
    return total_size;
}

size_t RequestHandle::readData(void* ptr, size_t size, size_t nmemb, void* stream) {
    std::ifstream* file = static_cast<std::ifstream*>(stream);
    file->read(static_cast<char*>(ptr), size * nmemb);
    return file->gcount();
}

size_t RequestHandle::writeData(void* ptr, size_t size, size_t nmemb, void* stream) {
    std::ofstream* file = static_cast<std::ofstream*>(stream);
    file->write(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

void RequestHandle::addGlobalResolve(
    const std::string& host,
    unsigned short src_port,
    const std::string& ip,
    unsigned short dst_port)
{
    std::ostringstream oss;
    oss << host << ':' << src_port << ':' << ip << ':' << dst_port;
    _global_resolve = curl_slist_append(_global_resolve, oss.str().c_str());
}