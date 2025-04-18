#pragma once

#include "BaseStorage.h"
#include "command.h"
#include "utils.h"

class CallbackDispatcher {
public:
    static CallbackDispatcher& get();
    void submit(std::unique_ptr<ICommand> command);
    void setDB(const std::string& db_file_name);
    void setClouds(const std::unordered_map<int, std::shared_ptr<BaseStorage>>& clouds);

private:
    CallbackDispatcher();
    ~CallbackDispatcher();

    CallbackDispatcher(const CallbackDispatcher&) = delete;
    CallbackDispatcher& operator=(const CallbackDispatcher&) = delete;

    CallbackDispatcher(CallbackDispatcher&&) noexcept = delete;
    CallbackDispatcher& operator=(CallbackDispatcher&&) noexcept = delete;

    void worker();

    std::unordered_map<int, std::shared_ptr<BaseStorage>> _clouds;

    std::atomic<bool> _should_stop{ false };
    RequestQueue<std::unique_ptr<ICommand>> _queue;

    std::unique_ptr<std::thread> _worker;

    std::unique_ptr<Database> _db;
};