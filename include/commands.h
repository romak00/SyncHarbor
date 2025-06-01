#pragma once

#include "BaseStorage.h"
#include "Networking.h"
#include "CallbackDispatcher.h"
#include "logger.h"

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void execute(const std::shared_ptr<BaseStorage>& cloud) = 0;
    virtual void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) = 0;
    virtual void continueChain() = 0;
    virtual void setDTO(std::unique_ptr<FileRecordDTO> dto);
    virtual void setDTO(std::unique_ptr<FileUpdatedDTO> dto);
    virtual void setDTO(std::unique_ptr<FileDeletedDTO> dto);
    virtual void setDTO(std::unique_ptr<FileMovedDTO> dto);
    virtual void addNext(std::unique_ptr<ICommand> next_command) = 0;
    virtual RequestHandle& getHandle();
    virtual std::string getTarget() const = 0;
    virtual EntryType getTargetType() const;
    virtual int getId() const = 0;
    virtual bool needRepeat() const;
    void setOwner(std::weak_ptr<Change> ow) noexcept;

protected:
    std::shared_ptr<Change> owner() const noexcept;

private:
    std::weak_ptr<Change> _owner;
};

class ChainedCommand : public ICommand {
public:
    void addNext(std::unique_ptr<ICommand> next_command) override;
    int getId() const override;
protected:
    std::vector<std::unique_ptr<ICommand>> _next_commands;
    int _cloud_id;
};

class CloudCommand : public ChainedCommand {
protected:
    void continueChain() override;
};

class LocalCommand : public ChainedCommand {
protected:
    void continueChain() override;
};

class LocalUploadCommand : public LocalCommand {
public:
    LocalUploadCommand(const int cloud_id);

    ~LocalUploadCommand() = default;
    LocalUploadCommand(const LocalUploadCommand&) = delete;
    LocalUploadCommand& operator=(const LocalUploadCommand&) = delete;

    LocalUploadCommand(LocalUploadCommand&&) noexcept = default;
    LocalUploadCommand& operator=(LocalUploadCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override {}

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override;

    void setDTO(std::unique_ptr<FileRecordDTO> dto) override;

    std::string getTarget() const override;

    EntryType getTargetType() const override;

private:
    std::unique_ptr<FileRecordDTO> _dto;
};

class LocalUpdateCommand : public LocalCommand {
public:
    LocalUpdateCommand(const int cloud_id);

    ~LocalUpdateCommand() = default;
    LocalUpdateCommand(const LocalUpdateCommand&) = delete;
    LocalUpdateCommand& operator=(const LocalUpdateCommand&) = delete;

    LocalUpdateCommand(LocalUpdateCommand&&) noexcept = default;
    LocalUpdateCommand& operator=(LocalUpdateCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override {}

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override;
    
    void setDTO(std::unique_ptr<FileUpdatedDTO> dto) override;

    std::string getTarget() const override;

    EntryType getTargetType() const override;

private:
    std::unique_ptr<FileUpdatedDTO> _dto;
};

class LocalMoveCommand : public LocalCommand {
public:
    LocalMoveCommand(const int cloud_id);

    ~LocalMoveCommand() = default;
    LocalMoveCommand(const LocalMoveCommand&) = delete;
    LocalMoveCommand& operator=(const LocalMoveCommand&) = delete;

    LocalMoveCommand(LocalMoveCommand&&) noexcept = default;
    LocalMoveCommand& operator=(LocalMoveCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override {}

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override;

    void setDTO(std::unique_ptr<FileMovedDTO> dto) override;

    std::string getTarget() const override;

    EntryType getTargetType() const override;

private:
    std::unique_ptr<FileMovedDTO> _dto;
};

class LocalDeleteCommand : public LocalCommand {
public:
    LocalDeleteCommand(const int cloud_id);

    ~LocalDeleteCommand() = default;
    LocalDeleteCommand(const LocalDeleteCommand&) = delete;
    LocalDeleteCommand& operator=(const LocalDeleteCommand&) = delete;

    LocalDeleteCommand(LocalDeleteCommand&&) noexcept = default;
    LocalDeleteCommand& operator=(LocalDeleteCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override {}

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override;

    void setDTO(std::unique_ptr<FileDeletedDTO> dto) override;

    std::string getTarget() const override;

private:
    std::unique_ptr<FileDeletedDTO> _dto;
};

class CloudUploadCommand : public CloudCommand {
public:
    CloudUploadCommand(const int cloud_id);

