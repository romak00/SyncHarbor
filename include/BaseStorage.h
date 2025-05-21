#pragma once

#include <filesystem>
#include <memory>
#include "database.h"

class Change;

class BaseStorage {
public:
    virtual ~BaseStorage() = default;
    virtual void setupUploadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const = 0;
    virtual void setupUpdateHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileUpdatedDTO>& dto) const = 0;
    virtual void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const = 0;
    virtual void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileUpdatedDTO>& dto) const = 0;
    virtual void setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) const = 0;
    virtual void setupMoveHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileMovedDTO>& dto) const = 0;

    virtual void proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const = 0;
    virtual void proccesUpdate(std::unique_ptr<FileUpdatedDTO>& dto, const std::string& response) const = 0;
    virtual void proccesDownload(std::unique_ptr<FileUpdatedDTO>& dto, const std::string& response) const = 0;
    virtual void proccesDelete(std::unique_ptr<FileDeletedDTO>& dto, const std::string& response) const = 0;
    virtual void proccesMove(std::unique_ptr<FileMovedDTO>& dto, const std::string& response) const = 0;

    virtual std::vector<std::unique_ptr<FileRecordDTO>> createPath(const std::filesystem::path& path, const std::filesystem::path& missing) = 0;

    virtual std::string buildAuthURL(int local_port) const = 0;
    virtual void getRefreshToken(const std::string& code, const int local_port) = 0;
    virtual void refreshAccessToken() = 0;
    virtual void proccessAuth(const std::string& responce) = 0;

    virtual std::string getDeltaToken() = 0;

    virtual std::string getHomeDir() const = 0;

    virtual std::vector<std::unique_ptr<FileRecordDTO>> initialFiles() = 0;
    virtual void getChanges() = 0;

    virtual int id() const = 0;

    virtual CloudProviderType getType() const = 0;

    virtual bool hasChanges() const = 0;

    virtual void setOnChange(std::function<void()> cb) = 0;

    virtual std::vector<std::shared_ptr<Change>> proccessChanges() = 0;

    virtual void ensureRootExists() = 0;
};