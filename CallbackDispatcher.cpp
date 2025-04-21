#include "CallbackDispatcher.h"
#include "command.h"
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
    _should_stop = true;
    _queue.notify();
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
        command->getTargetFile(),
        CloudResolver::getName(command->getId())
    );
    _queue.push(std::move(command));
}

void CallbackDispatcher::worker() {
    ThreadNamer::setThreadName("CallbackWorker");
    while (!_should_stop || !_queue.empty()) {
        std::unique_ptr<ICommand> command;
        while (_queue.pop(command, [&]() { return _should_stop.load(); })) {
            int cloud_id = command->getId();
            LOG_DEBUG(
                "CallbackDispatcher",
                "Callback started for file: %s and cloud: %s",
                command->getTargetFile(),
                CloudResolver::getName(command->getId())
            );
            command->completionCallback(*_db, *_clouds[cloud_id]);
        }
    }
}