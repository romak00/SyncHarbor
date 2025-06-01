#include "LocalStorageTestFixture.h"
#include <wtr/watcher.hpp>

TEST_F(LocalStorageUnitTest, OnFsEventAndProccessChanges) {
    bool fired = false;
    ls->setOnChange([&] { fired = true; });

    EXPECT_FALSE(ls->hasChanges());

    auto f = tmp / "xy.txt";
    std::ofstream(f) << "h";

    wtr::event ev {
        f,
            wtr::event::effect_type::create,
            wtr::event::path_type::file
    };

    ls->onFsEvent(ev);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(fired) << "Your onFsEvent didn't fire the onChange callback";

    auto changes = ls->proccessChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0]->getType(), ChangeType::New);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("xy.txt"));
}

TEST_F(LocalStorageUnitTest, HandleDeletedTrueDelete) {
    auto f = tmp / "del.txt";
    std::ofstream(f) << "X";
    FileRecordDTO dto{ EntryType::File, "del.txt", std::filesystem::file_size(f), 0, 0, ls->getFileId(f) };
    int gid = db->add_file(dto);
    ASSERT_GT(gid, 0);
    std::filesystem::remove(f);
    wtr::event ev {f, wtr::event::effect_type::destroy, wtr::event::path_type::file};
    ls->onFsEvent(ev);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto changes = ls->proccessChanges();
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::Delete);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("del.txt"));
}

TEST_F(LocalStorageUnitTest, HandleMovedTrueMoved) {
    auto old = tmp / "file.txt";
    std::ofstream(old) << "X";
    uint64_t fid = ls->getFileId(old);
    FileRecordDTO rec{ EntryType::File, "file.txt",
                       std::filesystem::file_size(old), 0, 0, fid };
    int gid = db->add_file(rec);
    ASSERT_GT(gid, 0);
    auto newf = tmp / "new_file.txt";
    std::filesystem::rename(old, newf);

    wtr::event ev {
        wtr::event{ old,
        wtr::event::effect_type::rename,
        wtr::event::path_type::file },
            wtr::event{ newf,
            wtr::event::effect_type::rename,
            wtr::event::path_type::file }
    };

    ls->onFsEvent(ev);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto changes = ls->proccessChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0]->getType(), ChangeType::Move);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("file.txt"));
}

TEST_F(LocalStorageUnitTest, HandleDeletedIgnoredBecauseExpected) {
    auto tmpf = tmp / ".-tmp-SyncHarbor-upd.txt";
    std::ofstream(tmpf) << "A";
    FileUpdatedDTO upd{ EntryType::File, 1, 0, 0, "upd.txt", 1, ls->getFileId(tmpf) };
    upd.cloud_id = cid;
    auto dto = std::make_unique<FileUpdatedDTO>(upd);
    ls->proccesUpdate(dto, "");
    wtr::event ev {tmpf, wtr::event::effect_type::destroy, wtr::event::path_type::file};
    ls->onFsEvent(ev);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto changes = ls->proccessChanges();
    EXPECT_TRUE(changes.empty());
}

TEST_F(LocalStorageUnitTest, HandleRenamedNoAssociatedCreatesNew) {
    auto f = tmp / "new.txt";
    std::ofstream(f) << "X";
    wtr::event ev {
        f,
            wtr::event::effect_type::rename,
            wtr::event::path_type::file
    };
    ls->onFsEvent(ev);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto changes = ls->proccessChanges();
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::New);
}

TEST_F(LocalStorageUnitTest, HandleUpdatedFakeAndReal) {
    auto f = tmp / "up.txt"; std::ofstream(f) << "X";
    uint64_t fid = ls->getFileId(f);
    FileRecordDTO rec{
        EntryType::File,
         "up.txt",
          std::filesystem::file_size(f),
        convertSystemTime(f),
        ls->computeFileHash(f),
        ls->getFileId(f) };
    int gid = db->add_file(rec);
    ASSERT_GT(gid, 0);
    FileEvent fake{ f, 50, ChangeType::Update };
    ls->onFsEvent({ f, wtr::event::effect_type::modify, wtr::event::path_type::file });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ls->_events_buff.push(fake);
    auto ch1 = ls->proccessChanges();

    EXPECT_TRUE(ch1.empty());
    std::ofstream(f, std::ios::app) << "Y";
    FileEvent real{ f, std::time(nullptr), ChangeType::Update };
    ls->_events_buff.push(real);
    auto ch2 = ls->proccessChanges();
    ASSERT_EQ(ch2.size(), 1);
    EXPECT_EQ(ch2[0]->getType(), ChangeType::Update);
}

