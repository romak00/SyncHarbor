#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "utils.h"

class ICommand;


class Database {
public:
    Database(const std::string& db_path);
    ~Database();

    int add_cloud(const std::string& name, const std::string& type, const nlohmann::json& config_data);
    nlohmann::json get_cloud_config(const int cloud_id);
    int add_file(const std::unique_ptr<FileRecordDTO>& dto);
    void initial_add_file_links(const std::vector<FileRecordDTO>& file);
    void update_cloud_data(const int cloud_id, const nlohmann::json& data);
    std::filesystem::path find_path_by_global_id(const int search_global_id);
    std::string get_cloud_file_id_by_cloud_id(const int cloud_id, const int global_id);
    std::vector<nlohmann::json> get_clouds();
    std::string get_cloud_type(const int  cloud_id);
    std::string get_cloud_parent_id_by_cloud_id(const int cloud_id, const std::string& cloud_file_id);
    int get_global_id_by_cloud_id(const int cloud_id, const std::string& cloud_file_id);
    void update_file_link_one_field(const int cloud_id, const int global_id, const std::string& field, const std::string& new_str);
    void update_file_time(const int global_id, const std::string& field, const std::time_t& time);
    void update_file_path(const int global_id, const std::string& field, const std::string& path);
    nlohmann::json get_cloud_file_info(const std::string& cloud_file_id, const int cloud_id);
    void add_file_link(const std::unique_ptr<FileRecordDTO>& dto);
    void update_file_link(const int cloud_id, const int global_id, const std::string& hash, const int mod_time, const std::string& parent_id, const std::string& cloud_file_id);
    void delete_file_and_links(const int global_id);

private:
    sqlite3* _db;
    void check_rc(int rc, const std::string& context);
    void create_tables();
    void execute(const std::string& sql);
};

