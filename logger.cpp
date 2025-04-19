#include "logger.h"
#include <iostream>
#include <filesystem>

std::map<std::thread::id, std::string> ThreadNamer::_thread_names;
std::mutex ThreadNamer::_mutex;

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

Logger& Logger::get() {
    static Logger instance;
    return instance;
}

void Logger::addLogFile(const std::string& name, const std::string& filename) {
    std::lock_guard<std::mutex> lock(_mutex);
    LogDestination dest;
    dest.stream.open(filename, std::ios::out | std::ios::app);
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
        << " [" << std::setw(7) << levelToString(level) << "]"
        << " [Thread:" << std::setw(15) << std::left << ThreadNamer::getThreadName() << "]"
        << " [" << std::setw(20) << std::left << component << "] "
        << message;
    return ss.str();
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

template<typename... Args>
void Logger::log(LogLevel level, const std::string& component, const std::string& format, Args... args) {
    if (level < _global_level) return;

    size_t size = snprintf(nullptr, 0, format.c_str(), args...) + 1;
    std::unique_ptr<char[]> buf(new char[size]);
    snprintf(buf.get(), size, format.c_str(), args...);
    std::string message(buf.get(), buf.get() + size - 1);

    std::string formatted = formatMessage(level, component, message);

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

void OperationContext::updateStatus(const std::string& status, const std::string& details = "") {
    Logger::get().log(LogLevel::INFO, _component,
        "[%s][%s] %s: %s",
        _operationType.c_str(),
        _fileInfo.c_str(),
        status.c_str(),
        details.c_str());
}

OperationContext::~OperationContext() {
    Logger::get().log(LogLevel::INFO, _component,
        "[%s][%s] Operation COMPLETED",
        _operationType.c_str(),
        _fileInfo.c_str());
}

OperationContext::OperationContext(
    const std::string& operationType,
    const std::string& component,
    const std::string& fileInfo = ""
)
    :
    _operationType(operationType),
    _component(component),
    _fileInfo(fileInfo)
{
    Logger::get().log(LogLevel::INFO, _component,
        "[%s][%s] Operation STARTED",
        _operationType.c_str(),
        _fileInfo.c_str());
}