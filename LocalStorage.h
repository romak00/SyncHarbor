#pragma once

#include "BaseStorage.h"
#include "wtr/watcher.hpp" 
#include <xxhash.h>
#include "logger.h"
#include "command.h"

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
    void setupUpdateHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileModifiedDTO>& dto) const override {}
    void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const override {}
    void setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) const override {}

    std::vector<std::unique_ptr<FileRecordDTO>> initialFiles() override { return {}; }
    void getChanges(const std::unique_ptr<RequestHandle>& handle) override {}
    bool handleChangesResponse(const std::unique_ptr<RequestHandle>& handle, std::vector<std::string>& pages)override { return ""; }
    std::vector<std::unique_ptr<Change>> proccessChanges() override;

    void subscribeToChanges(
        const std::unique_ptr<RequestHandle>& handle,
        const std::string& callback_url,
        const std::string& channel_id
    ) override {
    }

    CloudProviderType getType() const override { return CloudProviderType::LocalStorage; }

    void proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const override {}

    void proccesUpdate(std::unique_ptr<FileModifiedDTO>& dto, const std::string& response) const override;
    void proccesDownload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const override {}
    void proccesDelete(std::unique_ptr<FileDeletedDTO>& dto, const std::string& response) const override {}


    std::string buildAuthURL(int local_port) const override { return ""; }
    void getRefreshToken(const std::string& code, const int local_port) override {}
    void refreshAccessToken() override {}
    void proccessAuth(const std::string& responce) override {}
    void setDelta(const std::string& response) override {}
    void getDelta(const std::unique_ptr<RequestHandle>& handle) override {}


    int id() const override {
        return _id;
    }

    void startWatching();
    void stopWatching();

    std::vector<std::unique_ptr<Change>> flushOldDeletes() override;

    std::string getHomeDir() const override;

    ~LocalStorage() noexcept override;

    bool hasChanges() const override;

    uint64_t getFileId(const std::filesystem::path& p) const;
    uint64_t computeFileHash(const std::filesystem::path& path, uint64_t seed = 0) const;

    void setRawSignal(std::shared_ptr<RawSignal> raw_signal) override;

    bool isRealTime() const override;

    void ensureRootExists() override {}
private:
    void onFsEvent(const wtr::event& e);

    void handleRenamed(const FileEvent& evt);

    void handleDeleted(const FileEvent& evt);
    void handleUpdated(const FileEvent& evt);
    void handleCreated(const FileEvent& evt);
    void handleMoved(const FileEvent& evt);

    std::time_t fromWatcherTime(const long long);

    bool checkUndo(const FileEvent& evt);

    bool ignoreTmp(const std::filesystem::path& path);

    ThreadSafeQueue<std::unique_ptr<Change>> _changes_queue;
    ThreadSafeQueue<FileEvent> _events_buff;
    mutable ThreadSafeEventsregister _expected_events;

    std::unordered_map<uint64_t, FileEvent> _pending_deletes;

    std::unique_ptr<wtr::watcher::watch> _watcher;

    std::shared_ptr<Database> _db;

    std::shared_ptr<RawSignal> _raw_signal;

    std::filesystem::path _local_home_dir;

    std::condition_variable _cleanup_cv;
    std::mutex _cleanup_mtx;

    std::atomic<bool> _watching = false;

    int _UNDO_INTERVAL{ 15 };
    int _id;
};