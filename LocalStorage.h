#pragma once

#include "BaseStorage.h"
#include <efsw/efsw.hpp>

enum class FileEventType {
    Added,
    Deleted,
    Modified,
    Moved
};

struct FileEvent {
    FileEventType type;
    std::filesystem::path old_path;
    std::filesystem::path new_path;
};

class LocalStorage : public BaseStorage {
public:
    LocalStorage(const std::filesystem::path& home_dir, const int cloud_id, const std::string& db_file_name);

    void setupUploadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const override {}
    void setupUpdateHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileModifiedDTO>& dto) const override {}
    void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const override {}
    void setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) const override {}

    std::vector<std::unique_ptr<ChangeDTO>> initialFiles() override { return {}; }
    std::vector<std::unique_ptr<ChangeDTO>> scanForChanges(std::shared_ptr<Database> db) override { return {}; }
    
    void proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const override {}

    void proccesUpdate(std::unique_ptr<FileModifiedDTO>& dto, const std::string& response) const override {}
    void proccesDownload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const override {}
    void proccesDelete(std::unique_ptr<FileDeletedDTO>& dto, const std::string& response) const override {}

    const int id() const override {
        return _id;
    }

    void startWatching();
    void stopWatching();

    ~LocalStorage() noexcept override;

    
private:
    class Listener : public efsw::FileWatchListener {
        public:
        Listener(LocalStorage* owner) : _owner(owner) {}
        
        void handleFileAction(
            efsw::WatchID watchid,
            const std::string& dir,
            const std::string& filename,
            efsw::Action action,
            std::string oldFilename
        ) override {
            _owner->onFileChanged(dir, filename, action, oldFilename);
        }
        
        private:
        LocalStorage* _owner;
    };
    
    void onFileChanged(
        const std::string & dir,
        const std::string & filename,
        efsw::Action action,
        const std::string & old_filename
    );
    void changesFilter();

    
    ThreadSafeQueue<std::unique_ptr<ChangeDTO>> _changes_queue;
    ThreadSafeQueue<FileEvent> _events_queue;
    std::filesystem::path _local_home_dir;

    std::unique_ptr<Database> _db;

    std::unique_ptr<std::thread> _watcher_thread;
    std::unique_ptr<std::thread> _changes_filtering_worker;
    std::unique_ptr<Listener> _listener;
    
    efsw::FileWatcher _watcher;
    efsw::WatchID _watch_id;
    std::atomic<bool> _watching = false;
    int _id;
};


