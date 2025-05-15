#pragma once

#include "utils.h"

class ChangeFactory {
public:
    template <typename Command, typename DTO, typename... Args>
    static std::unique_ptr<Change> create(
        ChangeTypeFlags change_flags,
        std::time_t time,
        int cloud_id,
        Args&&... dto_args
    ) {
        auto dto = std::make_unique<DTO>(std::forward<Args>(dto_args)...);
        auto cmd = std::make_unique<Command>(cloud_id);
        cmd->setDTO(std::move(dto));
        return std::make_unique<Change>(change_flags, time, std::move(cmd));
    }

    template <typename Command, typename DTO, typename... Args>
    static std::unique_ptr<Change> createInitialNew(
        std::time_t time,
        int cloud_id,
        std::function<void(std::vector<std::unique_ptr<Change>>&& dependents)> on_complete_cb,
        Args&&... dto_args
    ) {
        auto cmd = std::make_unique<Command>(cloud_id);
        cmd->setDTO(std::make_unique<DTO>(std::forward<DTO>(dto)));

        if constexpr (std::is_same_v<Command, CloudDownloadNewCommand>) {
            auto local_cmd = std::make_unique<LocalUploadCommand>(0);
            cmd->addNext(std::move(local_cmd));
        }
        else if constexpr (std::is_same_v<Command, CloudDownloadUpdateCommand>) {
            auto local_cmd = std::make_unique<LocalUpdateCommand>(0);
            cmd->addNext(std::move(local_cmd));
        }

        auto change = std::make_unique<Change>(flags, timestamp, std::move(cmd));
        change->setOnComplete(on_complete_cb);

        change->onCommandCreated();
        change->onCommandCreated();

        return change;
    }
};