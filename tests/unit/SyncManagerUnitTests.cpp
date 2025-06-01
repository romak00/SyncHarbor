#include <gtest/gtest.h>
#include "sync-manager.h"

#include <filesystem>
#include <fstream>

class SyncManagerUnitTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto a_tmp_dir = std::filesystem::temp_directory_path() / "-test-local_storage-";

        std::error_code ec;
        std::filesystem::remove_all(a_tmp_dir, ec);
        std::filesystem::create_directory(a_tmp_dir);

        tmp = std::filesystem::canonical(a_tmp_dir);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tmp, ec);
    }

    std::filesystem::path tmp;
};

TEST_F(SyncManagerUnitTest, SetLocalDirCreatesDirectory) {
    auto tmp1 = tmp / "sm_test_create";
    SyncManager sm("util_db1.sqlite3", SyncManager::Mode::Daemon);
    EXPECT_FALSE(std::filesystem::exists(tmp1));
    EXPECT_NO_THROW(sm.setLocalDir(tmp1.string()));
    EXPECT_TRUE(std::filesystem::exists(tmp1));
    EXPECT_TRUE(std::filesystem::is_directory(tmp1));
    std::filesystem::remove_all(tmp1);
}

TEST_F(SyncManagerUnitTest, SetLocalDirThrowsIfNotDirectory) {
    auto tmp1 = tmp / "not_a_dir.txt";
    std::filesystem::create_directories(tmp1.parent_path());
    {
        std::ofstream out(tmp1);
        out << "hello";
    }
    SyncManager sm("util_db2.sqlite3", SyncManager::Mode::Daemon);
    EXPECT_THROW(sm.setLocalDir(tmp1.string()), std::runtime_error);
    std::filesystem::remove_all(tmp1.parent_path());
}

TEST_F(SyncManagerUnitTest, DirectoryIsWritableTrue) {
    auto tmp1 = tmp / "sm_test_writable";
    std::filesystem::create_directories(tmp1);
    SyncManager sm("util_db3.sqlite3", SyncManager::Mode::Daemon);
    EXPECT_TRUE(sm.directoryIsWritable(tmp1));
    std::filesystem::remove_all(tmp1);
}

TEST_F(SyncManagerUnitTest, CheckLocalPermissionsOnEmptyDir) {
    auto tmp1 = tmp / "sm_test_perm_empty";
    std::filesystem::create_directories(tmp1);
    SyncManager sm("util_db4.sqlite3", SyncManager::Mode::Daemon);
    sm.setLocalDir(tmp1.string());
    auto bad = sm.checkLocalPermissions();
    EXPECT_TRUE(bad.empty());
    std::filesystem::remove_all(tmp1);
}

TEST_F(SyncManagerUnitTest, CheckLocalPermissionsThrowsOnInvalid) {
    SyncManager sm("util_db5.sqlite3", SyncManager::Mode::Daemon);
    EXPECT_THROW(sm.checkLocalPermissions(), std::runtime_error);
}

TEST_F(SyncManagerUnitTest, LoadConfigThrowsOnMissingFile) {
    auto cfg = tmp / "sm_test_load_missing" / "nofile.json";
    EXPECT_THROW(
        SyncManager(
            cfg.string(),
            "util_db6.sqlite3",
            SyncManager::Mode::InitialSync),
        std::runtime_error
    );
}

TEST_F(SyncManagerUnitTest, LoadConfigSucceedsOnEmptyClouds) {
    auto dir = tmp / "sm_test_load_ok";
    std::filesystem::create_directories(dir);
    auto cfg = dir / "config.json";
    {
        std::ofstream out(cfg);
        out << "{ \"clouds\": [], \"local\": \"" << dir.string() << "\" }";
    }
    EXPECT_NO_THROW(
        SyncManager(
            cfg.string(),
            "util_db7.sqlite3",
            SyncManager::Mode::InitialSync)
    );
    std::filesystem::remove_all(dir);
}
