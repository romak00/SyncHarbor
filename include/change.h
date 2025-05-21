#pragma once

#include "commands.h"

class Change {
public:
    enum class Status { Pending, Completed, Cancelled };

    Change(
        ChangeType t,
        const std::filesystem::path& target_path,
        std::time_t ct,
        const int cloud_id
    );

    Change(
        ChangeType t,
        const std::filesystem::path& target_path,
        std::time_t ct,
        const int cloud_id,
        std::vector<std::unique_ptr<ICommand>> fcmds,
        const int cmds
    );

    Change(
        ChangeType t,
        const std::filesystem::path& target_path,
        std::time_t ct,
        const int cloud_id,
        std::unique_ptr<ICommand> fcmds,
        const int cmds
    );

    Change() = delete;

    Change(const Change& other) = delete;
    Change(Change&& other) noexcept;
    Change& operator=(const Change&) = delete;
    Change& operator=(Change&& other) noexcept;

    void setOnComplete(std::function<void(std::vector<std::shared_ptr<Change>>&& dependents)> cb);
    void onCommandCreated() noexcept;
    void onCommandFinished() noexcept;
    void onCancel() noexcept;
    void addDependent(std::shared_ptr<Change> change);
    std::time_t getTime() const;
    std::filesystem::path getTargetPath() const;
    ChangeType getType() const;
    int getCloudId() const;
    void dispatch();
    EntryType getTargetType() const;

    void setCmdChain(std::unique_ptr<ICommand> cmd);
    void setCmdChain(std::vector<std::unique_ptr<ICommand>> cmds);

private:
    std::vector<std::shared_ptr<Change>> _dependents;
    std::vector<std::unique_ptr<ICommand>> _cmd_chain;
    std::filesystem::path _target_path;
    std::function<void(std::vector<std::shared_ptr<Change>>&& dependents)> _on_complete;
    std::atomic<int> _pending_cmds;
    std::time_t _change_time;
    int _cloud_id;
    std::mutex _mtx;
    std::atomic<bool> _procced = true;
    Change::Status _status;
    ChangeType _type;
};