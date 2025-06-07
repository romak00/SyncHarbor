#pragma once

#include <mutex>
#include <condition_variable>
#include <atomic>

class ActiveCount {
public:

    void increment() noexcept;
    void decrement() noexcept;

    int get() const noexcept;

    bool isIdle() const noexcept;
    void waitUntilIdle() const;

private:
    mutable std::mutex _mtx;
    mutable std::condition_variable _cv;
    std::atomic<int> _count{ 0 };
};