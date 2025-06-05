#pragma once

#include <sqlite3.h>
#include <vector>
#include "utils.h"
#include <nlohmann/json.hpp>

class Database {
public:
    Database(const std::string& db_path);
    Database(const std::filesystem::path& db_path);
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

    std::unique_ptr<FileRecordDTO> getFileByCloudIdAndCloudFileId(const int cloud_id, const std::string& cloud_file_id);
    std::unique_ptr<FileRecordDTO> getFileByCloudIdAndGlobalId(const int cloud_id, const int global_id);
    
    int add_file(const FileRecordDTO& dto);
    void add_file_link(const FileRecordDTO& dto);

    void update_file_link(const FileUpdatedDTO& dto);
    void update_file(const FileUpdatedDTO& dto);

    void update_file_link(const FileMovedDTO& dto);
    void update_file(const FileMovedDTO& dto);

    void delete_file_and_links(const int global_id);
    std::unique_ptr<FileRecordDTO> getFileByFileId(const uint64_t file_id);
    std::unique_ptr<FileRecordDTO> getFileByPath(const std::filesystem::path& path);
    std::unique_ptr<FileRecordDTO> getFileByGlobalId(const int global_id);
    int getGlobalIdByFileId(const uint64_t file_id);
    int getGlobalIdByPath(const std::filesystem::path& path);

    std::string getCloudFileIdByPath(const std::filesystem::path& path, const int cloud_id);

    bool quickPathCheck(const std::filesystem::path& path);

    std::filesystem::path getMissingPathPart(const std::filesystem::path& path, const int num_clouds);

    bool isInitialSyncDone();
    void markInitialSyncDone();

    void addLocalDir(const std::string& local_dir);
    std::string getLocalDir();


private:
    sqlite3* _db;
    void check_rc(int rc, const std::string& context);
    void create_tables();
    void execute(const std::string& sql);

#ifdef ENABLE_GTEST_FRIENDS
#include <gtest/gtest_prod.h>

    FRIEND_TEST(DatabaseUnitTest, GetCloudsEmptyAfterDelete);
    FRIEND_TEST(DatabaseUnitTest, ExecuteBadSql);
    FRIEND_TEST(DatabaseUnitTest, CheckRcThrows);
    FRIEND_TEST(DatabaseUnitTest, GetCloudConfigPrepareError);
    FRIEND_TEST(DatabaseUnitTest, GetCloudsPrepareError);
    FRIEND_TEST(DatabaseUnitTest, GetGlobalIdByFileIdPrepareError);
    FRIEND_TEST(DatabaseUnitTest, GetGlobalIdByPathPrepareError);
    FRIEND_TEST(DatabaseUnitTest, GetFileByFileIdPrepareError);
    FRIEND_TEST(DatabaseUnitTest, GetFileByPathPrepareError);
    FRIEND_TEST(DatabaseUnitTest, QuickPathCheckPrepareError);
    FRIEND_TEST(DatabaseUnitTest, GetCloudFileIdByPathPrepareError);
    FRIEND_TEST(DatabaseUnitTest, GetParentIdPrepareError);
    FRIEND_TEST(DatabaseUnitTest, GetCloudFileIdByCloudIdPrepareError);
    FRIEND_TEST(DatabaseUnitTest, ExecuteAndCheckRc);
    FRIEND_TEST(DatabaseUnitTest, GetPathByGlobalIdNotFound);
    FRIEND_TEST(DatabaseUnitTest, UpdateCloudDataPrepareError);
    FRIEND_TEST(DatabaseUnitTest, UpdateFileLinkAndFilePrepareError);
    FRIEND_TEST(DatabaseUnitTest, UpdateFileMovedDTOPrepareError);
#endif


};

