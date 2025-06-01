// tests/unit/EventRegistryTests.cpp

#include <gtest/gtest.h>
#include "event-registry.h"
#include "utils.h"
#include <filesystem>
#include <unordered_map>

using namespace std::filesystem;

TEST(EventRegistryUnitTest, AddAndCheckByString) {
    ThreadSafeEventsRegistry reg;
    std::string str = "foo";
    EXPECT_FALSE(reg.check(str, ChangeType::New));
    reg.add(str, ChangeType::New);
    EXPECT_TRUE(reg.check(str, ChangeType::New));
    EXPECT_FALSE(reg.check(str, ChangeType::New));
}

TEST(EventRegistryUnitTest, AddAndCheckByPath) {
    ThreadSafeEventsRegistry reg;
    path p = "bar.txt";
    EXPECT_FALSE(reg.check(p, ChangeType::Update));
    reg.add(p, ChangeType::Update);
    EXPECT_TRUE(reg.check(p, ChangeType::Update));
    EXPECT_FALSE(reg.check(p, ChangeType::Update));
}

TEST(EventRegistryUnitTest, CheckWrongTypeDoesNotRemoveString) {
    ThreadSafeEventsRegistry reg;
    std::string str("baz");
    reg.add(str, ChangeType::Delete);
    EXPECT_FALSE(reg.check(str, ChangeType::New));
    EXPECT_TRUE(reg.check(str, ChangeType::Delete));
}

TEST(EventRegistryUnitTest, CheckWrongTypeDoesNotRemovePath) {
    ThreadSafeEventsRegistry reg;
    path p = "baz";
    reg.add(p, ChangeType::Delete);
    EXPECT_FALSE(reg.check(p, ChangeType::New));
    EXPECT_TRUE(reg.check(p, ChangeType::Delete));
}

TEST(EventRegistryUnitTest, CopyMapClearsRegistry) {
    ThreadSafeEventsRegistry reg;
    reg.add(std::string{ "a" }, ChangeType::New);
    reg.add(path{ "b" }, ChangeType::Move);
    reg.add(std::string{ "c" }, ChangeType::Rename);

    auto m = reg.copyMap();
    EXPECT_EQ(m.size(), 3u);
    EXPECT_EQ(m.at("a"), ChangeType::New);
    EXPECT_EQ(m.at("b"), ChangeType::Move);
    EXPECT_EQ(m.at("c"), ChangeType::Rename);

    EXPECT_FALSE(reg.check(std::string{ "a" }, ChangeType::New));
    EXPECT_FALSE(reg.check(path {"b" }, ChangeType::Move));
    EXPECT_FALSE(reg.check(std::string{ "c" }, ChangeType::Rename));

    auto empty = reg.copyMap();
    EXPECT_TRUE(empty.empty());
}

TEST(EventRegistryUnitTest, InitialMapAndCheck) {
    std::unordered_map<std::string, ChangeType> init = {
        {"x", ChangeType::New},
        {"y", ChangeType::Update}
    };
    PrevEventsRegistry prev(init);

    EXPECT_TRUE(prev.check(std::string{ "x" }, ChangeType::New));
    EXPECT_TRUE(prev.check(std::string{ "y" }, ChangeType::Update));
    EXPECT_FALSE(prev.check(std::string{ "x" }, ChangeType::New));
    EXPECT_FALSE(prev.check(std::string{ "y" }, ChangeType::Update));
}

TEST(EventRegistryUnitTest, AddAndCheckByStringAndPath) {
    PrevEventsRegistry prev(std::unordered_map<std::string, ChangeType>{});
    prev.add(std::string{ "foo" }, ChangeType::Move);
    EXPECT_TRUE(prev.check(std::string{ "foo" }, ChangeType::Move));
    EXPECT_FALSE(prev.check(std::string{ "foo" }, ChangeType::Move));

    path p = "bar";
    prev.add(p, ChangeType::Rename);
    EXPECT_TRUE(prev.check(p, ChangeType::Rename));
    EXPECT_FALSE(prev.check(p, ChangeType::Rename));
}

TEST(EventRegistryUnitTest, CheckWrongTypeDoesNotRemove) {
    PrevEventsRegistry prev(std::unordered_map<std::string, ChangeType>{});
    prev.add(std::string{ "oops" }, ChangeType::Delete);
    EXPECT_FALSE(prev.check(std::string{ "oops" }, ChangeType::New));
    EXPECT_TRUE(prev.check(std::string{ "oops" }, ChangeType::Delete));
}
