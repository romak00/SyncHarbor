#pragma once

#include "utils.h"
#include "Networking.h"
#include "CallbackDispatcher.h"


class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void execute(const BaseStorage& cloud) = 0;
    virtual void completionCallback(Database& db, const BaseStorage& cloud) = 0;
    virtual void continueChain() = 0;
    virtual void setDTO(std::unique_ptr<FileRecordDTO> dto) = 0;
    virtual void setDTO(std::unique_ptr<FileModifiedDTO> dto) = 0;
    virtual void setDTO(std::unique_ptr<FileDeletedDTO> dto) = 0;
    virtual RequestHandle& getHandle() const = 0;
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
    RequestHandle& getHandle() const override {}
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
        continueChain();
    }
    void setDTO(std::unique_ptr<FileRecordDTO> dto) override {
        _dto = std::move(dto);
        _dto->cloud_id = _cloud_id;
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
    }

    void completionCallback(Database& db, const BaseStorage& cloud) override {
        cloud.proccesUpload(_dto, _handle->_response);
        db.add_file_link(_dto);
        continueChain();
    }

    void setDTO(std::unique_ptr<FileRecordDTO> dto) {
        _dto = std::move(dto);
        _dto->cloud_id = _cloud_id;
    }

    RequestHandle& getHandle() const override {
        return *_handle;
    }

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileRecordDTO> _dto;
};

class CloudUpdateCommand : public CloudCommand {
public:
    CloudUpdateCommand() {

    }
    
    void execute(const BaseStorage& cloud) override{
        _handle = std::make_unique<RequestHandle>();
        cloud.setupUpdateHandle(_handle, _dto);
    }

    void completionCallback(Database& db, const BaseStorage& cloud) override {
        cloud.proccesUpdate(_dto, _handle->_response);
    }

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileModifiedDTO> _dto;
};

class CloudDownloadCommand : public CloudCommand {
public:
    CloudDownloadCommand(std::unique_ptr<FileRecordDTO> dto, std::shared_ptr<BaseStorage> cloud) {
        _dto = std::move(dto);
        _handle = std::make_unique<RequestHandle>();
        cloud->setupDownloadHandle(_handle, _dto);
    }

    void execute(const BaseStorage& cloud) override {
    }

    void completionCallback(Database& db, const BaseStorage& cloud) override {
        cloud.proccesUpload(_dto, _handle->_response);
    }

private:
    std::unique_ptr<RequestHandle> _handle;
    std::unique_ptr<FileRecordDTO> _dto;
};

class ChangeCommand : public ICommand {
    
};
