#pragma once

#include "command.h"

class CommandChainFactory {
    template <typename Command, typename DTO, typename... Args>
    static std::unique_ptr<Command> create(
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
};