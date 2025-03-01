#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "utils.hpp"

class Database {
public:
    Database(const std::string& db_path);
    ~Database();

    void add_cloud(const std::string& name, const std::string& type, const nlohmann::json& config_data);
    nlohmann::json get_cloud_config(const int cloud_id);
    int add_file(const std::filesystem::path& path, const std::string& type, const size_t& mod_time);
    void initial_add_file_links(const std::vector<FileLinkRecord>& file);
    void update_cloud_data(const int cloud_id, const nlohmann::json& data);
    std::filesystem::path find_path_by_global_id(const int search_global_id);
    std::string get_cloud_file_id_by_cloud_id(const int cloud_id, const int global_id);
    std::vector<nlohmann::json> get_clouds();
    void update_file_link_one_field(const int cloud_id, const int global_id, const std::string& field, const std::string& new_str);

private:
    sqlite3* _db;
    void check_rc(int rc, const std::string& context);
    void create_tables();
    void execute(const std::string& sql);
};

