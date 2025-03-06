#pragma once

#include <filesystem>
#include "google_cloud.hpp"
#include "dropbox.hpp"
#include <curl/curl.h>
#include "database.hpp"
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <random>

class SyncHandler {
public:
    SyncHandler(const std::string config_name, const std::filesystem::path& loc_path, const bool IS_INITIAL);
    const std::filesystem::path& get_local_path() const;
    ~SyncHandler();

private:
    void initial_upload();
    void sync();
    void async_db_add_file_link();
    void async_curl_multi_small_worker();
    void async_curl_multi_files_worker();
    void check_delayed_requests(const std::string& type);
    void schedule_retry(std::unique_ptr<CurlEasyHandle> easy_handle);

    std::unordered_map<int, std::unique_ptr<BaseCloud>> _clouds;
    const std::string _db_file_name;
    const std::filesystem::path _loc_path;
    std::unique_ptr<Database> _db;
    std::unordered_map<CURL*, std::unique_ptr<CurlEasyHandle>> _active_handles_map;
    std::mutex _handles_map_mutex;
    std::vector<std::unique_ptr<CurlEasyHandle>> _delayed_vec;
    std::mutex _delayed_mutex;

    std::vector<std::pair<int, std::string>> _dirs_to_map;

    std::queue<FileLinkRecord> _file_link_queue;
    std::mutex _file_link_mutex;
    std::condition_variable _file_link_CV;
    std::atomic<bool> _file_link_finished;

    std::queue<nlohmann::json> _changes_queue;
    std::condition_variable _changes_CV;
    std::mutex _changes_mutex;
    std::atomic<bool> _changes_finished;

    const int _FILES_MAX_ACTIVE = 100;
    int _files_active_count = 0;
    std::queue<std::unique_ptr<CurlEasyHandle>> _files_curl_queue;
    std::mutex _files_curl_mutex;
    std::condition_variable _files_curl_CV;
    std::atomic<bool> _files_curl_finished;
    
    const int _SMALL_MAX_ACTIVE = 7;
    int _small_active_count = 0;
    std::queue<std::unique_ptr<CurlEasyHandle>> _small_curl_queue;
    std::mutex _small_curl_mutex;
    std::condition_variable _small_curl_CV;
    std::atomic<bool> _small_curl_finished;
};