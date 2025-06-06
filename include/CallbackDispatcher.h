#pragma once

#include "BaseStorage.h"
#include "active-count.h"
#include "thread-safe-queue.h"
#include <thread>

class ICommand;

class CallbackDispatcher {
public:
    static CallbackDispatcher& get();
    void submit(std::unique_ptr<ICommand> command);
    void setDB(const std::string& db_file_name);
    void setDB(const std::shared_ptr<Database>& db);
    void setClouds(const std::unordered_map<int, std::shared_ptr<BaseStorage>>& clouds);

    bool isIdle() const noexcept;
    void waitUntilIdle() const;

    void syncDbWrite(const std::unique_ptr<FileRecordDTO>& dto);
    void syncDbWrite(const std::unique_ptr<FileUpdatedDTO>& dto);
    void syncDbWrite(const std::vector<std::unique_ptr<FileRecordDTO>>& vec_dto);

    void finish();
    void start();
private:
    friend class HttpClient;

    CallbackDispatcher();
    ~CallbackDispatcher() = default;


    CallbackDispatcher(const CallbackDispatcher&) = delete;
    CallbackDispatcher& operator=(const CallbackDispatcher&) = delete;

    CallbackDispatcher(CallbackDispatcher&&) noexcept = delete;
    CallbackDispatcher& operator=(CallbackDispatcher&&) noexcept = delete;

    void worker();

    std::unordered_map<int, std::shared_ptr<BaseStorage>> _clouds;

    ThreadSafeQueue<std::unique_ptr<ICommand>> _queue;

    std::unique_ptr<std::thread> _worker;

    std::unique_ptr<Database> _db;

    std::atomic<bool> _should_stop{ false };
    std::atomic<bool> _running{ false };

    ActiveCount _active_count;

    std::mutex _db_mutex;
};