// tests/DatabaseTests.cpp

#include <gtest/gtest.h>
#include "database.h"
#include <nlohmann/json.hpp>
#include "logger.h"

using json = nlohmann::json;

class DatabaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        db = std::make_unique<Database>(":memory:");
    }

    std::unique_ptr<Database> db;
};

TEST_F(DatabaseTest, CloudConfigCRUD) {
    json cfg;
    cfg["foo"] = 123;
    cfg["bar"] = "baz";

    int cid = db->add_cloud("mycloud", CloudProviderType::Dropbox, cfg);
    CloudResolver::registerCloud(cid, "Dropbox");

    ASSERT_GT(cid, 0);

    auto got = db->get_cloud_config(cid);
    EXPECT_EQ(got, cfg);

    auto list = db->get_clouds();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0]["config_id"], cid);
    EXPECT_EQ(list[0]["type"], "Dropbox");
    EXPECT_EQ(list[0]["config_data"], cfg);

    json newCfg = { {"foo", 999} };
    db->update_cloud_data(cid, newCfg);
    auto got2 = db->get_cloud_config(cid);
    EXPECT_EQ(got2, newCfg);

    EXPECT_EQ(db->get_cloud_type(cid), "Dropbox");
}

TEST_F(DatabaseTest, FileTableBasicOps) {
    FileRecordDTO dto{
        EntryType::File,
        std::filesystem::path("foo.txt"),
        /*size=*/ 42,
        /*mod_time=*/ 1000,
        /*hash=*/ 0xdeadbeef,
        /*file_id=*/ 7
    };
    int gid = db->add_file(dto);
    ASSERT_GT(gid, 0);

    EXPECT_TRUE(db->quickPathCheck("foo.txt"));
    EXPECT_EQ(db->getGlobalIdByPath("foo.txt"), gid);

    EXPECT_EQ(db->getGlobalIdByFileId(7), gid);

    auto rec1 = db->getFileByGlobalId(gid);
    ASSERT_TRUE(rec1);
    EXPECT_EQ(rec1->file_id, 7u);
    EXPECT_EQ(rec1->rel_path, std::filesystem::path("foo.txt"));
    EXPECT_EQ(rec1->size, 42u);

    auto rec2 = db->getFileByFileId(7);
    ASSERT_TRUE(rec2);
    EXPECT_EQ(rec2->global_id, gid);
    EXPECT_EQ(rec2->rel_path, std::filesystem::path("foo.txt"));
}

TEST_F(DatabaseTest, FileLinksCRUD) {
    json cfg;
    cfg["foo"] = 123;
    cfg["bar"] = "baz";

    int cid = db->add_cloud("mycloud", CloudProviderType::Dropbox, cfg);
    CloudResolver::registerCloud(cid, "Dropbox");
    ASSERT_GT(cid, 0);

    FileRecordDTO fdto{
        EntryType::File,
        std::filesystem::path("bar.bin"),
        /*size=*/ 123,
        /*mod_time=*/ 2000,
        /*hash=*/ 0x42,
        /*file_id=*/ 99
    };
    int gid = db->add_file(fdto);
    ASSERT_GT(gid, 0);

    FileRecordDTO link_dto{
        /*g_id=*/ gid,
        /*c_id=*/ cid,
        /*parent=*/ "root",
        /*cf_id=*/ "cloud-file-xyz",
        /*size=*/ 123,
        /*hash*/ std::string("ffee"),
        /*mod_time=*/ 2000
    };
    db->add_file_link(link_dto);

    EXPECT_EQ(db->getCloudFileIdByPath("bar.bin", cid), "cloud-file-xyz");

    EXPECT_EQ(db->get_cloud_parent_id_by_cloud_id(cid, "cloud-file-xyz"), "root");

    auto cloudRec = db->getFileByCloudIdAndCloudFileId(cid, "cloud-file-xyz");
    ASSERT_TRUE(cloudRec);
    EXPECT_EQ(cloudRec->global_id, gid);
    EXPECT_EQ(cloudRec->cloud_file_id, "cloud-file-xyz");
    EXPECT_EQ(std::get<std::string>(cloudRec->cloud_hash_check_sum), "ffee");
}

TEST_F(DatabaseTest, DeleteFileAndLinks) {
    FileRecordDTO fdto{ EntryType::File, "todelete.txt", 1, 0, 0, 10 };
    int gid = db->add_file(fdto);
    ASSERT_GT(gid, 0);

    json cfg;
    cfg["foo"] = 123;
    cfg["bar"] = "baz";

    int cid = db->add_cloud("mycloud", CloudProviderType::Dropbox, cfg);
    CloudResolver::registerCloud(cid, "Dropbox");
    ASSERT_GT(cid, 0);

    db->add_file_link({ gid, cid, "p", "fid", 1, std::string("h"), 0 });

    EXPECT_NO_THROW(db->getFileByGlobalId(gid));
    EXPECT_EQ(db->getCloudFileIdByPath("todelete.txt", cid), "fid");

    db->delete_file_and_links(gid);

    EXPECT_FALSE(db->quickPathCheck("todelete.txt"));
    EXPECT_EQ(db->getFileByGlobalId(gid), nullptr);
    EXPECT_EQ(db->getCloudFileIdByPath("todelete.txt", cid), std::string{});
}