TEST_F(LocalStorageUnitTest, OnFsEventWatcherPathType) {
    auto f = tmp / "a";
    wtr::event ev {f, wtr::event::effect_type::create, wtr::event::path_type::watcher};
    bool fired = false;
    ls->setOnChange([&] { fired = true; });
    ls->onFsEvent(ev);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(fired);
}

TEST_F(LocalStorageUnitTest, HandleMovedIgnoredTmpThenUpdate) {
    auto real = tmp / "file.txt"; std::ofstream(real) << "X";
    uint64_t fid = ls->getFileId(real);
    FileRecordDTO rec{ EntryType::File, "file.txt",
                       std::filesystem::file_size(real), 0, 0, fid };
    int gid = db->add_file(rec); ASSERT_GT(gid, 0);
    auto old_tmp = tmp / (std::string(".-tmp-SyncHarbor-") + "file.txt");
    std::ofstream(old_tmp) << "X";

    wtr::event ev {
        wtr::event{ old_tmp,
        wtr::event::effect_type::rename,
        wtr::event::path_type::file },
            wtr::event{ real,
            wtr::event::effect_type::rename,
            wtr::event::path_type::file }
    };

    ls->onFsEvent(ev);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto changes = ls->proccessChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0]->getType(), ChangeType::Update);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("file.txt"));
}

TEST_F(LocalStorageUnitTest, HandleMovedIgnoredTmpBoth) {
    auto t1 = tmp / (std::string(".-tmp-SyncHarbor-") + "A.txt");
    auto t2 = tmp / (std::string(".-tmp-SyncHarbor-") + "B.txt");
    std::ofstream(t1) << "1"; std::ofstream(t2) << "2";

    wtr::event ev {
        wtr::event{ t1,
        wtr::event::effect_type::rename,
        wtr::event::path_type::file },
            wtr::event{ t2,
            wtr::event::effect_type::rename,
            wtr::event::path_type::file }
    };

    ls->onFsEvent(ev);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto changes = ls->proccessChanges();
    EXPECT_TRUE(changes.empty());
}

TEST_F(LocalStorageUnitTest, HandleMovedExpectedMove) {
    auto a = tmp / "A.txt";
    std::ofstream(a) << "A";
    auto b = tmp / "B.txt";
    std::ofstream(b) << "B";
    FileMovedDTO dto{ EntryType::File, 1, 0,
                      "A.txt", "B.txt" };
    dto.cloud_id = cid;
    auto ndto = std::make_unique<FileMovedDTO>(dto);
    ls->proccesMove(ndto, "");

    wtr::event ev {
        wtr::event{ a,
        wtr::event::effect_type::rename,
        wtr::event::path_type::file },
            wtr::event{ b,
            wtr::event::effect_type::rename,
            wtr::event::path_type::file }
    };

    ls->onFsEvent(ev);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto changes = ls->proccessChanges();
    EXPECT_TRUE(changes.empty());
}

TEST_F(LocalStorageUnitTest, HandleMovedUnknownFileIdCreatesNew) {
    auto oldf = tmp / "old2.txt"; std::ofstream(oldf) << "O";
    auto newf = tmp / "new2.txt"; std::ofstream(newf) << "N";

    wtr::event ev {
        wtr::event{ oldf,
        wtr::event::effect_type::rename,
        wtr::event::path_type::file },
            wtr::event{ newf,
            wtr::event::effect_type::rename,
            wtr::event::path_type::file }
    };

    ls->onFsEvent(ev);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto changes = ls->proccessChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0]->getType(), ChangeType::New);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("new2.txt"));
}

TEST_F(LocalStorageUnitTest, HandleCreatedIgnoredTmp) {
    auto tmpf = tmp / ".goutputstream-test.txt"; std::ofstream(tmpf) << "T";
    wtr::event ev { tmpf,
        wtr::event::effect_type::create,
        wtr::event::path_type::file };
    ls->onFsEvent(ev);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto changes = ls->proccessChanges();
    EXPECT_TRUE(changes.empty());
}

TEST_F(LocalStorageUnitTest, HandleCreatedExpectedNew) {
    std::filesystem::create_directory(tmp / "dir");
    auto dtos = ls->createPath("dir/sub", std::filesystem::path{ "sub" });
    ASSERT_EQ(dtos.size(), 1);

    auto sub = tmp / "dir" / "sub"; std::ofstream(sub) << "X";
    wtr::event ev { sub,
        wtr::event::effect_type::create,
        wtr::event::path_type::file };
    ls->onFsEvent(ev);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto changes = ls->proccessChanges();
    EXPECT_TRUE(changes.empty());
}