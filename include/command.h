#pragma once

#include "utils.h"
#include "Networking.h"
#include "CallbackDispatcher.h"
#include "logger.h"

class Change;

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void execute(const std::shared_ptr<BaseStorage>& cloud) = 0;
    virtual void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) = 0;
    virtual void continueChain() = 0;
    virtual void setDTO(std::unique_ptr<FileRecordDTO> dto) {}
    virtual void setDTO(std::unique_ptr<FileUpdatedDTO> dto) {}
    virtual void setDTO(std::unique_ptr<FileDeletedDTO> dto) {}
    virtual void setDTO(std::unique_ptr<FileMovedDTO> dto) {}
    virtual void addNext(std::unique_ptr<ICommand> next_command) = 0;
    virtual RequestHandle& getHandle() {
        static RequestHandle dummy;
        return dummy;
    }
    virtual std::string getTarget() const = 0;
    virtual EntryType getTargetType() const {
        return EntryType::Null;
    }
    virtual int getId() const = 0;
    virtual bool needRepeat() const {
        return false;
    }

    void setOwner(std::weak_ptr<Change> ow) noexcept {
        _owner = std::move(ow);
        if (auto ch = owner()){
            ch->onCommandCreated();
        }
    }

protected:
    std::shared_ptr<Change> owner() const noexcept {
        return _owner.lock();
    }

private:
    std::weak_ptr<Change> _owner;
};

class ChainedCommand : public ICommand {
public:
    void addNext(std::unique_ptr<ICommand> next_command) override {
        _next_commands.emplace_back(std::move(next_command));
    }
    int getId() const override {
        return _cloud_id;
    }
protected:
    std::vector<std::unique_ptr<ICommand>> _next_commands;
    int _cloud_id;
};

class CloudCommand : public ChainedCommand {
protected:
    void continueChain() override {
        for (auto& next : _next_commands) {
            CallbackDispatcher::get().submit(std::move(next));
        }
    }
};

class LocalCommand : public ChainedCommand {
protected:
    void continueChain() override {
        for (auto& next : _next_commands) {
            HttpClient::get().submit(std::move(next));
        }
    }
};

class LocalUploadCommand : public LocalCommand {
public:
    LocalUploadCommand(const int cloud_id) {
        _cloud_id = cloud_id;
    }

    ~LocalUploadCommand() = default;
    LocalUploadCommand(const LocalUploadCommand&) = delete;
    LocalUploadCommand& operator=(const LocalUploadCommand&) = delete;

    LocalUploadCommand(LocalUploadCommand&&) noexcept = default;
    LocalUploadCommand& operator=(LocalUploadCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override {

    }

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override {
        cloud->proccesUpload(_dto, "");
        for (auto& next_command : _next_commands) {
            int cloud_id = next_command->getId();
            _dto->cloud_parent_id = db->getCloudFileIdByPath(_dto->rel_path, cloud_id);
            _dto->cloud_id = cloud_id;
            next_command->setDTO(std::make_unique<FileRecordDTO>(*_dto));
        }
        LOG_INFO("LOCAL UPLOAD", this->getTarget(), "completed");

        if (auto ch = owner()) {
            ch->onCommandFinished();
        }
        continueChain();
    }
    void setDTO(std::unique_ptr<FileRecordDTO> dto) override {
        _dto = std::move(dto);
    }

    std::string getTarget() const override {
        return _dto->rel_path.string();
    }

    EntryType getTargetType() const override {
        return _dto->type;
    }

private:
    std::unique_ptr<FileRecordDTO> _dto;
};

class LocalUpdateCommand : public LocalCommand {
public:
    LocalUpdateCommand(const int cloud_id) {
        _cloud_id = cloud_id;
    }

    ~LocalUpdateCommand() = default;
    LocalUpdateCommand(const LocalUpdateCommand&) = delete;
    LocalUpdateCommand& operator=(const LocalUpdateCommand&) = delete;

    LocalUpdateCommand(LocalUpdateCommand&&) noexcept = default;
    LocalUpdateCommand& operator=(LocalUpdateCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override {}

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override {
        LOG_INFO("LOCAL UPDATE", this->getTarget(), "started");

        cloud->proccesUpdate(_dto, "");
        db->update_file(*_dto);

        for (auto& next_command : _next_commands) {
            int cloud_id = next_command->getId();
            int global_id = _dto->global_id;
            std::string cloud_file_id = db->get_cloud_file_id_by_cloud_id(cloud_id, global_id);
            _dto->cloud_file_id = cloud_file_id;
            _dto->cloud_id = cloud_id;
            next_command->setDTO(std::make_unique<FileUpdatedDTO>(*_dto));
        }
        LOG_INFO("LOCAL UPDATE", this->getTarget(), "completed");

        if (auto ch = owner()) {
            ch->onCommandFinished();
        }
        continueChain();
    }
    void setDTO(std::unique_ptr<FileUpdatedDTO> dto) override {
        _dto = std::move(dto);
    }

    std::string getTarget() const override {
        return _dto->rel_path.string();
    }

    EntryType getTargetType() const override {
        return _dto->type;
    }

private:
    std::unique_ptr<FileUpdatedDTO> _dto;
};

class LocalMoveCommand : public LocalCommand {
public:
    LocalMoveCommand(const int cloud_id) {
        _cloud_id = cloud_id;
    }

