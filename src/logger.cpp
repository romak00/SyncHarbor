#include "logger.h"
#include <iostream>
#include <filesystem>

std::map<std::thread::id, std::string> ThreadNamer::_thread_names;
std::mutex ThreadNamer::_mutex;

std::unordered_map<int, std::string> CloudResolver::_cloudNames;
std::mutex CloudResolver::_mutex;

void ThreadNamer::setThreadName(const std::string& name) {
    std::lock_guard<std::mutex> lock(_mutex);
    _thread_names[std::this_thread::get_id()] = name;
}

std::string ThreadNamer::getThreadName() {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _thread_names.find(std::this_thread::get_id());
    return it != _thread_names.end() ? it->second : "unknown";
}

Logger::Logger() {
    addLogFile("debug", "cloudsync.debug.log");
    addLogFile("error", "cloudsync.error.log");
    addLogFile("general", "cloudsync.log");
}

Logger::~Logger() {
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto& [name, dest] : _logs) {
        if (dest.stream.is_open()) {
            dest.stream.close();
        }
    }
}

void Logger::log(LogLevel level, const std::string& component, const std::string& message) {
    if (level < _global_level) return;
    std::string formatted = formatMessage(level, component, message);
    output(formatted, level);
}

Logger& Logger::get() {
    static Logger instance;
    return instance;
}

void Logger::addLogFile(const std::string& name, const std::string& filename) {
    std::lock_guard<std::mutex> lock(_mutex);
    LogDestination dest;
    dest.stream.open(filename, std::ios::out);
    dest.level = _global_level;
    _logs[name] = std::move(dest);
}

void Logger::setConsoleLogging(bool enable) {
    std::lock_guard<std::mutex> lock(_mutex);
    _console_enabled = enable;
}

void Logger::setGlobalLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(_mutex);
    _global_level = level;
}

void Logger::setLogLevelFor(const std::string& logName, LogLevel level) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_logs.count(logName)) {
        _logs[logName].level = level;
    }
}

std::string Logger::formatMessage(LogLevel level, const std::string& component, const std::string& message) {
    std::stringstream ss;
    ss << getTimestamp()
        << " [" << levelToString(level) << "]"
        << " [Thread:"<< std::left << ThreadNamer::getThreadName() << "]"
        << " [" << std::left << component << "] "
        << message;
    return ss.str();
}

void Logger::output(const std::string& formatted, LogLevel level) {
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto& [name, dest] : _logs) {
        if (level >= dest.level) {
            dest.stream << formatted << std::endl;
        }
    }
    if (_console_enabled) {
        std::cout << formatted << std::endl;
    }
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
    case LogLevel::DEBUG:   return "DEBUG";
    case LogLevel::INFO:    return "INFO";
    case LogLevel::WARNING: return "WARNING";
    case LogLevel::ERROR:   return "ERROR";
    default:                return "UNKNOWN";
    }
}

std::string Logger::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}