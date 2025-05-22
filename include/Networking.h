#pragma once

#include <curl/curl.h>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <queue>
#include <vector>
#include "utils.h"
#include "BaseStorage.h"

class ICommand;

class HttpClient {
public:
    static HttpClient& get();
    void submit(std::unique_ptr<ICommand> command);
    void setClouds(const std::unordered_map<int, std::shared_ptr<BaseStorage>>& clouds);

    void shutdown();
    void start();

    void syncRequest(const std::unique_ptr<RequestHandle>& handle);

    bool isIdle() const noexcept;

    void waitUntilIdle() const;

private:
    HttpClient();
    ~HttpClient();


    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    HttpClient(HttpClient&&) noexcept = delete;
    HttpClient& operator=(HttpClient&&) noexcept = delete;

    void largeRequestsWorker();

    void checkDelayedRequests();

    std::atomic<bool> _should_stop{ false };
    std::unordered_map<CURL*, std::unique_ptr<ICommand>> _active_handles;
    std::vector<std::unique_ptr<ICommand>> _delayed_requests;

    std::unordered_map<int, std::shared_ptr<BaseStorage>> _clouds;

    ThreadSafeQueue<std::unique_ptr<ICommand>> _large_queue;

    std::unique_ptr<std::thread> _large_worker;

    CURLM* _large_multi_handle;

    int _MAX_CONCURRENT = 120;

    ActiveCount _large_active_count;
};