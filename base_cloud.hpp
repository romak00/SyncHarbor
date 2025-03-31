#pragma once

#include "utils.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <memory>
#include "database.hpp"

class BaseCloud {
public:
    virtual ~BaseCloud() {}
    //virtual void initial_config() = 0;
    virtual std::unique_ptr<CurlEasyHandle> create_file_upload_handle(const std::filesystem::path&, const std::string & = "") = 0;
    virtual std::unique_ptr<CurlEasyHandle> create_dir_upload_handle(const std::filesystem::path&, const std::string & = "") = 0;
    virtual std::unique_ptr<CurlEasyHandle> create_file_update_handle(const std::string&, const std::filesystem::path&, const std::string& name = "") = 0;
    virtual std::unique_ptr<CurlEasyHandle> create_file_download_handle(const std::string&, const std::filesystem::path&) = 0;
    virtual std::unique_ptr<CurlEasyHandle> create_parent_update_handle(const std::string&, const std::string&, const std::string& parent_to_remove = "") = 0;
    virtual std::unique_ptr<CurlEasyHandle> create_name_update_handle(const std::string&, const std::string&) = 0;
    virtual std::unique_ptr<CurlEasyHandle> create_file_delete_handle(const std::string&) = 0;
    virtual std::vector<nlohmann::json> get_changes(const int, std::shared_ptr<Database>) = 0;
    virtual std::string post_upload() = 0;
    virtual void insert_path_id_mapping(const std::string&, const std::string&) = 0;
    virtual const std::string get_path_id_mapping(const std::string&) const = 0;
    virtual const std::string& get_home_dir_id() const = 0;
    virtual void procces_response(FileLinkRecord& file_info, const nlohmann::json& response) = 0;
};