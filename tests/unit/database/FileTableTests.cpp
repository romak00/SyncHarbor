#include "DatabaseTestFixture.h"

TEST_F(DatabaseUnitTest, AddFileAndQueries) {
    FileRecordDTO dto{
        EntryType::File,
        std::filesystem::path("foo.txt"),
        42,
        1000,
        0xdeadbeef,
        7
    };
    int gid = db->add_file(dto);
    ASSERT_GT(gid, 0);

    EXPECT_TRUE(db->quickPathCheck("foo.txt"));
    EXPECT_EQ(db->getGlobalIdByPath("foo.txt"), gid);
    EXPECT_EQ(db->getGlobalIdByFileId(7), gid);

    auto byGlobal = db->getFileByGlobalId(gid);
    ASSERT_TRUE(byGlobal);
    EXPECT_EQ(byGlobal->file_id, 7u);
    EXPECT_EQ(byGlobal->rel_path, std::filesystem::path("foo.txt"));
    EXPECT_EQ(byGlobal->size, 42u);

    auto byFileId = db->getFileByFileId(7);
    ASSERT_TRUE(byFileId);
    EXPECT_EQ(byFileId->global_id, gid);
    EXPECT_EQ(byFileId->rel_path, std::filesystem::path("foo.txt"));
}

TEST_F(DatabaseUnitTest, GetGlobalIdByPathNotFound) {
    EXPECT_THROW(db->getGlobalIdByPath("no_such.txt"), std::runtime_error);
}

TEST_F(DatabaseUnitTest, GetGlobalIdByFileIdNotFound) {
    EXPECT_THROW(db->getGlobalIdByFileId(0xabcdef), std::runtime_error);
}

TEST_F(DatabaseUnitTest, GetFileByGlobalIdNotFound) {
    EXPECT_EQ(db->getFileByGlobalId(999), nullptr);
}

TEST_F(DatabaseUnitTest, GetFileByFileIdNotFound) {
    EXPECT_EQ(db->getFileByFileId(888), nullptr);
}

TEST_F(DatabaseUnitTest, GetGlobalIdByFileIdPrepareError) {
    db->execute("DROP TABLE files;");
    EXPECT_THROW(db->getGlobalIdByFileId(123), std::runtime_error);
}

TEST_F(DatabaseUnitTest, GetGlobalIdByPathPrepareError) {
    db->execute("DROP TABLE files;");
    EXPECT_THROW(db->getGlobalIdByPath("foo.txt"), std::runtime_error);
}

TEST_F(DatabaseUnitTest, GetFileByFileIdPrepareError) {
    db->execute("DROP TABLE files;");
    EXPECT_THROW(db->getFileByFileId(5), std::runtime_error);
}

TEST_F(DatabaseUnitTest, GetFileByPathPrepareError) {
    db->execute("DROP TABLE files;");
    EXPECT_THROW(db->getFileByPath("bar"), std::runtime_error);
}

TEST_F(DatabaseUnitTest, QuickPathCheckPrepareError) {
    db->execute("DROP TABLE files;");
    EXPECT_THROW(db->quickPathCheck("baz"), std::runtime_error);
}