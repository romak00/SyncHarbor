#pragma once

#include <sqlite3.h>
#include <vector>
#include <chrono>
#include "utils.h"

class ICommand;


class Database {
public:
    Database(const std::string& db_path);
    ~Database();

    int add_cloud(
        const std::string& name,
        const CloudProviderType type,
        const nlohmann::json& config_data);
    nlohmann::json get_cloud_config(const int cloud_id);
    void update_cloud_data(const int cloud_id, const nlohmann::json& data);
    std::filesystem::path getPathByGlobalId(const int search_global_id);
    std::string get_cloud_file_id_by_cloud_id(const int cloud_id, const int global_id);
    std::vector<nlohmann::json> get_clouds();
    std::string get_cloud_type(const int cloud_id);
    std::string get_cloud_parent_id_by_cloud_id(const int cloud_id, const std::string& cloud_file_id);
    std::unique_ptr<FileRecordDTO> getFileByCloudIdAndCloudFileId(const int cloud_id, const std::string& cloud_file_id);
    
    int add_file(const FileRecordDTO& dto);
    void add_file_link(const FileRecordDTO& dto);

    void update_file_link(const FileUpdatedDTO& dto);
    void update_file(const FileUpdatedDTO& dto);

    void update_file_link(const FileMovedDTO& dto);
    void update_file(const FileMovedDTO& dto);

    void delete_file_and_links(const int global_id);
    std::unique_ptr<FileRecordDTO> getFileByFileId(const uint64_t file_id);
    std::unique_ptr<FileRecordDTO> getFileByGlobalId(const int global_id);
    int getGlobalIdByFileId(const uint64_t file_id);
    int getGlobalIdByPath(const std::filesystem::path& path);

    std::string getCloudFileIdByPath(const std::filesystem::path& path, const int cloud_id);

    bool quickPathCheck(const std::filesystem::path& path);

    std::filesystem::path getMissingPathPart(const std::filesystem::path& path, const int num_clouds);

    std::string getParentId(const int cloud_id, const int global_id);

    bool isInitialSyncDone();
    void markInitialSyncDone();


private:
    sqlite3* _db;
    void check_rc(int rc, const std::string& context);
    void create_tables();
    void execute(const std::string& sql);
};

