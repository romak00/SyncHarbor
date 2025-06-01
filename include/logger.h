#pragma once

#include <string>
#include <mutex>
#include <map>
#include <thread>
#include <unordered_map>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <fstream>
#include "utils.h"

enum class LogLevel {
    DBG,
    INF,
    WRN,
    ERR
};

class ThreadNamer {
public:
    static void setThreadName(const std::string& name);
    static std::string getThreadName();

private:
    static std::mutex _mutex;
    static std::map<std::thread::id, std::string> _thread_names;
};

class Logger {
public:
    static Logger& get();

    void addLogFile(const std::string& name, const std::string& filename);
    void setConsoleLogging(bool enable);

    void setGlobalLogLevel(LogLevel level);
    void setLogLevelFor(const std::string& logName, LogLevel level);

    void log(LogLevel level, const std::string& component, const std::string& message);

    template<typename... Args>
    void log(LogLevel level, const std::string& component, const std::string& format, Args&&... args) {
        if (level < _global_level) return;

        size_t size = snprintf(nullptr, 0, format.c_str(), convertArg(std::forward<Args>(args))...) + 1;
        std::unique_ptr<char[]> buf(new char[size]);
        snprintf(buf.get(), size, format.c_str(), convertArg(std::forward<Args>(args))...);
        std::string message(buf.get(), buf.get() + size - 1);

        std::string formatted = formatMessage(level, component, message);
        output(formatted, level);
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();
    ~Logger();

    std::string formatMessage(LogLevel level, const std::string& component, const std::string& message);
    std::string levelToString(LogLevel level);
    std::string getTimestamp();

    void output(const std::string& formatted, LogLevel level);

    template<typename T>
    auto convertArg(T&& arg) -> decltype(auto) {
        if constexpr (std::is_same_v<std::decay_t<T>, std::string>) {
            return arg.c_str();
        }
        else {
            return std::forward<T>(arg);
        }
    }

    struct LogDestination {
        std::ofstream stream;
        LogLevel level;
    };

    std::mutex _mutex;
    std::map<std::string, LogDestination> _logs;
    bool _console_enabled = false;
    LogLevel _global_level = LogLevel::DBG;

};

class CloudResolver {
public:
    static void registerCloud(int id, const std::string& name) {
        std::lock_guard<std::mutex> lock(_mutex);
        _cloudNames[id] = name;
    }

    static std::string getName(int id) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _cloudNames.find(id);
        return it != _cloudNames.end() ? it->second : "unknown_cloud";
    }

private:
    static std::unordered_map<int, std::string> _cloudNames;
    static std::mutex _mutex;
};

#if CUSTOM_LOGGING_ENABLED
#define LOG_DEBUG(component, ...)           Logger::get().log(LogLevel::DBG, component, __VA_ARGS__)
#define LOG_INFO(component, ...)            Logger::get().log(LogLevel::INF, component, __VA_ARGS__)
#define LOG_WARNING(component, ...)         Logger::get().log(LogLevel::WRN, component, __VA_ARGS__)
#define LOG_ERROR(component, ...)           Logger::get().log(LogLevel::ERR, component, __VA_ARGS__)
#else
#define LOG_DEBUG(...)
#define LOG_INFO(...)
#define LOG_WARNING(...)
#define LOG_ERROR(...)
#endif