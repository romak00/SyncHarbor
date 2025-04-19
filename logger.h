#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <map>
#include <atomic>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
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

    template<typename... Args>
    void log(LogLevel level, const std::string& component, const std::string& format, Args... args);

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();
    ~Logger();

    std::string formatMessage(LogLevel level, const std::string& component, const std::string& message);
    std::string levelToString(LogLevel level);
    std::string getTimestamp();

    struct LogDestination {
        std::ofstream stream;
        LogLevel level;
    };

    std::mutex _mutex;
    std::map<std::string, LogDestination> _logs;
    bool _console_enabled = false;
    LogLevel _global_level = LogLevel::INFO;
};

enum class OperationStatus {
    STARTED,
    IN_PROGRESS,
    COMPLETED,
    FAILED
};

class OperationContext {
public:
    OperationContext(
        const std::string& operationType,
        const std::string& component,
        const std::string& fileInfo = "");

    ~OperationContext();

    void updateStatus(const std::string& status, const std::string& details = "");

private:
    std::string _operationType;
    std::string _component;
    std::string _fileInfo;
};


#define TRACE_FILE_OP(opType, component, filePath) \
    OperationContext _op_ctx_##__LINE__(opType, component, \
        std::filesystem::path(filePath).filename().string())

#define TRACE_GENERIC_OP(opType, component) \
    OperationContext _op_ctx_##__LINE__(opType, component)

#define LOG_OP_STATUS(status, details) \
    _op_ctx_##__LINE__.updateStatus(status, details)

#define LOG_DEBUG(component, ...)           Logger::get().log(LogLevel::DEBUG, component, __VA_ARGS__)
#define LOG_INFO(component, ...)            Logger::get().log(LogLevel::INFO, component, __VA_ARGS__)
#define LOG_WARNING(component, ...)         Logger::get().log(LogLevel::WARNING, component, __VA_ARGS__)
#define LOG_ERROR(component, ...)           Logger::get().log(LogLevel::ERROR, component, __VA_ARGS__)