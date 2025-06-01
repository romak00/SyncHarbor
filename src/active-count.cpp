#include "active-count.h"

void ActiveCount::increment() noexcept {
    _count.fetch_add(1, std::memory_order_acq_rel);
}

void ActiveCount::decrement() noexcept {
    if (_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        std::lock_guard<std::mutex> lk(_mtx);
        _cv.notify_all();
    }
}

int ActiveCount::get() const noexcept {
    return _count.load(std::memory_order_acquire);
}

bool ActiveCount::isIdle() const noexcept {
    return get() == 0;
}

void ActiveCount::waitUntilIdle() const {
    std::unique_lock<std::mutex> lk(_mtx);
    _cv.wait(lk, [&] {
        return _count.load(std::memory_order_acquire) == 0;
        });
}