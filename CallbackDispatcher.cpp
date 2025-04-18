#include "CallbackDispatcher.h"

CallbackDispatcher& CallbackDispatcher::get() {
    static CallbackDispatcher instance;
    return instance;
}

CallbackDispatcher::CallbackDispatcher() {
    _worker = std::make_unique<std::thread>(&CallbackDispatcher::worker, this);
}

CallbackDispatcher::~CallbackDispatcher() {
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
    _queue.push(std::move(command));
}

void CallbackDispatcher::worker() {
    while (!_should_stop || !_queue.empty()) {
        std::unique_ptr<ICommand> command;
        while (_queue.pop(command)) {
            int cloud_id = command->getId();
            command->completionCallback(*_db, *_clouds[cloud_id]);
        }
    }
}

