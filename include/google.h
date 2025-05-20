#pragma once

#include "BaseStorage.h"
#include <string>
#include <iostream>

struct GoogleDocMimeInfo {
    std::string cloud_mime_type;
    std::string export_mime_type;
};

class GoogleDrive : public BaseStorage {
public:
    GoogleDrive(
        const std::string& client_id,
        const std::string& client_secret,
        const std::string& refresh_token,
        const std::filesystem::path& home_dir,
        const std::filesystem::path& local_home_dir,
        const std::shared_ptr<Database>& db_conn,
        const int cloud_id
    );

    GoogleDrive(
        const std::string& client_id,
        const std::string& client_secret,
        const std::filesystem::path& home_dir,
        const std::filesystem::path& local_home_dir,
        const std::shared_ptr<Database>& db_conn,
        const int cloud_id
    );

    GoogleDrive(
        const std::string& client_id,
        const std::string& client_secret,
        const std::string& refresh_token,
        const std::filesystem::path& home_dir,
        const std::filesystem::path& local_home_dir,
        const std::shared_ptr<Database>& db_conn,
        const int cloud_id,
        const std::string& start_page_token
    );

    //void initial_config() override;
    void setupUploadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const override;

    void setupUpdateHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileUpdatedDTO>& dto) const override;
    void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const override;
    void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileUpdatedDTO>& dto) const override;
    void setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) const override;
    void setupMoveHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileMovedDTO>& dto) const override;

    void getChanges() override;

    std::vector<std::unique_ptr<FileRecordDTO>> initialFiles() override;

    std::vector<std::unique_ptr<Change>> proccessChanges() override;

    std::vector<std::unique_ptr<FileRecordDTO>> createPath(const std::filesystem::path& path, const std::filesystem::path& missing) override;

    int id() const override;

    void proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const override;

    void proccesUpdate(std::unique_ptr<FileUpdatedDTO>& dto, const std::string& response) const override;
    void proccesDownload(std::unique_ptr<FileUpdatedDTO>& dto, const std::string& response) const override;
    void proccesDelete(std::unique_ptr<FileDeletedDTO>& dto, const std::string& response) const override;
    void proccesMove(std::unique_ptr<FileMovedDTO>& dto, const std::string& response) const override;

    void refreshAccessToken() override;

    std::string getHomeDir() const override;

    CloudProviderType getType() const override { return CloudProviderType::GoogleDrive; };

    ~GoogleDrive() = default;

    bool hasChanges() const override;

    std::string buildAuthURL(int local_port) const override;

    void getRefreshToken(const std::string& code, const int local_port) override;

    void proccessAuth(const std::string& responce) override;

    std::string getDeltaToken() override;

    void ensureRootExists() override;

    void setOnChange(std::function<void()> cb) override;
private:
    std::optional<GoogleDocMimeInfo> getGoogleDocMimeByExtension(const std::filesystem::path& path) const;
        
    bool ignoreTmp(const std::string& name);

    std::unordered_map<std::string, std::string> _dir_id_map;

    std::filesystem::path _local_home_path;
    std::filesystem::path _home_path;

    std::string _client_id;
    std::string _client_secret;
    std::string _refresh_token;
    std::string _access_token;
    std::string _home_dir_id;
    std::string _page_token;

    ThreadSafeQueue<std::vector<nlohmann::json>> _events_buff;
    mutable ThreadSafeEventsregister _expected_events;

    std::unordered_map<std::string, std::unique_ptr<FileDeletedDTO>> _pending_deletes;

    std::shared_ptr<Database> _db;

    std::function<void()> _onChange;

    std::time_t _access_token_expires;

    int _id;
};