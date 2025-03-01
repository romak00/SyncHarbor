#pragma once

#include "utils.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <memory>
class BaseCloud {
public:
    virtual ~BaseCloud() {}
    //virtual void initial_config() = 0;
    virtual std::unique_ptr<CurlEasyHandle> create_file_upload_handle(const std::filesystem::path&) = 0;
    virtual std::unique_ptr<CurlEasyHandle> create_dir_upload_handle(const std::filesystem::path&) = 0;
    virtual std::vector<nlohmann::json> list_changes() = 0;
    virtual std::string post_upload() = 0;
    virtual void insert_path_id_mapping(const std::string&, const std::string&) = 0;
    virtual const std::string get_path_id_mapping(const std::string&) const = 0;
    virtual void add_to_batch(const std::string&, const std::string&) = 0;
    virtual std::unique_ptr<CurlEasyHandle> patch_change_parent(const std::string&, const std::string&) = 0;
    virtual const std::string& get_home_dir_id() const = 0;
};