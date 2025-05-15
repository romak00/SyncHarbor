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
    virtual void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileModifiedDTO>& dto) const = 0;
    virtual void setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) const = 0;

    virtual void proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const = 0;
    virtual void proccesUpdate(std::unique_ptr<FileModifiedDTO>& dto, const std::string& response) const = 0;
    virtual void proccesDownload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const = 0;
    virtual void proccesDelete(std::unique_ptr<FileDeletedDTO>& dto, const std::string& response) const = 0;

    virtual std::vector<std::unique_ptr<Change>> flushOldDeletes() = 0;

    virtual void subscribeToChanges(
        const std::unique_ptr<RequestHandle>& handle,
        const std::string& callback_url,
        const std::string& channel_id
    ) = 0;


    virtual std::string buildAuthURL(int local_port) const = 0;
    virtual void getRefreshToken(const std::string& code, const int local_port) = 0;
    virtual void refreshAccessToken() = 0;
    virtual void proccessAuth(const std::string& responce) = 0;
    virtual void setDelta(const std::string& response) = 0;
    virtual void getDelta(const std::unique_ptr<RequestHandle>& handle) = 0;

    virtual std::string getHomeDir() const = 0;

    virtual std::vector<std::unique_ptr<FileRecordDTO>> initialFiles() = 0;
    virtual void getChanges(const std::unique_ptr<RequestHandle>& handle) = 0;
    virtual bool handleChangesResponse(const std::unique_ptr<RequestHandle>& handle, std::vector<std::string>& pages) = 0;

    virtual int id() const = 0;

    virtual CloudProviderType getType() const = 0;

    virtual bool hasChanges() const = 0;

    virtual std::vector<std::unique_ptr<Change>> proccessChanges() = 0;

    virtual void setRawSignal(std::shared_ptr<RawSignal> raw_signal) = 0;

    virtual void ensureRootExists() = 0;

    virtual bool isRealTime() const {
        return false;
    }
};