    ~LocalMoveCommand() = default;
    LocalMoveCommand(const LocalMoveCommand&) = delete;
    LocalMoveCommand& operator=(const LocalMoveCommand&) = delete;

    LocalMoveCommand(LocalMoveCommand&&) noexcept = default;
    LocalMoveCommand& operator=(LocalMoveCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override {}

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override {
        LOG_INFO("LOCAL MOVE", this->getTarget(), "started");

        cloud->proccesMove(_dto, "");

        for (auto& next_command : _next_commands) {
            int cloud_id = next_command->getId();
            int global_id = _dto->global_id;
            auto cloud_dto = db->getFileByCloudIdAndGlobalId(cloud_id, global_id);
            _dto->cloud_file_id = cloud_dto->cloud_file_id;
            _dto->old_cloud_parent_id = cloud_dto->cloud_parent_id;
            _dto->new_cloud_parent_id = db->getCloudFileIdByPath(_dto->new_rel_path, cloud_id);
            _dto->cloud_id = cloud_id;
            next_command->setDTO(std::make_unique<FileMovedDTO>(*_dto));
        }
        LOG_INFO("LOCAL MOVE", this->getTarget(), "completed");

        if (auto ch = owner()) {
            ch->onCommandFinished();
        }
        continueChain();
    }
    void setDTO(std::unique_ptr<FileMovedDTO> dto) override {
        _dto = std::move(dto);
    }

    std::string getTarget() const override {
        return _dto->old_rel_path.string();
    }

    EntryType getTargetType() const override {
        return _dto->type;
    }

private:
    std::unique_ptr<FileMovedDTO> _dto;
};

class LocalDeleteCommand : public LocalCommand {
public:
    LocalDeleteCommand(const int cloud_id) {
        _cloud_id = cloud_id;
    }

    ~LocalDeleteCommand() = default;
    LocalDeleteCommand(const LocalDeleteCommand&) = delete;
    LocalDeleteCommand& operator=(const LocalDeleteCommand&) = delete;

    LocalDeleteCommand(LocalDeleteCommand&&) noexcept = default;
    LocalDeleteCommand& operator=(LocalDeleteCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override {}

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override {
        bool need_local_delete = (_dto->cloud_id != 0);

        LOG_INFO("LOCAL MOVE", this->getTarget(), "started");

        cloud->proccesDelete(_dto, "");

        for (auto& next_command : _next_commands) {
            int cloud_id = next_command->getId();
            int global_id = _dto->global_id;
            std::string cloud_file_id = db->get_cloud_file_id_by_cloud_id(cloud_id, global_id);
            _dto->cloud_file_id = cloud_file_id;
            _dto->cloud_id = cloud_id;
            next_command->setDTO(std::make_unique<FileDeletedDTO>(*_dto));
        }
        if (need_local_delete) {
            std::string path = cloud->getHomeDir() + "/" + _dto->rel_path.string();
            std::filesystem::remove(path);
        }
        db->delete_file_and_links(_dto->global_id);
        LOG_INFO("LOCAL DELETE", this->getTarget(), "completed");

        if (auto ch = owner()) {
            ch->onCommandFinished();
        }
        continueChain();
    }

    void setDTO(std::unique_ptr<FileDeletedDTO> dto) override {
        _dto = std::move(dto);
    }

    std::string getTarget() const override {
        return _dto->rel_path.string();
    }

private:
    std::unique_ptr<FileDeletedDTO> _dto;
};

class CloudUploadCommand : public CloudCommand {
public:
    CloudUploadCommand(const int cloud_id) {
        _cloud_id = cloud_id;
    }

    ~CloudUploadCommand() = default;
    CloudUploadCommand(const CloudUploadCommand&) = delete;
    CloudUploadCommand& operator=(const CloudUploadCommand&) = delete;