    ~CloudUploadCommand() = default;
    CloudUploadCommand(const CloudUploadCommand&) = delete;
    CloudUploadCommand& operator=(const CloudUploadCommand&) = delete;

    CloudUploadCommand(CloudUploadCommand&&) noexcept = default;
    CloudUploadCommand& operator=(CloudUploadCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override;

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override;

    void setDTO(std::unique_ptr<FileRecordDTO> dto) override;

    RequestHandle& getHandle() override;

    std::string getTarget() const override;

    EntryType getTargetType() const override;

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileRecordDTO> _dto;
};

class CloudUpdateCommand : public CloudCommand {
public:
    CloudUpdateCommand(const int cloud_id);

    ~CloudUpdateCommand() = default;
    CloudUpdateCommand(const CloudUpdateCommand&) = delete;
    CloudUpdateCommand& operator=(const CloudUpdateCommand&) = delete;

    CloudUpdateCommand(CloudUpdateCommand&&) noexcept = default;
    CloudUpdateCommand& operator=(CloudUpdateCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override;

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override;

    RequestHandle& getHandle() override;

    void setDTO(std::unique_ptr<FileUpdatedDTO> dto) override;

    std::string getTarget() const override;

    EntryType getTargetType() const override;

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileUpdatedDTO> _dto;
};

class CloudMoveCommand : public CloudCommand {
public:
    CloudMoveCommand(const int cloud_id);

    ~CloudMoveCommand() = default;
    CloudMoveCommand(const CloudMoveCommand&) = delete;
    CloudMoveCommand& operator=(const CloudMoveCommand&) = delete;

    CloudMoveCommand(CloudMoveCommand&&) noexcept = default;
    CloudMoveCommand& operator=(CloudMoveCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override;

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override;

    RequestHandle& getHandle() override;

    void setDTO(std::unique_ptr<FileMovedDTO> dto) override;

    std::string getTarget() const override;

    EntryType getTargetType() const override;

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileMovedDTO> _dto;
};

class CloudDownloadNewCommand : public CloudCommand {
public:
    CloudDownloadNewCommand(const int cloud_id);

    ~CloudDownloadNewCommand() = default;
    CloudDownloadNewCommand(const CloudDownloadNewCommand&) = delete;
    CloudDownloadNewCommand& operator=(const CloudDownloadNewCommand&) = delete;

    CloudDownloadNewCommand(CloudDownloadNewCommand&&) noexcept = default;
    CloudDownloadNewCommand& operator=(CloudDownloadNewCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override;

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override;

    RequestHandle& getHandle() override;

    void setDTO(std::unique_ptr<FileRecordDTO> dto) override;

    std::string getTarget() const override;

    EntryType getTargetType() const override;

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileRecordDTO> _dto;
};

class CloudDownloadUpdateCommand : public CloudCommand {
public:
    CloudDownloadUpdateCommand(const int cloud_id);

    ~CloudDownloadUpdateCommand() = default;
    CloudDownloadUpdateCommand(const CloudDownloadUpdateCommand&) = delete;
    CloudDownloadUpdateCommand& operator=(const CloudDownloadUpdateCommand&) = delete;

    CloudDownloadUpdateCommand(CloudDownloadUpdateCommand&&) noexcept = default;
    CloudDownloadUpdateCommand& operator=(CloudDownloadUpdateCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override;

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override;

    RequestHandle& getHandle() override;

    void setDTO(std::unique_ptr<FileUpdatedDTO> dto) override;

    std::string getTarget() const override;

    EntryType getTargetType() const override;

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileUpdatedDTO> _dto;
};

class CloudDeleteCommand : public CloudCommand {
public:
    CloudDeleteCommand(const int cloud_id);

    ~CloudDeleteCommand() = default;
    CloudDeleteCommand(const CloudDeleteCommand&) = delete;
    CloudDeleteCommand& operator=(const CloudDeleteCommand&) = delete;

    CloudDeleteCommand(CloudDeleteCommand&&) noexcept = default;
    CloudDeleteCommand& operator=(CloudDeleteCommand&&) noexcept = default;

    void execute(const std::shared_ptr<BaseStorage>& cloud) override;

    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) override;

    void setDTO(std::unique_ptr<FileDeletedDTO> dto) override;

    RequestHandle& getHandle() override;

    std::string getTarget() const override;

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileDeletedDTO> _dto;
};
