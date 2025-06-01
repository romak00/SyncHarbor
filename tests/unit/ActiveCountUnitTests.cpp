#include <gtest/gtest.h>
#include "active-count.h"

#include <thread>
#include <future>
#include <chrono>

using namespace std::chrono_literals;

TEST(ActiveCountUnitTest, InitialState) {
    ActiveCount ac;
    EXPECT_EQ(ac.get(), 0);
    EXPECT_TRUE(ac.isIdle());
    EXPECT_NO_THROW(ac.waitUntilIdle());
}

TEST(ActiveCountUnitTest, IncrementDecrementBasic) {
    ActiveCount ac;
    ac.increment();
    EXPECT_EQ(ac.get(), 1);
    EXPECT_FALSE(ac.isIdle());

    ac.decrement();
    EXPECT_EQ(ac.get(), 0);
    EXPECT_TRUE(ac.isIdle());
}

TEST(ActiveCountUnitTest, WaitUntilIdleBlocksAndUnblocks) {
    ActiveCount ac;
    ac.increment();
    ac.increment();
    EXPECT_EQ(ac.get(), 2);

    std::promise<void> started, done;
    auto f_started = started.get_future();
    auto f_done = done.get_future();

    std::thread waiter([&] {
        started.set_value();
        ac.waitUntilIdle();
        done.set_value();
        });

    ASSERT_EQ(f_started.wait_for(100ms), std::future_status::ready);

    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(f_done.wait_for(0ms), std::future_status::timeout);

    ac.decrement();
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(f_done.wait_for(0ms), std::future_status::timeout);

    ac.decrement();
    ASSERT_EQ(f_done.wait_for(200ms), std::future_status::ready);

    waiter.join();
    EXPECT_EQ(ac.get(), 0);
    EXPECT_TRUE(ac.isIdle());
}