    CloudUploadCommand(CloudUploadCommand&&) noexcept = default;
    CloudUploadCommand& operator=(CloudUploadCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override {
        _dto->cloud_id = _cloud_id;
        _handle = std::make_unique<RequestHandle>();
        cloud->setupUploadHandle(_handle, _dto);

        LOG_INFO("CLOUD UPLOAD", "Started for entry: %s on: %s", this->getTarget(), CloudResolver::getName(_cloud_id));
    }

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override {
        cloud->proccesUpload(_dto, _handle->_response);
        db->add_file_link(*_dto);
        LOG_INFO("CLOUD UPLOAD", "Completed for entry: %s on: %s", this->getTarget(), CloudResolver::getName(_cloud_id));
        for (auto& next_command : _next_commands) {
            next_command->setDTO(std::make_unique<FileRecordDTO>(*_dto));
        }

        if (auto ch = owner()) {
            ch->onCommandFinished();
        }
        continueChain();
    }

    void setDTO(std::unique_ptr<FileRecordDTO> dto) override {
        _dto = std::move(dto);
    }

    RequestHandle& getHandle() override {
        return *_handle;
    }

    std::string getTarget() const override {
        return _dto->rel_path.string();
    }

    EntryType getTargetType() const override {
        return _dto->type;
    }

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileRecordDTO> _dto;
};

class CloudUpdateCommand : public CloudCommand {
public:
    CloudUpdateCommand(const int cloud_id) {
        _cloud_id = cloud_id;
    }

    ~CloudUpdateCommand() = default;
    CloudUpdateCommand(const CloudUpdateCommand&) = delete;
    CloudUpdateCommand& operator=(const CloudUpdateCommand&) = delete;

    CloudUpdateCommand(CloudUpdateCommand&&) noexcept = default;
    CloudUpdateCommand& operator=(CloudUpdateCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override {
        _dto->cloud_id = _cloud_id;
        _handle = std::make_unique<RequestHandle>();
        cloud->setupUpdateHandle(_handle, _dto);

        LOG_INFO("CLOUD UPDATE", "Started for entry: %s on: %s", this->getTarget(), CloudResolver::getName(_cloud_id));
    }

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override {
        cloud->proccesUpdate(_dto, _handle->_response);
        db->update_file_link(*_dto);
        LOG_INFO("CLOUD UPDATE", "Completed for entry: %s on: %s", this->getTarget(), CloudResolver::getName(_cloud_id));
        for (auto& next_command : _next_commands) {
            next_command->setDTO(std::make_unique<FileUpdatedDTO>(*_dto));
        }

        if (auto ch = owner()) {
            ch->onCommandFinished();
        }
        continueChain();
    }

    RequestHandle& getHandle() override {
        return *_handle;
    }

    void setDTO(std::unique_ptr<FileUpdatedDTO> dto) override {
        _dto = std::move(dto);
    }

    std::string getTarget() const override {
        return _dto->rel_path.filename().string();
    }

    EntryType getTargetType() const override {
        return _dto->type;
    }

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileUpdatedDTO> _dto;
};

class CloudMoveCommand : public CloudCommand {
public:
    CloudMoveCommand(const int cloud_id) {
        _cloud_id = cloud_id;
    }

    ~CloudMoveCommand() = default;
    CloudMoveCommand(const CloudMoveCommand&) = delete;
    CloudMoveCommand& operator=(const CloudMoveCommand&) = delete;

    CloudMoveCommand(CloudMoveCommand&&) noexcept = default;
    CloudMoveCommand& operator=(CloudMoveCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override {
        _dto->cloud_id = _cloud_id;
        _handle = std::make_unique<RequestHandle>();
        cloud->setupMoveHandle(_handle, _dto);

        LOG_INFO("CLOUD MOVE", "Started for entry: %s on: %s", this->getTarget(), CloudResolver::getName(_cloud_id));
    }

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override {
        cloud->proccesMove(_dto, _handle->_response);
        db->update_file_link(*_dto);
        LOG_INFO("CLOUD MOVE", "Completed for entry: %s on: %s", this->getTarget(), CloudResolver::getName(_cloud_id));
        for (auto& next_command : _next_commands) {
            next_command->setDTO(std::make_unique<FileMovedDTO>(*_dto));
        }

        if (auto ch = owner()) {
            ch->onCommandFinished();
        }
        continueChain();
    }

    RequestHandle& getHandle() override {
        return *_handle;
    }

    void setDTO(std::unique_ptr<FileMovedDTO> dto) override {
        _dto = std::move(dto);
    }

    std::string getTarget() const override {
        return _dto->old_rel_path.filename().string();
    }

    EntryType getTargetType() const override {
        return _dto->type;
    }

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileMovedDTO> _dto;
};

class CloudDownloadNewCommand : public CloudCommand {
public:
    CloudDownloadNewCommand(const int cloud_id) {
        _cloud_id = cloud_id;
    }

