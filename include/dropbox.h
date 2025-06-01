#pragma once

#include "change-factory.h"
#include "event-registry.h"
#include <unordered_set>

class Dropbox : public BaseStorage {
public:
    Dropbox(
        const std::string& client_id,
        const std::string& client_secret,
        const std::string& refresh_token,
        const std::filesystem::path& home_dir,
        const std::filesystem::path& local_home_dir,
        const std::shared_ptr<Database>& db_conn,
        const int cloud_id
    );
    Dropbox(
        const std::string& client_id,
        const std::string& client_secret,
        const std::filesystem::path& home_dir,
        const std::filesystem::path& local_home_dir,
        const std::shared_ptr<Database>& db_conn,
        const int cloud_id
    );
    Dropbox(
        const std::string& client_id,
        const std::string& client_secret,
        const std::string& refresh_token,
        const std::filesystem::path& home_dir,
        const std::filesystem::path& local_home_dir,
        const std::shared_ptr<Database>& db_conn,
        const int cloud_id,
        const std::string& start_page_token
    );

    void setupUploadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const override;

    void setupUpdateHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileUpdatedDTO>& dto) const override;
    void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const override;
    void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileUpdatedDTO>& dto) const override;
    void setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) const override;
    void setupMoveHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileMovedDTO>& dto) const override;

    void getChanges() override;
    std::vector<std::shared_ptr<Change>> proccessChanges() override;

    std::vector<std::unique_ptr<FileRecordDTO>> initialFiles() override;

    int id() const override;

    std::vector<std::unique_ptr<FileRecordDTO>> createPath(const std::filesystem::path& path, const std::filesystem::path& missing) override;

    /* std::unique_ptr<CurlEasyHandle> create_file_upload_handle(const std::filesystem::path& path, const std::string& parent = "") override;
    std::unique_ptr<CurlEasyHandle> create_dir_upload_handle(const std::filesystem::path& path, const std::string& parent = "") override;
    std::unique_ptr<CurlEasyHandle> create_file_download_handle(const std::string& id, const std::filesystem::path& path) override;
    std::unique_ptr<CurlEasyHandle> create_file_update_handle(const std::string& id, const std::filesystem::path& path, const std::string& name = "") override;
    std::unique_ptr<CurlEasyHandle> create_file_delete_handle(const std::string& id) override;
    std::unique_ptr<CurlEasyHandle> create_name_update_handle(const std::string& id, const std::string& name) override;
    std::unique_ptr<CurlEasyHandle> create_parent_update_handle(const std::string& id, const std::string& parent, const std::string& parent_to_remove = "") override;
    std::vector<nlohmann::json> get_changes(const int cloud_id, std::shared_ptr<Database> db_conn) override; */
    void proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const override;

    void proccesUpdate(std::unique_ptr<FileUpdatedDTO>& dto, const std::string& response) const override;
    void proccesDownload(std::unique_ptr<FileUpdatedDTO>& dto, const std::string& response) const override;
    void proccesDelete(std::unique_ptr<FileDeletedDTO>& dto, const std::string& response) const override;
    void proccesMove(std::unique_ptr<FileMovedDTO>& dto, const std::string& response) const override;

    void refreshAccessToken() override;

    std::string getHomeDir() const override;

    CloudProviderType getType() const override { return CloudProviderType::Dropbox; };

    bool hasChanges() const override;

    void proccessAuth(const std::string& responce) override;

    std::string buildAuthURL(int local_port) const override;

    std::string getRefreshToken(const std::string& code, const int local_port) override;

    void ensureRootExists() override;

    std::string getDeltaToken() override;

    ~Dropbox() = default;

    void setOnChange(std::function<void()> cb) override;
private:

    bool isDropboxShortcutJsonFile(const std::filesystem::path& path) const;

    std::filesystem::path _home_path;
    std::filesystem::path _local_home_path;
    std::string _client_id;
    std::string _client_secret;
    std::string _refresh_token;
    std::string _access_token;
    std::string _page_token;

    ThreadSafeQueue<std::vector<std::string>> _events_buff;
    mutable ThreadSafeEventsRegistry _expected_events;

    std::shared_ptr<Database> _db;

    std::function<void()> _onChange;

    std::time_t _access_token_expires;

    int _id;
};
