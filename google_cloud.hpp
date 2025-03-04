#pragma once

#include "base_cloud.hpp"
#include <string>
#include <iostream>

class GoogleDrive : public BaseCloud {
public:
    GoogleDrive(const std::string& client_id, const std::string& client_secret, const std::string& refresh_token, const std::filesystem::path& home_dir, const std::string& start_page_token);
    GoogleDrive(const std::string& client_id, const std::string& client_secret, const std::string& refresh_token, const std::string& home_dir, const std::string& start_page_token);

    //void initial_config() override;
    std::unique_ptr<CurlEasyHandle> create_file_upload_handle(const std::filesystem::path& path) override;
    std::unique_ptr<CurlEasyHandle> create_dir_upload_handle(const std::filesystem::path& rel_path) override;
    std::vector<nlohmann::json> get_changes(const int cloud_id, std::shared_ptr<Database> db_conn) override;
    std::string post_upload() override;
    void insert_path_id_mapping(const std::string& path, const std::string& id) override;
    const std::string get_path_id_mapping(const std::string& path) const override;
    std::unique_ptr<CurlEasyHandle> create_metadata_update_handle(const std::string& id, const std::string& parent, const std::string& name = "") override;
    const std::string& get_home_dir_id() const override;
    std::unique_ptr<CurlEasyHandle> create_file_download_handle(const std::string& id, const std::filesystem::path& path) override;
    std::unique_ptr<CurlEasyHandle> create_file_update_handle(const std::string& id, const std::filesystem::path& path, const std::string& name, const std::string& parent_id) override;
    std::unique_ptr<CurlEasyHandle> create_file_delete_handle(const std::string& id) override;
    ~GoogleDrive();
    
private:
    std::string get_dir_id_by_path(const std::filesystem::path& path);
    void generateAuthURL();
    std::string getRefreshToken(const std::string& auth_code);
    std::string getAccessToken();
    std::string first_time_auth();
    void get_start_page_token();

    std::unordered_map<std::string, std::string> _dir_id_map;
    std::string _batch_body;
    const std::string _client_id;
    const std::string _client_secret;
    const std::string _refresh_token;
    std::string _access_token;
    std::string _home_dir_id;
    std::string _page_token;
};