    ~CloudDownloadNewCommand() = default;
    CloudDownloadNewCommand(const CloudDownloadNewCommand&) = delete;
    CloudDownloadNewCommand& operator=(const CloudDownloadNewCommand&) = delete;

    CloudDownloadNewCommand(CloudDownloadNewCommand&&) noexcept = default;
    CloudDownloadNewCommand& operator=(CloudDownloadNewCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override {
        LOG_INFO("CLOUD DOWNLOAD", "New file download started: %s", _dto->rel_path.string().c_str());
        _dto->cloud_id = _cloud_id;
        _handle = std::make_unique<RequestHandle>();
        cloud->setupDownloadHandle(_handle, _dto);
    }

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override {
        LOG_INFO("CLOUD DOWNLOAD", "New file downloaded: %s", _dto->rel_path.string().c_str());
        for (auto& next_command : _next_commands) {
            next_command->setDTO(std::make_unique<FileRecordDTO>(*_dto));
        }

        if (auto ch = owner()) {
            ch->onCommandFinished();
        }
        continueChain();
    }

    RequestHandle& getHandle() override {
        return *_handle;
    }

    void setDTO(std::unique_ptr<FileRecordDTO> dto) override {
        _dto = std::move(dto);
    }

    std::string getTarget() const override {
        return _dto->rel_path.filename().string();
    }

    EntryType getTargetType() const override {
        return _dto->type;
    }

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileRecordDTO> _dto;
};

class CloudDownloadUpdateCommand : public CloudCommand {
public:
    CloudDownloadUpdateCommand(const int cloud_id) {
        _cloud_id = cloud_id;
    }

    ~CloudDownloadUpdateCommand() = default;
    CloudDownloadUpdateCommand(const CloudDownloadUpdateCommand&) = delete;
    CloudDownloadUpdateCommand& operator=(const CloudDownloadUpdateCommand&) = delete;

    CloudDownloadUpdateCommand(CloudDownloadUpdateCommand&&) noexcept = default;
    CloudDownloadUpdateCommand& operator=(CloudDownloadUpdateCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override {
        LOG_INFO("CLOUD DOWNLOAD", "Update file download started: %s", _dto->rel_path.string().c_str());
        _dto->cloud_id = _cloud_id;
        _handle = std::make_unique<RequestHandle>();
        cloud->setupDownloadHandle(_handle, _dto);
    }

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override {
        LOG_INFO("CLOUD DOWNLOAD", "Updated file downloaded (tmp): %s",
            _dto->rel_path.string().c_str());
        for (auto& next_command : _next_commands) {
            next_command->setDTO(std::make_unique<FileUpdatedDTO>(*_dto));
        }

        if (auto ch = owner()) {
            ch->onCommandFinished();
          }
        continueChain();
    }

    RequestHandle& getHandle() override {
        return *_handle;
    }

    void setDTO(std::unique_ptr<FileUpdatedDTO> dto) override {
        _dto = std::move(dto);
    }

    std::string getTarget() const override {
        return _dto->rel_path.filename().string();
    }

    EntryType getTargetType() const override {
        return _dto->type;
    }

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileUpdatedDTO> _dto;
};

class CloudDeleteCommand : public CloudCommand {
public:
    CloudDeleteCommand(const int cloud_id) {
        _cloud_id = cloud_id;
    }

    ~CloudDeleteCommand() = default;
    CloudDeleteCommand(const CloudDeleteCommand&) = delete;
    CloudDeleteCommand& operator=(const CloudDeleteCommand&) = delete;

    CloudDeleteCommand(CloudDeleteCommand&&) noexcept = default;
    CloudDeleteCommand& operator=(CloudDeleteCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override {
        _dto->cloud_id = _cloud_id;
        _handle = std::make_unique<RequestHandle>();
        cloud->setupDeleteHandle(_handle, _dto);

        LOG_INFO("CLOUD DELETE", "Started for entry: %s on: %s", this->getTarget(), CloudResolver::getName(_cloud_id));
    }

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override {
        cloud->proccesDelete(_dto, _handle->_response);
        LOG_INFO("CLOUD DELETE", "Completed for entry: %s on: %s", this->getTarget(), CloudResolver::getName(_cloud_id));
        for (auto& next_command : _next_commands) {
            next_command->setDTO(std::make_unique<FileDeletedDTO>(*_dto));
        }

        if (auto ch = owner()) {
            ch->onCommandFinished();
        }
        continueChain();
    }

    void setDTO(std::unique_ptr<FileDeletedDTO> dto) override {
        _dto = std::move(dto);
    }

    RequestHandle& getHandle() override {
        return *_handle;
    }

    std::string getTarget() const override {
        return _dto->rel_path.string();
    }

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileDeletedDTO> _dto;
};
