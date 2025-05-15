#pragma once

#include "BaseStorage.h"
#include "utils.h"

class ICommand;

class CallbackDispatcher {
public:
    static CallbackDispatcher& get();
    void submit(std::unique_ptr<ICommand> command);
    void setDB(const std::string& db_file_name);
    void setClouds(const std::unordered_map<int, std::shared_ptr<BaseStorage>>& clouds);
private:
    friend class HttpClient;

    CallbackDispatcher();
    ~CallbackDispatcher() = default;

    void finish();

    CallbackDispatcher(const CallbackDispatcher&) = delete;
    CallbackDispatcher& operator=(const CallbackDispatcher&) = delete;

    CallbackDispatcher(CallbackDispatcher&&) noexcept = delete;
    CallbackDispatcher& operator=(CallbackDispatcher&&) noexcept = delete;

    void worker();

    std::unordered_map<int, std::shared_ptr<BaseStorage>> _clouds;

    std::atomic<bool> _should_stop{ false };
    ThreadSafeQueue<std::unique_ptr<ICommand>> _queue;

    ActiveCount _active_count;

    std::unique_ptr<std::thread> _worker;

    std::unique_ptr<Database> _db;
};