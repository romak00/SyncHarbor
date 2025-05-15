#include "utils.h"
#include "command.h"

Change::Change(
    ChangeTypeFlags t,
    std::time_t ct,
    std::unique_ptr<ICommand> fcmd
) :
    _type(t),
    _change_time(ct),
    _first_cmd(std::move(fcmd)),
    _pending_cmds(0),
    _status(Status::Pending)
{
}

Change::Change(Change&& other) noexcept
    : _type(other._type)
    , _change_time(other._change_time)
    , _status(other._status)
    , _first_cmd(std::move(other._first_cmd))
    , _dependents(std::move(other._dependents))
    , _on_complete(std::move(other._on_complete))
    , _pending_cmds(other._pending_cmds.load(std::memory_order_relaxed))
{
}

Change& Change::operator=(Change&& other) noexcept {
    if (this != &other) {
        _type = other._type;
        _change_time = other._change_time;
        _status = other._status;

        _first_cmd = std::move(other._first_cmd);
        _dependents = std::move(other._dependents);
        _on_complete = std::move(other._on_complete);

        _pending_cmds.store(
            other._pending_cmds.load(std::memory_order_relaxed),
            std::memory_order_relaxed
        );
    }
    return *this;
}

void Change::setOnComplete(std::function<void(std::vector<std::unique_ptr<Change>>&& dependents)> cb) {
    std::lock_guard lk(_mtx);
    _on_complete = std::move(cb);
}

void Change::onCommandCreated() noexcept {
    _pending_cmds.fetch_add(1, std::memory_order_acq_rel);
}

void Change::onCommandFinished() noexcept {
    if (_pending_cmds.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        std::lock_guard lk(_mtx);
    
        _status = Status::Completed;
        _on_complete(std::move(_dependents));
    }
}

void Change::addDependent(std::unique_ptr<Change> change) {
    std::lock_guard lk(_mtx);
    _dependents.emplace_back(std::move(change));
}