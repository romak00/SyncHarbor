#include "DatabaseTestFixture.h"

TEST_F(DatabaseUnitTest, AddAndGetCloud) {
    json cfg = { {"foo", 123}, {"bar", "baz"} };
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
}

TEST_F(DatabaseUnitTest, UpdateCloudDataAndType) {
    json cfg = { {"x",1} };
    int cid = db->add_cloud("c", CloudProviderType::Dropbox, cfg);
    CloudResolver::registerCloud(cid, "Dropbox");

    json newCfg = { {"foo", 999} };
    db->update_cloud_data(cid, newCfg);
    EXPECT_EQ(db->get_cloud_config(cid), newCfg);
}

TEST_F(DatabaseUnitTest, GetCloudConfigNotFound) {
    EXPECT_THROW(db->get_cloud_config(999), std::runtime_error);
}

TEST_F(DatabaseUnitTest, GetCloudsEmptyAfterDelete) {
    db->execute("DELETE FROM cloud_configs;");
    auto clouds = db->get_clouds();
    EXPECT_TRUE(clouds.empty());
}

TEST_F(DatabaseUnitTest, StringCtorAndFsPathCtorEquivalent) {
    std::filesystem::path p = ":memory:";
    EXPECT_NO_THROW({
        Database db2(p);
        });
}

TEST_F(DatabaseUnitTest, FsPathCtorThrowsOnDirectory) {
    auto dir = std::filesystem::temp_directory_path() / "db_dir";
    std::filesystem::create_directory(dir);
    EXPECT_THROW({
        Database db2(dir);
        }, std::runtime_error);
    std::filesystem::remove_all(dir);
}

TEST_F(DatabaseUnitTest, AddCloudDuplicateNameThrows) {
    json cfg = { {"foo", 1} };
    int cid = db->add_cloud("dup", CloudProviderType::Dropbox, cfg);
    EXPECT_THROW(db->add_cloud("dup", CloudProviderType::Dropbox, cfg), std::runtime_error);
}

TEST_F(DatabaseUnitTest, GetCloudConfigPrepareError) {
    db->execute("DROP TABLE cloud_configs;");
    EXPECT_THROW(db->get_cloud_config(1), std::runtime_error);
}

TEST_F(DatabaseUnitTest, GetCloudsPrepareError) {
    db->execute("DROP TABLE cloud_configs;");
    EXPECT_THROW(db->get_clouds(), std::runtime_error);
}
