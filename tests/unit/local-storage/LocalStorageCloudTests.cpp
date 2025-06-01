#include "LocalStorageTestFixture.h"
#include <fstream>

TEST_F(LocalStorageUnitTest, ProccesUploadLocal) {
    auto dto = std::make_unique<FileRecordDTO>(
        EntryType::File, "u.txt", 0, 0, 0, 0
    );
    dto->cloud_id = 0;
    std::ofstream(tmp / "u.txt") << "X";
    ls->proccesUpload(dto, "");
    EXPECT_TRUE(db->quickPathCheck("u.txt"));
}

TEST_F(LocalStorageUnitTest, ProccesUploadCloud) {
    auto dto = std::make_unique<FileRecordDTO>(
        EntryType::File,
        std::filesystem::path("v.txt"),
        "cfi",
        0,
        0,
        "hash",
        cid
    );

    auto tmpf = tmp / (std::string(".-tmp-SyncHarbor-") + dto->rel_path.filename().string());
    std::ofstream(tmpf) << "Y";

    ls->proccesUpload(dto, "");

    auto links = db->getCloudFileIdByPath("v.txt", cid);
    EXPECT_FALSE(links.empty());
}

TEST_F(LocalStorageUnitTest, ProccesDeleteLocalNoop) {
    auto dto = std::make_unique<FileDeletedDTO>("d.txt", 1, 0);
    EXPECT_NO_THROW(ls->proccesDelete(dto, ""));
}

TEST_F(LocalStorageUnitTest, ProccesDeleteCloud) {
    auto dto = std::make_unique<FileDeletedDTO>("d.txt", 1, 0);
    dto->cloud_id = cid;
    EXPECT_NO_THROW(ls->proccesDelete(dto, ""));
}

TEST_F(LocalStorageUnitTest, ProccesUpdateLocalNoop) {
    auto dto = std::make_unique<FileUpdatedDTO>(
        EntryType::File, 1, 0, 0, "p.txt", 0, 0
    );
    dto->cloud_id = 0;
    EXPECT_NO_THROW(ls->proccesUpdate(dto, ""));
}

TEST_F(LocalStorageUnitTest, ProccesUpdateRemote) {
    auto tmpf = tmp / (std::string(".-tmp-SyncHarbor-") + "q.txt");
    auto real = tmp / "q.txt";
    std::ofstream(tmpf) << "T";
    std::ofstream(real) << "R";

    uint64_t fid = ls->getFileId(real);
    uint64_t nfid = ls->getFileId(tmpf);
    FileRecordDTO base_rec{
        EntryType::File,
        "q.txt",
        std::filesystem::file_size(real),
        0,
        0,
        fid
    };
    int gid = db->add_file(base_rec);
    ASSERT_GT(gid, 0);

    base_rec.global_id = gid;
    base_rec.cloud_id = cid;
    base_rec.cloud_file_id = "cloud-q";
    db->add_file_link(base_rec);

    auto dto = std::make_unique<FileUpdatedDTO>(
        EntryType::File,
        gid,
        0,
        100,
        std::filesystem::path("q.txt"),
        std::filesystem::file_size(real),
        fid
    );
    dto->cloud_id = cid;
    
    EXPECT_NO_THROW(ls->proccesUpdate(dto, ""));
    auto mtime = convertSystemTime(real);
    auto hash = ls->computeFileHash(real);

    auto rec = db->getFileByFileId(nfid);
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(std::get<uint64_t>(rec->cloud_hash_check_sum), hash);
    EXPECT_EQ(rec->cloud_file_modified_time, mtime);
}

TEST_F(LocalStorageUnitTest, ProccesMoveLocal) {
    auto dto = std::make_unique<FileMovedDTO>(
        EntryType::File, 1, 0,
        "o.txt", "n.txt"
    );
    std::ofstream(tmp / "n.txt") << "N";
    dto->cloud_id = 0;
    EXPECT_NO_THROW(ls->proccesMove(dto, ""));
}

TEST_F(LocalStorageUnitTest, ProccesMoveRemote) {
    auto oldf = tmp / "old.txt"; std::ofstream(oldf) << "O";
    FileRecordDTO rec{ EntryType::File,"old.txt",1,0,0,ls->getFileId(oldf) };
    int gid = db->add_file(rec); ASSERT_GT(gid, 0);
    db->add_file_link(FileRecordDTO{ gid, cid,"","cloud",1,"",0 });

    std::ofstream(tmp / "new.txt") << "N";
    auto dto = std::make_unique<FileMovedDTO>(
        EntryType::File, gid, 0,
        "old.txt", "new.txt"
    );
    dto->cloud_id = cid;
    ls->proccesMove(dto, "");
    EXPECT_TRUE(db->quickPathCheck("new.txt"));
}

TEST_F(LocalStorageUnitTest, ProccesMoveCloudDirectoryRecursive) {
    std::filesystem::create_directories(tmp / "A" / "B");
    std::ofstream(tmp / "A" / "file1.txt") << "1";
    std::ofstream(tmp / "A" / "B" / "file2.txt") << "2";

    for (auto const& rel : { "A","A/B","A/file1.txt","A/B/file2.txt" }) {
        auto full = tmp / rel;
        uint64_t fid = ls->getFileId(full);
        FileRecordDTO dto{
            std::filesystem::is_directory(full) ? EntryType::Directory : EntryType::File,
            rel,
            std::filesystem::is_directory(full) ? 0 : std::filesystem::file_size(full),
            0,
            0,
            fid
        };
        int gid = db->add_file(dto);
        ASSERT_GT(gid, 0);
        dto.global_id = gid;
        dto.cloud_id = cid;
        dto.cloud_file_id = std::string("cloud-") + rel;
        db->add_file_link(dto);
    }

    auto target_gid = db->getGlobalIdByPath("A");

    auto dto = std::make_unique<FileMovedDTO>(
        EntryType::Directory,
        target_gid,
        cid,
        "cloud-A",
        0,
        std::filesystem::path("A"),
        std::filesystem::path("AR"),
        "par1",
        "par2"
    );

    EXPECT_NO_THROW(ls->proccesMove(dto, ""));
    EXPECT_TRUE(std::filesystem::exists(tmp / "AR"));

    std::vector<std::filesystem::path> expected = {
        tmp / "AR",
        tmp / "AR" / "B",
        tmp / "AR" / "file1.txt",
        tmp / "AR" / "B" / "file2.txt"
    };
    for (auto const& full_new : expected) {
        uint64_t fid = ls->getFileId(full_new);
        ASSERT_NE(fid, 0u);
        auto rec = db->getFileByFileId(fid);
        ASSERT_NE(rec, nullptr);
        EXPECT_EQ(rec->rel_path, full_new.lexically_relative(tmp));
    }
}
