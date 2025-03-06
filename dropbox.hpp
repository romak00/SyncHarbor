#pragma once

#include "base_cloud.hpp"
#include <string>
#include <iostream>

class Dropbox : public BaseCloud {
public:
    Dropbox(const std::string& client_id, const std::string& client_secret, const std::string& access_token, const std::filesystem::path& home_dir);
    Dropbox(const std::string& client_id, const std::string& client_secret, const std::string& refresh_token, const std::filesystem::path& home_dir, const std::string& start_page_token);

    std::unique_ptr<CurlEasyHandle> create_file_upload_handle(const std::filesystem::path& path, const std::string& parent = "") override;
    std::unique_ptr<CurlEasyHandle> create_dir_upload_handle(const std::filesystem::path& path, const std::string& parent = "") override;
    std::unique_ptr<CurlEasyHandle> create_file_download_handle(const std::string& id, const std::filesystem::path& path) override;
    std::unique_ptr<CurlEasyHandle> create_file_update_handle(const std::string& id, const std::filesystem::path& path, const std::string& name = "") override;
    std::unique_ptr<CurlEasyHandle> create_file_delete_handle(const std::string& id) override;
    std::unique_ptr<CurlEasyHandle> create_name_update_handle(const std::string& id, const std::string& name) override;
    std::unique_ptr<CurlEasyHandle> create_parent_update_handle(const std::string& id, const std::string& parent, const std::string& parent_to_remove = "") override;
    std::vector<nlohmann::json> get_changes(const int cloud_id, std::shared_ptr<Database> db_conn) override;
    std::string post_upload() override;
    const std::string& get_home_dir_id() const override;
    void insert_path_id_mapping(const std::string& path, const std::string& id) override;
    const std::string get_path_id_mapping(const std::string& path) const override;
    void procces_responce(FileLinkRecord& file_info, const nlohmann::json& responce) override;
    ~Dropbox();

private:
    void generate_auth_url();
    std::string get_refresh_token(const std::string& auth_code);
    std::string get_access_token();
    std::string first_time_auth();
    void get_start_page_token();

    std::unordered_map<std::string, std::string> _dir_id_map;
    const std::string _client_id;
    const std::string _client_secret;
    std::string _refresh_token;
    std::string _access_token;
    std::string _home_dir_id;
    std::string _page_token;
};
