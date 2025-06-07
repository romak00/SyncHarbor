#include "DatabaseTestFixture.h"


TEST_F(DatabaseUnitTest, AddAndQueryLink) {
    json cfg = { {"foo",123} };
    auto cid = db->add_cloud("cloud", CloudProviderType::Dropbox, cfg);
    FileRecordDTO fdto{
        EntryType::File,
        std::filesystem::path("bar.bin"),
        123,
        2000,
        0x42,
        99
    };
    auto gid = db->add_file(fdto);
    FileRecordDTO link{
        gid,
        cid,
        "root",
        "cloud-file-xyz",
        123,
        std::string("ffee"),
        2000
    };
    db->add_file_link(link);

    EXPECT_EQ(db->getCloudFileIdByPath("bar.bin", cid), "cloud-file-xyz");

    auto rec1 = db->getFileByCloudIdAndCloudFileId(cid, "cloud-file-xyz");
    ASSERT_TRUE(rec1);
    EXPECT_EQ(rec1->global_id, gid);
    EXPECT_EQ(rec1->cloud_file_id, "cloud-file-xyz");
    EXPECT_EQ(std::get<std::string>(rec1->cloud_hash_check_sum), "ffee");

    auto rec2 = db->getFileByCloudIdAndGlobalId(cid, gid);
    ASSERT_TRUE(rec2);
    EXPECT_EQ(rec2->cloud_file_id, "cloud-file-xyz");
}

TEST_F(DatabaseUnitTest, LinkNotFoundReturnsNull) {
    json cfg = { {"foo",123} };
    auto cid = db->add_cloud("cloud", CloudProviderType::Dropbox, cfg);
    FileRecordDTO fdto{
        EntryType::File,
        std::filesystem::path("bar.bin"),
        123,
        2000,
        0x42,
        99
    };
    auto gid = db->add_file(fdto);
    EXPECT_EQ(db->getFileByCloudIdAndCloudFileId(cid, "nope"), nullptr);
}

TEST_F(DatabaseUnitTest, get_cloud_file_id_by_cloud_idThrows) {
    json cfg = { {"foo",123} };
    auto cid = db->add_cloud("cloud", CloudProviderType::Dropbox, cfg);
    FileRecordDTO fdto{
        EntryType::File,
        std::filesystem::path("bar.bin"),
        123,
        2000,
        0x42,
        99
    };
    auto gid = db->add_file(fdto);
    EXPECT_THROW(db->get_cloud_file_id_by_cloud_id(cid, gid + 1), std::runtime_error);
}

TEST_F(DatabaseUnitTest, MissingPathPartAllExistsReturnsEmpty) {
    std::filesystem::path p1 = std::filesystem::path{ "a" };
    std::filesystem::path p2 = std::filesystem::path{ "a" } / "b";
    std::filesystem::path p3 = std::filesystem::path{ "a" } / "b" / "c.txt";

    FileRecordDTO d1{ EntryType::Directory, p1, 0,0,0, 10 };
    FileRecordDTO d2{ EntryType::Directory, p2, 0,0,0, 11 };
    FileRecordDTO f3{ EntryType::File,      p3, 1,1,1, 12 };
    
    int cid = db->add_cloud("c", CloudProviderType::Dropbox, nlohmann::json::object());
    int g1 = db->add_file(d1);
    int g2 = db->add_file(d2);
    int g3 = db->add_file(f3);
    db->add_file_link({ g1, cid, "p", "i1", 1, std::string("h"), 1 });
    db->add_file_link({ g2, cid, "p", "i2", 1, std::string("h"), 1 });
    db->add_file_link({ g3, cid, "p", "i3", 1, std::string("h"), 1 });

    auto miss = db->getMissingPathPart(std::filesystem::path{ "a" } / "b" / "c.txt", 1);
    EXPECT_TRUE(miss.empty());
}

TEST_F(DatabaseUnitTest, GetCloudFileIdByPathNoRow) {
    int cid = db->add_cloud("c2", CloudProviderType::Dropbox, nlohmann::json::object());
    EXPECT_EQ(db->getCloudFileIdByPath("foo", cid), "");
}

TEST_F(DatabaseUnitTest, GetCloudFileIdByPathPrepareError) {
    db->execute("DROP TABLE files;");
    int cid = db->add_cloud("c3", CloudProviderType::Dropbox, nlohmann::json::object());
    EXPECT_THROW(db->getCloudFileIdByPath("foo", cid), std::runtime_error);
}

TEST_F(DatabaseUnitTest, GetCloudFileIdByCloudIdPrepareError) {
    db->execute("DROP TABLE file_links;");
    EXPECT_THROW(db->get_cloud_file_id_by_cloud_id(1, 1), std::runtime_error);
}