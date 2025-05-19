#pragma once

#include "change.h"
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
    static std::unique_ptr<Change> make(
        ChangeType                      type,
        std::time_t                     time,
        const std::filesystem::path&    path,
        std::unique_ptr<DTO>            dto,
        int                             src_cloud_id
    ) {
        auto headCmd = std::make_unique<InitialCmd>(src_cloud_id);
        headCmd->setDTO(std::make_unique<DTO>(*dto));

        auto change = std::make_unique<Change>(type, path, time, src_cloud_id);

        attachChangeCallbacks(change.get(), headCmd.get());

        std::vector<ICommand*> parents;
        parents.push_back(headCmd.get());

        ((parents = addNextCommands<NextCmds, DTO>(change.get(), parents, src_cloud_id)), ...);

        change->setCmdChain(std::move(headCmd));
        return change;
    }

    static std::unique_ptr<Change> makeCloudNew(std::unique_ptr<FileRecordDTO> dto) {
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

    static std::unique_ptr<Change> makeLocalNew(std::unique_ptr<FileRecordDTO> dto) {
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
    static std::unique_ptr<Change> makeCloudMove(std::unique_ptr<FileMovedDTO> dto) {
        auto mtime = dto->cloud_file_modified_time;
        auto rp = dto->old_rel_path;
        auto cloud_id = dto->cloud_id;
        return make<
            CloudMoveCommand, FileMovedDTO,
            LocalMoveCommand, CloudMoveCommand
        >(ChangeType::Move,
            mtime,
            rp,
            std::move(dto),
            cloud_id);
    }
    static std::unique_ptr<Change> makeLocalMove(std::unique_ptr<FileMovedDTO> dto) {
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
    static std::unique_ptr<Change> makeCloudUpdate(std::unique_ptr<FileUpdatedDTO> dto) {
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
    static std::unique_ptr<Change> makeLocalUpdate(std::unique_ptr<FileUpdatedDTO> dto) {
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
    static std::unique_ptr<Change> makeDelete(std::unique_ptr<FileDeletedDTO> dto) {
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

    static void attachChangeCallbacks(Change* change, ICommand* cmd) {
        cmd->setChangeCallbacks(
            [c = change]() noexcept { c->onCommandCreated(); },
            [c = change]() noexcept { c->onCommandFinished(); },
            [c = change]() noexcept { c->onCancel(); }
        );
    }

private:
    static std::unordered_map<int, std::shared_ptr<BaseStorage>> _clouds;

    template<typename Cmd, typename DTO_t>
    static std::vector<ICommand*> addNextCommands(
        Change*                         change,
        const std::vector<ICommand*>&   parents,
        int                             src_cloud_id
    ) {
        std::vector<ICommand*> children;
        if constexpr (std::is_base_of_v<LocalCommand, Cmd>) {
            for (auto* parent : parents) {
                auto cmd = std::make_unique<Cmd>(0);
                attachChangeCallbacks(change, cmd.get());
                children.push_back(cmd.get());
                parent->addNext(std::move(cmd));
            }
        }
        else if constexpr (std::is_base_of_v<CloudCommand, Cmd>) {
            for (auto* parent : parents) {
                for (auto& [cid, _] : _clouds) {
                    if (cid == src_cloud_id) continue;
                    auto cmd = std::make_unique<Cmd>(cid);
                    attachChangeCallbacks(change, cmd.get());
                    children.push_back(cmd.get());
                    parent->addNext(std::move(cmd));
                }
            }
        }
        else {
            static_assert(always_false_v<Cmd>,
                "Cmd must derive from LocalCommand or CloudCommand");
        }
        return children;
    }
};

inline std::unordered_map<int, std::shared_ptr<BaseStorage>> ChangeFactory::_clouds;
