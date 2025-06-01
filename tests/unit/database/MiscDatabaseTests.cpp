#include "DatabaseTestFixture.h"

TEST_F(DatabaseUnitTest, InitialSyncFlag) {
    EXPECT_FALSE(db->isInitialSyncDone());
    db->markInitialSyncDone();
    EXPECT_TRUE(db->isInitialSyncDone());
}

TEST_F(DatabaseUnitTest, ExecuteBadSql) {
    EXPECT_THROW(db->execute("BAD SQL;"), std::runtime_error);
}

TEST_F(DatabaseUnitTest, CheckRcThrows) {
    EXPECT_THROW(db->check_rc(SQLITE_ERROR, "ctx"), std::runtime_error);
}

TEST_F(DatabaseUnitTest, MissingPathPart) {
    auto miss = db->getMissingPathPart("a/b/c.txt", 1);

    EXPECT_EQ(miss, std::filesystem::path("a/b/c.txt"));

    FileRecordDTO d1{ EntryType::Directory,"a",0,0,0,10 };
    json cfg = { {"foo",1} };
    int cid = db->add_cloud("c", CloudProviderType::Dropbox, cfg);
    int g1 = db->add_file(d1);
    db->add_file_link({ g1, cid, "p", "id", 1, std::string("h"), 1 });

    auto miss2 = db->getMissingPathPart("a/b/c.txt", 1);
    EXPECT_EQ(miss2, std::filesystem::path("b/c.txt"));
}

TEST_F(DatabaseUnitTest, UpdateFileAndLinkOperations) {
    json cfg = { {"foo",1} };
    int cid = db->add_cloud("c", CloudProviderType::Dropbox, cfg);
    CloudResolver::registerCloud(cid, "Dropbox");

    FileRecordDTO dto{ EntryType::File,"u.txt",7,7,7,70 };
    int gid = db->add_file(dto);
    db->add_file_link({ gid,cid,"pp","cfid",8,std::string("zzz"),9 });

    FileUpdatedDTO udto{ EntryType::File,gid,123u,321,"u.txt",7,70 };
    udto.cloud_id = cid;
    db->update_file_link(udto);
    db->update_file(udto);

    FileMovedDTO mdto{ EntryType::File,gid,cid,"cfid2", 111, "u.txt","v.txt","pp2","ppp2"};
    db->update_file_link(mdto);
    db->update_file(mdto);

    EXPECT_EQ(db->getCloudFileIdByPath("v.txt", cid), "cfid2");
    EXPECT_EQ(db->getPathByGlobalId(gid), std::filesystem::path("v.txt"));
}

TEST_F(DatabaseUnitTest, DeleteFileAndLinksCascade) {
    json cfg = { {"x",2} };
    int cid = db->add_cloud("c2", CloudProviderType::Dropbox, cfg);
    CloudResolver::registerCloud(cid, "Dropbox");

    FileRecordDTO dto{ EntryType::File,"d.txt",1,1,1,50 };
    int gid = db->add_file(dto);
    db->add_file_link({ gid,cid,"pp","cid",1,std::string("h"),1 });

    EXPECT_TRUE(db->quickPathCheck("d.txt"));
    EXPECT_EQ(db->getCloudFileIdByPath("d.txt", cid), "cid");

    db->delete_file_and_links(gid);

    EXPECT_FALSE(db->quickPathCheck("d.txt"));
    EXPECT_EQ(db->getFileByGlobalId(gid), nullptr);
    EXPECT_EQ(db->getCloudFileIdByPath("d.txt", cid), "");
}

TEST_F(DatabaseUnitTest, ExecuteAndCheckRc) {
    EXPECT_NO_THROW(db->execute("SELECT 1;"));
    EXPECT_THROW(db->execute("THIS IS BAD;"), std::runtime_error);

    EXPECT_NO_THROW(db->check_rc(SQLITE_OK, "ctx"));
    EXPECT_THROW(db->check_rc(SQLITE_ERROR, "ctx"), std::runtime_error);
}

TEST_F(DatabaseUnitTest, GetPathByGlobalIdNotFound) {
    EXPECT_THROW(db->getPathByGlobalId(42), std::runtime_error);
}

TEST_F(DatabaseUnitTest, UpdateCloudDataPrepareError) {
    db->execute("DROP TABLE cloud_configs;");
    EXPECT_THROW(db->update_cloud_data(1, json::object()), std::runtime_error);
}

TEST_F(DatabaseUnitTest, UpdateFileLinkAndFilePrepareError) {
    FileUpdatedDTO ud{ EntryType::File, 1, 0, 0, "x", 0, 0 };
    ud.cloud_id = 1;
    db->execute("DROP TABLE file_links;");
    EXPECT_THROW(db->update_file_link(ud), std::runtime_error);

    db->execute("DROP TABLE files;");
    EXPECT_THROW(db->update_file(ud), std::runtime_error);
}

TEST_F(DatabaseUnitTest, UpdateFileMovedDTOPrepareError) {
    FileMovedDTO md{ EntryType::File, 1, 1, "cid", 0, "o", "n", "pp", "np" };
    db->execute("DROP TABLE file_links;");
    EXPECT_THROW(db->update_file_link(md), std::runtime_error);

    db->execute("DROP TABLE files;");
    EXPECT_THROW(db->update_file(md), std::runtime_error);
}
