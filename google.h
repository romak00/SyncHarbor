#pragma once

#include "BaseStorage.h"
#include "command.h"
#include <string>
#include <iostream>

class GoogleDrive : public BaseStorage {
public:
    GoogleDrive(const std::string& client_id, const std::string& client_secret, const std::string& refresh_token, const std::filesystem::path& home_dir, const int cloud_id);
    GoogleDrive(const std::string& client_id, const std::string& client_secret, const std::string& refresh_token, const std::filesystem::path& home_dir, const int cloud_id, const std::string& start_page_token);

    //void initial_config() override;
    void setupUploadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const override;
    //std::unique_ptr<CurlEasyHandle> create_dir_upload_handle(const std::filesystem::path& path, const std::string& parent = "") override;
    std::vector<std::unique_ptr<ChangeDTO>> scanForChanges(std::shared_ptr<Database> db_conn) override;
    std::string post_upload();
    void insert_path_id_mapping(const std::string& path, const std::string& id);
    const std::string get_path_id_mapping(const std::string& path) const;
    //std::unique_ptr<CurlEasyHandle> create_parent_update_handle(const std::string& id, const std::string& parent, const std::string& parent_to_remove = "") override;
    const std::string& get_home_dir_id() const;
    //std::unique_ptr<CurlEasyHandle> create_file_download_handle(const std::string& id, const std::filesystem::path& path) override;
    //std::unique_ptr<CurlEasyHandle> create_file_update_handle(const std::string& id, const std::filesystem::path& path, const std::string& name = "") override;
    //std::unique_ptr<CurlEasyHandle> create_file_delete_handle(const std::string& id) override;
    //std::unique_ptr<CurlEasyHandle> create_name_update_handle(const std::string& id, const std::string& name) override;
    void proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const override;
    ~GoogleDrive() = default;

private:
    std::string get_dir_id_by_path(const std::filesystem::path& path);
    void generateAuthURL();
    std::string getRefreshToken(const std::string& auth_code);
    std::string getAccessToken();
    std::string first_time_auth();
    void get_start_page_token();

    std::unordered_map<std::string, std::string> _dir_id_map;
    const std::string _client_id;
    const std::string _client_secret;
    std::string _refresh_token;
    std::string _access_token;
    std::string _home_dir_id;
    std::string _page_token;

    std::filesystem::path _local_home_path;

    const int _id;
};