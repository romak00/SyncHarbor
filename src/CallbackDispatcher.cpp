#include "CallbackDispatcher.h"
#include "commands.h"
#include "logger.h"


CallbackDispatcher& CallbackDispatcher::get() {
    static CallbackDispatcher instance;
    return instance;
}

CallbackDispatcher::CallbackDispatcher() {
    _worker = std::make_unique<std::thread>(&CallbackDispatcher::worker, this);
    LOG_INFO("CallbackDispatcher", "initialized. Worker thread started");
}

void CallbackDispatcher::finish() {
    LOG_INFO("CallbackDispatcher", "Shutting down dispatcher...");
    _should_stop.store(true, std::memory_order_release);
    _queue.close();
    if (_worker && _worker->joinable()) {
        _worker->join();
    }
}

void CallbackDispatcher::setDB(const std::string& db_file_name) {
    _db = std::make_unique<Database>(db_file_name);
}

void CallbackDispatcher::setClouds(const std::unordered_map<int, std::shared_ptr<BaseStorage>>& clouds) {
    _clouds = clouds;
}

void CallbackDispatcher::submit(std::unique_ptr<ICommand> command) {
    LOG_DEBUG(
        "CallbackDispatcher",
        "Callback submited for file: %s and cloud: %s",
        command->getTarget(),
        CloudResolver::getName(command->getId())
    );
    _active_count.increment();
    _queue.push(std::move(command));
}

void CallbackDispatcher::worker() {
    ThreadNamer::setThreadName("CallbackWorker");
    std::unique_ptr<ICommand> command;
    while (_queue.pop(command)) {
        int cloud_id = command->getId();
        LOG_DEBUG(
            "CallbackDispatcher",
            "Callback started for file: %s and cloud: %s",
            command->getTarget(),
            CloudResolver::getName(command->getId())
        );
        std::lock_guard lock(_db_mutex);
        command->completionCallback(_db, _clouds[cloud_id]);
        if (command->needRepeat()) {
            HttpClient::get().submit(std::move(command));
        }
        _active_count.decrement();
    }
}

bool CallbackDispatcher::isIdle() const noexcept {
    return _active_count.isIdle();
}

void CallbackDispatcher::waitUntilIdle() const {
    _active_count.waitUntilIdle();
}

void CallbackDispatcher::syncDbWrite(const std::unique_ptr<FileRecordDTO>& dto) {
    CallbackDispatcher::get().waitUntilIdle();
    std::lock_guard lock(_db_mutex);

    if (dto->cloud_id == 0) {
        int global_id = _db->add_file(*dto);
        dto->global_id = global_id;
    }
    else {
        _db->add_file_link(*dto);
    }
}

void CallbackDispatcher::syncDbWrite(const std::vector<std::unique_ptr<FileRecordDTO>>& vec_dto) {
    CallbackDispatcher::get().waitUntilIdle();
    std::lock_guard lock(_db_mutex);

    for (auto& dto : vec_dto) {
        if (dto->cloud_id == 0) {
            int global_id = _db->add_file(*dto);
            dto->global_id = global_id;
        }
        else {
            _db->add_file_link(*dto);
        }
    }
}