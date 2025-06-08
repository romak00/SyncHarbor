#pragma once

#include "change.h"
#include "commands.h"
#include "BaseStorage.h"

template<class> inline constexpr bool always_false_v = false;

class ChangeFactory {
public:
    static void initClouds(const std::unordered_map<int, std::shared_ptr<BaseStorage>>& clouds) {
        _clouds = clouds;
    }

    template<
        typename InitialCmd,
        typename DTO,
        typename... NextCmds
    >
    static std::shared_ptr<Change> make(
        ChangeType type,
        std::time_t time,
        const std::filesystem::path& path,
        std::unique_ptr<DTO> dto,
        int src_cloud_id
    ) {
        auto first_cmd = std::make_unique<InitialCmd>(std::is_base_of_v<LocalCommand, InitialCmd> ? 0 : src_cloud_id);
        first_cmd->setDTO(std::make_unique<DTO>(*dto));

        auto change = std::make_shared<Change>(type, path, time, src_cloud_id);

        first_cmd->setOwner(change);

        ICommand* parent = first_cmd.get();

        ((parent = addNextCommands<NextCmds, DTO>(change, parent, src_cloud_id)), ...);

        change->setCmdChain(std::move(first_cmd));
        return change;
    }

    static std::shared_ptr<Change> makeCloudNew(std::unique_ptr<FileRecordDTO> dto) {
        auto mtime = dto->cloud_file_modified_time;
        auto rp = dto->rel_path;
        auto cloud_id = dto->cloud_id;
        return make<
            CloudDownloadNewCommand, FileRecordDTO,
            LocalUploadCommand, CloudUploadCommand
        >(ChangeType::New,
            mtime,
            rp,
            std::move(dto),
            cloud_id);
    }

    static std::shared_ptr<Change> makeLocalNew(std::unique_ptr<FileRecordDTO> dto) {
        auto mtime = dto->cloud_file_modified_time;
        auto rp = dto->rel_path;
        return make<
            LocalUploadCommand, FileRecordDTO,
            CloudUploadCommand
        >(ChangeType::New,
            mtime,
            rp,
            std::move(dto),
            0);
    }
    static std::shared_ptr<Change> makeCloudMove(std::unique_ptr<FileMovedDTO> dto) {
        auto mtime = dto->cloud_file_modified_time;
        auto rp = dto->old_rel_path;
        auto cloud_id = dto->cloud_id;
        return make<
            LocalMoveCommand, FileMovedDTO,
            CloudMoveCommand
        >(ChangeType::Move,
            mtime,
            rp,
            std::move(dto),
            cloud_id);
    }
    static std::shared_ptr<Change> makeLocalMove(std::unique_ptr<FileMovedDTO> dto) {
        auto mtime = dto->cloud_file_modified_time;
        auto rp = dto->old_rel_path;
        return make<
            LocalMoveCommand, FileMovedDTO,
            CloudMoveCommand
        >(ChangeType::Move,
            mtime,
            rp,
            std::move(dto),
            0);
    }
    static std::shared_ptr<Change> makeCloudUpdate(std::unique_ptr<FileUpdatedDTO> dto) {
        auto mtime = dto->cloud_file_modified_time;
        auto rp = dto->rel_path;
        auto cloud_id = dto->cloud_id;
        return make<
            CloudDownloadUpdateCommand, FileUpdatedDTO,
            LocalUpdateCommand, CloudUpdateCommand
        >(ChangeType::Update,
            mtime,
            rp,
            std::move(dto),
            cloud_id);
    }
    static std::shared_ptr<Change> makeLocalUpdate(std::unique_ptr<FileUpdatedDTO> dto) {
        auto mtime = dto->cloud_file_modified_time;
        auto rp = dto->rel_path;
        return make<
            LocalUpdateCommand, FileUpdatedDTO,
            CloudUpdateCommand
        >(ChangeType::Update,
            mtime,
            rp,
            std::move(dto),
            0);
    }
    static std::shared_ptr<Change> makeDelete(std::unique_ptr<FileDeletedDTO> dto) {
        auto mtime = dto->when;
        auto rp = dto->rel_path;
        auto cloud_id = dto->cloud_id;
        return make<
            LocalDeleteCommand, FileDeletedDTO,
            CloudDeleteCommand
        >(ChangeType::Delete,
            mtime,
            rp,
            std::move(dto),
            cloud_id);
    }

private:
    static std::unordered_map<int, std::shared_ptr<BaseStorage>> _clouds;

    template<typename Cmd, typename DTO_t>
    static ICommand* addNextCommands(
        std::shared_ptr<Change>     change,
        ICommand*                   parent,
        int                         src_cloud_id
    ) {
        ICommand* child = parent;
        if constexpr (std::is_base_of_v<LocalCommand, Cmd>) {
            auto cmd = std::make_unique<Cmd>(0);
            cmd->setOwner(change);
            child = cmd.get();
            parent->addNext(std::move(cmd));
        }
        else if constexpr (std::is_base_of_v<CloudCommand, Cmd>) {
            for (auto& [cid, _] : _clouds) {
                if (cid == src_cloud_id || cid == 0) {
                    continue;
                }
                auto cmd = std::make_unique<Cmd>(cid);
                cmd->setOwner(change);
                child = cmd.get();
                parent->addNext(std::move(cmd));
            }
        }
        else {
            static_assert(always_false_v<Cmd>,
                "Cmd must derive from LocalCommand or CloudCommand");
        }
        return child;
    }
};

inline std::unordered_map<int, std::shared_ptr<BaseStorage>> ChangeFactory::_clouds;
