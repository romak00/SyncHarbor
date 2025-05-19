#pragma once

#include <vector>
#include <string>
#include <filesystem>
#include <atomic>
#include <unordered_map>
#include "database.h"
#include "BaseStorage.h"
#include "LocalStorage.h"
#include "http-server.h"
#include "cloud-factory.h"
#include "change-factory.h"
#include "conflict-resolver.h"

class SyncManager {
public:
    enum class Mode {
        InitialSync,
        Daemon
    };

    SyncManager(
        const std::string& config_path,
        const std::string& db_file,
        const std::filesystem::path& local_dir,
        Mode mode
    );

    SyncManager(
        const std::string& db_file,
        Mode mode
    );

    void run();

    void shutdown();

    void setLocalDir(const std::string& path);

    std::vector<std::filesystem::path> checkLocalPermissions() const;

    void onChangeCompleted(const std::filesystem::path& path,
        std::vector<std::unique_ptr<Change>>&& dependents);

    void registerCloud(const std::string& cloud_name, const CloudProviderType type, const std::string& client_id, const std::string& client_secret, const std::filesystem::path& home_path);
private:
    void init();

    void changeFinished();

    void createPath(const std::filesystem::path& path, const std::filesystem::path& missing);

    void loadConfig();

    void loadDBConfig();

    void setupClouds();

    void initialSync();

    void daemonMode();

    bool checkInitialSyncCompleted();

    void setupLocalHttpServer();

    void proccessLoop();

    bool anyStorageHasRaw();

    void pollingLoop();

    void handleChange(std::unique_ptr<Change> change);

    bool ensureParentDirectories(const std::filesystem::path& path);

    void setRawSignal();

    void ensureRootsExist();

    void startChange(const std::filesystem::path& path, std::unique_ptr<Change> change);

    void refreshAccessTokens();

    void openUrl(const std::string& url);

    bool directoryIsWritable(const std::filesystem::path& dir) const;

    std::filesystem::path _local_dir;

    std::string _config_path;
    std::string _db_file;

    std::unordered_map<std::filesystem::path, std::unique_ptr<Change>> _current_changes;
    ThreadSafeQueue<std::unique_ptr<Change>> _changes_buff;

    std::unordered_map<int, std::shared_ptr<BaseStorage>> _clouds;
    nlohmann::json _cloud_configs;

    std::shared_ptr<LocalStorage> _local;
    std::shared_ptr<Database> _db;

    std::unique_ptr<LocalHttpServer> _http_server;

    std::unique_ptr<std::thread> _polling_worker;
    std::unique_ptr<std::thread> _changes_worker;

    std::shared_ptr<RawSignal> _raw_signal;

    std::atomic<bool> _should_exit;

    std::time_t _next_token_refresh;

    int _num_clouds;

    Mode _mode;
};