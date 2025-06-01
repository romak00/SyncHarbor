#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

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