#include "change.h"

Change::Change(
    ChangeType t,
    const std::filesystem::path& target_path,
    std::time_t ct,
    const int cloud_id
) :
    _target_path(target_path),
    _pending_cmds(0),
    _change_time(ct),
    _cloud_id(cloud_id),
    _status(Status::Pending),
    _type(t)
{
}

Change::Change(
    ChangeType t,
    const std::filesystem::path& target_path,
    std::time_t ct,
    const int cloud_id,
    std::vector<std::unique_ptr<ICommand>> fcmds,
    const int cmds
) :
    _cmd_chain(std::move(fcmds)),
    _target_path(std::move(target_path)),
    _pending_cmds(cmds),
    _change_time(ct),
    _cloud_id(cloud_id),
    _status(Status::Pending),
    _type(t)
{
}

Change::Change(
    ChangeType t,
    const std::filesystem::path& target_path,
    std::time_t ct,
    const int cloud_id,
    std::unique_ptr<ICommand> fcmds,
    const int cmds
) :
    _target_path(target_path),
    _pending_cmds(cmds),
    _change_time(ct),
    _cloud_id(cloud_id),
    _status(Status::Pending),
    _type(t)
{
    _cmd_chain.clear();
    _cmd_chain.push_back(std::move(fcmds));
}

Change::Change(Change&& other) noexcept
    : _dependents(std::move(other._dependents))
    , _cmd_chain(std::move(other._cmd_chain))
    , _target_path(std::move(other._target_path))
    , _on_complete(std::move(other._on_complete))
    , _pending_cmds(other._pending_cmds.load(std::memory_order_relaxed))
    , _change_time(other._change_time)
    , _cloud_id(other._cloud_id)
    , _status(other._status)
    , _type(other._type)
{
}

auto Change::operator=(Change&& other) noexcept -> Change& {
    if (this != &other) {
        _type = other._type;
        _change_time = other._change_time;
        _status = other._status;
        _cloud_id = other._cloud_id;

        _cmd_chain = std::move(other._cmd_chain);
        _dependents = std::move(other._dependents);
        _on_complete = std::move(other._on_complete);
        _target_path = std::move(other._target_path);

        _pending_cmds.store(
            other._pending_cmds.load(std::memory_order_relaxed),
            std::memory_order_relaxed
        );
    }
    return *this;
}

void Change::setOnComplete(std::function<void(std::vector<std::shared_ptr<Change>>&& dependents)> cb) {
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

void Change::addDependent(std::shared_ptr<Change> change) {
    std::lock_guard lk(_mtx);
    _dependents.emplace_back(std::move(change));
}

auto Change::getTime() const -> std::time_t {
    return _change_time;
}

auto Change::getTargetPath() const -> std::filesystem::path {
    return _target_path;
}

auto Change::getType() const -> ChangeType {
    return _type;
}

auto Change::getCloudId() const -> int {
    return _cloud_id;
}

void Change::dispatch() {
    ICommand* cmd = _cmd_chain[0].get();

    if (auto* local = dynamic_cast<LocalCommand*>(cmd)) {
        for (auto& lcmd : _cmd_chain) {
            CallbackDispatcher::get().submit(std::move(lcmd));
        }
    }
    else {
        for (auto& ccmd : _cmd_chain) {
            HttpClient::get().submit(std::move(ccmd));
        }
    }
}

auto Change::getTargetType() const -> EntryType {
    return _cmd_chain.empty() ? EntryType::Null : _cmd_chain[0]->getTargetType();
}

void Change::onCancel() noexcept {
    _status = Status::Cancelled;
}

void Change::setCmdChain(std::vector<std::unique_ptr<ICommand>> cmds) {
    _cmd_chain = std::move(cmds);
}
void Change::setCmdChain(std::unique_ptr<ICommand> cmd) {
    _cmd_chain.push_back(std::move(cmd));
}