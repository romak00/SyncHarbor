#include "event-registry.h"

void ThreadSafeEventsRegistry::add(const std::string& path, ChangeType ct) {
    std::lock_guard<std::mutex> lock(_mutex);
    _map.emplace(path, ct);
}

void ThreadSafeEventsRegistry::add(const std::filesystem::path& path, ChangeType ct) {
    std::lock_guard<std::mutex> lock(_mutex);
    _map.emplace(path.string(), ct);
}

bool ThreadSafeEventsRegistry::check(const std::string& path, ChangeType ct) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_map.contains(path) && _map[path] == ct) {
        _map.erase(path);
        return true;
    }
    return false;
}

bool ThreadSafeEventsRegistry::check(const std::filesystem::path& path, ChangeType ct) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_map.contains(path.string()) && _map[path.string()] == ct) {
        _map.erase(path.string());
        return true;
    }
    return false;
}

std::unordered_map<std::string, ChangeType> ThreadSafeEventsRegistry::copyMap() {
    std::lock_guard<std::mutex> lock(_mutex);
    auto map = _map;
    _map.clear();
    return map;
}

PrevEventsRegistry::PrevEventsRegistry(const std::unordered_map<std::string, ChangeType>& map) : _map(map)
{
}

void PrevEventsRegistry::add(const std::string& path, ChangeType ct) {
    _map.emplace(path, ct);
}

void PrevEventsRegistry::add(const std::filesystem::path& path, ChangeType ct) {
    _map.emplace(path.string(), ct);
}

bool PrevEventsRegistry::check(const std::string& path, ChangeType ct) {
    if (_map.contains(path) && _map[path] == ct) {
        _map.erase(path);
        return true;
    }
    return false;
}

bool PrevEventsRegistry::check(const std::filesystem::path& path, ChangeType ct) {
    if (_map.contains(path.string()) && _map[path.string()] == ct) {
        _map.erase(path.string());
        return true;
    }
    return false;
}