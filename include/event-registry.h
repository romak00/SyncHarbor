#pragma once

#include <mutex>
#include <unordered_map>
#include <string>
#include <filesystem>

enum class ChangeType : uint8_t;

class ThreadSafeEventsRegistry {
public:

    ThreadSafeEventsRegistry() = default;
    ~ThreadSafeEventsRegistry() = default;

    void add(const std::string& path, ChangeType ct);
    void add(const std::filesystem::path& path, ChangeType ct);

    bool check(const std::string& path, ChangeType ct);
    bool check(const std::filesystem::path& path, ChangeType ct);

    std::unordered_map<std::string, ChangeType> copyMap();

private:
    std::unordered_map<std::string, ChangeType> _map;
    std::mutex _mutex;
};

class PrevEventsRegistry {
public:

    PrevEventsRegistry(const std::unordered_map<std::string, ChangeType>& map);
    PrevEventsRegistry() = delete;
    ~PrevEventsRegistry() = default;

    void add(const std::string& path, ChangeType ct);
    void add(const std::filesystem::path& path, ChangeType ct);

    bool check(const std::string& path, ChangeType ct);
    bool check(const std::filesystem::path& path, ChangeType ct);

private:
    std::unordered_map<std::string, ChangeType> _map;
};