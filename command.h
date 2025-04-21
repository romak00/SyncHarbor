#pragma once

#include "utils.h"
#include "Networking.h"
#include "CallbackDispatcher.h"
#include "logger.h"


class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void execute(const BaseStorage& cloud) = 0;
    virtual void completionCallback(Database& db, const BaseStorage& cloud) = 0;
    virtual void continueChain() = 0;
    virtual void setDTO(std::unique_ptr<FileRecordDTO> dto) = 0;
    virtual void setDTO(std::unique_ptr<FileModifiedDTO> dto) = 0;
    virtual void setDTO(std::unique_ptr<FileDeletedDTO> dto) = 0;
    virtual RequestHandle& getHandle() = 0;
    virtual std::string getTargetFile() const = 0;
    virtual const int getId() const = 0;
};

class ChainedCommand : public ICommand {
public:
    void addNext(std::unique_ptr<ICommand> next_command) {
        _next_commands.emplace_back(std::move(next_command));
    }
    const int getId() const override {
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
    RequestHandle& getHandle() override {
        static RequestHandle dummy;
        return dummy;
    }
    void execute(const BaseStorage& cloud) override {

    }
    void completionCallback(Database& db, const BaseStorage& cloud) override {
        int global_id = db.add_file(_dto);
        _dto->global_id = global_id;
        if (_dto->cloud_id != 0) {
            db.add_file_link(_dto);
        }
        for (auto& next_command : _next_commands) {
            next_command->setDTO(std::make_unique<FileRecordDTO>(*_dto));
        }
        LOG_INFO("LOCAL UPLOAD", this->getTargetFile(), "completed");
        continueChain();
    }
    void setDTO(std::unique_ptr<FileRecordDTO> dto) override {
        _dto = std::move(dto);
        _dto->cloud_id = _cloud_id;
    }

    void setDTO(std::unique_ptr<FileModifiedDTO> dto) override {
        throw std::logic_error("Not implemented for LocalUploadCommand");
    }

    void setDTO(std::unique_ptr<FileDeletedDTO> dto) override {
        throw std::logic_error("Not implemented for LocalUploadCommand");
    }

    std::string getTargetFile() const override {
        return _dto->rel_path.string();
    }

private:
    std::unique_ptr<FileRecordDTO> _dto;
};

class CloudUploadCommand : public CloudCommand {
public:
    CloudUploadCommand(const int cloud_id) {
        _cloud_id = cloud_id;
    }

    void execute(const BaseStorage& cloud) override {
        _handle = std::make_unique<RequestHandle>();
        cloud.setupUploadHandle(_handle, _dto);

        LOG_INFO("CLOUD UPLOAD", "Started for entry: %s on: %s", this->getTargetFile(), CloudResolver::getName(_cloud_id));
    }

    void completionCallback(Database& db, const BaseStorage& cloud) override {
        cloud.proccesUpload(_dto, _handle->_response);
        db.add_file_link(_dto);
        LOG_INFO("CLOUD UPLOAD", "Completed for entry: %s on: %s", this->getTargetFile(), CloudResolver::getName(_cloud_id));
        continueChain();
    }

    void setDTO(std::unique_ptr<FileRecordDTO> dto) override {
        _dto = std::move(dto);
        _dto->cloud_id = _cloud_id;
    }

    void setDTO(std::unique_ptr<FileModifiedDTO> dto) override {
        throw std::logic_error("Not implemented for CloudUploadCommand");
    }

    void setDTO(std::unique_ptr<FileDeletedDTO> dto) override {
        throw std::logic_error("Not implemented for CloudUploadCommand");
    }

    RequestHandle& getHandle() override {
        return *_handle;
    }

    std::string getTargetFile() const override {
        return _dto->rel_path.string();
    }

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileRecordDTO> _dto;
};

class CloudUpdateCommand : public CloudCommand {
public:
    CloudUpdateCommand() {

    }

    void execute(const BaseStorage& cloud) override {

    }

    void completionCallback(Database& db, const BaseStorage& cloud) override {

    }

    RequestHandle& getHandle() override {
        return *_handle;
    }

    std::string getTargetFile() const override {
        return _dto->rel_path.filename().string();
    }

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileModifiedDTO> _dto;
};

class CloudDownloadCommand : public CloudCommand {
public:
    CloudDownloadCommand() {

    }

    void execute(const BaseStorage& cloud) override {
    }

    void completionCallback(Database& db, const BaseStorage& cloud) override {

    }

    RequestHandle& getHandle() override {
        return *_handle;
    }

    std::string getTargetFile() const override {
        return _dto->rel_path.filename().string();
    }

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileRecordDTO> _dto;
};

