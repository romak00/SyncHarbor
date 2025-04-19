#pragma once

#include "BaseStorage.h"
#include <string>
#include <iostream>

class Dropbox : public BaseStorage {
public:
    Dropbox(
        const std::string& client_id,
        const std::string& client_secret,
        const std::string& refresh_token,
        const std::filesystem::path& home_dir,
        const std::filesystem::path& local_home_dir,
        const int cloud_id
    );
    Dropbox(
        const std::string& client_id,
        const std::string& client_secret,
        const std::string& refresh_token,
        const std::filesystem::path& home_dir,
        const std::filesystem::path& local_home_dir,
        const int cloud_id,
        const std::string& start_page_token
    );

    void setupUploadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const override;

    void setupUpdateHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileModifiedDTO>& dto) const override;
    void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const override;
    void setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) const override;

    std::vector<std::unique_ptr<ChangeDTO>> scanForChanges(std::shared_ptr<Database> db_conn) override;
    std::vector<std::unique_ptr<ChangeDTO>> initialFiles() override;

    const int id() const override;

    /* std::unique_ptr<CurlEasyHandle> create_file_upload_handle(const std::filesystem::path& path, const std::string& parent = "") override;
    std::unique_ptr<CurlEasyHandle> create_dir_upload_handle(const std::filesystem::path& path, const std::string& parent = "") override;
    std::unique_ptr<CurlEasyHandle> create_file_download_handle(const std::string& id, const std::filesystem::path& path) override;
    std::unique_ptr<CurlEasyHandle> create_file_update_handle(const std::string& id, const std::filesystem::path& path, const std::string& name = "") override;
    std::unique_ptr<CurlEasyHandle> create_file_delete_handle(const std::string& id) override;
    std::unique_ptr<CurlEasyHandle> create_name_update_handle(const std::string& id, const std::string& name) override;
    std::unique_ptr<CurlEasyHandle> create_parent_update_handle(const std::string& id, const std::string& parent, const std::string& parent_to_remove = "") override;
    std::vector<nlohmann::json> get_changes(const int cloud_id, std::shared_ptr<Database> db_conn) override; */
    void proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const override;

    void proccesUpdate(std::unique_ptr<FileModifiedDTO>& dto, const std::string& response) const override;
    void proccesDownload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const override;
    void proccesDelete(std::unique_ptr<FileDeletedDTO>& dto, const std::string& response) const override;
    ~Dropbox() = default;

private:
    void generate_auth_url();
    std::string get_refresh_token(const std::string& auth_code);
    std::string get_access_token();
    std::string first_time_auth();
    void get_start_page_token();

    std::filesystem::path _home_path;
    std::filesystem::path _local_home_path;
    const std::string _client_id;
    const std::string _client_secret;
    std::string _refresh_token;
    std::string _access_token;
    std::string _page_token;

    const int _id;
};
