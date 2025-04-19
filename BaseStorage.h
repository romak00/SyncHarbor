#pragma once

#include "utils.h"
#include <filesystem>
#include <memory>
#include "database.h"

class BaseStorage {
public:
    virtual ~BaseStorage() = default;
    virtual void setupUploadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const = 0;
    virtual void setupUpdateHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileModifiedDTO>& dto) const = 0;
    virtual void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const = 0;
    virtual void setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) const = 0;

    virtual void proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const = 0;
    virtual void proccesUpdate(std::unique_ptr<FileModifiedDTO>& dto, const std::string& response) const = 0;
    virtual void proccesDownload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const = 0;
    virtual void proccesDelete(std::unique_ptr<FileDeletedDTO>& dto, const std::string& response) const = 0;
    
    virtual std::vector<std::unique_ptr<ChangeDTO>> initialFiles() = 0;
    virtual std::vector<std::unique_ptr<ChangeDTO>> scanForChanges(std::shared_ptr<Database> db) = 0;

    virtual const int id() const = 0;
};