#pragma once

#include "BaseStorage.h"
#include "change-factory.h"
#include "wtr/watcher.hpp" 
#include "logger.h"

#define XXH_INLINE_ALL
#include <xxhash.h>


#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif


class LocalStorage : public BaseStorage {
public:
    LocalStorage(const std::filesystem::path& home_dir, const int cloud_id, const std::shared_ptr<Database>& db_conn);

    void setupUploadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const override {}
    void setupUpdateHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileUpdatedDTO>& dto) const override {}
    void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const override {}
    void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileUpdatedDTO>& dto) const override {}
    void setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) const override {}
    void setupMoveHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileMovedDTO>& dto) const override {}

    std::vector<std::unique_ptr<FileRecordDTO>> initialFiles() override;
    void getChanges() override {}

    std::vector<std::shared_ptr<Change>> proccessChanges() override;

    CloudProviderType getType() const override { return CloudProviderType::LocalStorage; }

    void proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response = "") const override;

    void proccesUpdate(std::unique_ptr<FileUpdatedDTO>& dto, const std::string& response = "") const override;
    void proccesDownload(std::unique_ptr<FileUpdatedDTO>& dto, const std::string& response = "") const override {}
    void proccesDelete(std::unique_ptr<FileDeletedDTO>& dto, const std::string& response = "") const override;
    void proccesMove(std::unique_ptr<FileMovedDTO>& dto, const std::string& response = "") const override;

    std::vector<std::unique_ptr<FileRecordDTO>> createPath(const std::filesystem::path& path, const std::filesystem::path& missing) override;

    std::string buildAuthURL(int local_port) const override { return ""; }
    void getRefreshToken(const std::string& code, const int local_port) override {}
    void refreshAccessToken() override {}
    void proccessAuth(const std::string& responce) override {}

    std::string getDeltaToken() override { return ""; }


    int id() const override {
        return _id;
    }

    void startWatching();
    void stopWatching();

    std::string getHomeDir() const override;

    ~LocalStorage() noexcept override;

    bool hasChanges() const override;

    void ensureRootExists() override {}

    uint64_t getFileId(const std::filesystem::path& p) const;
    uint64_t computeFileHash(const std::filesystem::path& path, uint64_t seed = 0) const;
    void onFsEvent(const wtr::event& e);


    bool isDoc(const std::filesystem::path& path) const;

    friend class LocalStorageTest;

    void handleRenamed(const FileEvent& evt);

    void handleDeleted(const FileEvent& evt);
    void handleUpdated(const FileEvent& evt);
    void handleCreated(const FileEvent& evt);
    void handleMoved(const FileEvent& evt);

    std::time_t fromWatcherTime(const long long);

    bool ignoreTmp(const std::filesystem::path& path);

    void setOnChange(std::function<void()> cb) override;
private:

    bool thatFileTmpExists(const std::filesystem::path& path);

    ThreadSafeQueue<std::shared_ptr<Change>> _changes_queue;
    ThreadSafeQueue<FileEvent> _events_buff;
    mutable ThreadSafeEventsRegistry _expected_events;

    std::unique_ptr<wtr::watcher::watch> _watcher;

    std::shared_ptr<Database> _db;

    std::function<void()> _onChange;

    std::filesystem::path _local_home_dir;

    std::atomic<bool> _watching = false;

    int _id;
};