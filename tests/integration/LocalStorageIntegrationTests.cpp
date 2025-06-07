#include <gtest/gtest.h>
#include "LocalStorage.h"
#include "logger.h"
#include "database.h"
#include <filesystem>
#include <random>
#include <thread>
#include <chrono>

using namespace std::chrono;

static std::time_t now() {
    return system_clock::to_time_t(system_clock::now());
}

static void safeSleep() {
    std::this_thread::sleep_for(milliseconds(1200));
}

class LocalStorageIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto a_tmp_dir = std::filesystem::temp_directory_path() / "-test-local_storage-";

        std::error_code ec;
        std::filesystem::remove_all(a_tmp_dir, ec);
        std::filesystem::create_directory(a_tmp_dir);

        tmp_dir = std::filesystem::canonical(a_tmp_dir);

        db = std::make_shared<Database>(std::string{ ":memory:" });

        CallbackDispatcher::get().finish();
        CallbackDispatcher::get().setDB(db);
        CallbackDispatcher::get().setClouds({});

        storage = std::make_unique<LocalStorage>(tmp_dir, /*cloudId=*/0, db);
        storage->setOnChange([] {});
        storage->startWatching();

        Logger::get().setGlobalLogLevel(LogLevel::DBG);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    void TearDown() override {
        storage->stopWatching();

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        std::error_code ec;
        std::filesystem::remove_all(tmp_dir, ec);
    }

    std::vector<std::shared_ptr<Change>> waitChanges(size_t need = 1,
        std::chrono::seconds to = 10s)
    {
        std::vector<std::shared_ptr<Change>> ch{};
        auto deadline = std::chrono::steady_clock::now() + to;
        while (std::chrono::steady_clock::now() < deadline)
        {
            auto tmp = storage->proccessChanges();
            ch.reserve(ch.size() + tmp.size());
            ch.insert(ch.end(),
                std::make_move_iterator(tmp.begin()),
                std::make_move_iterator(tmp.end())
            );
            if (ch.size() >= need)
                return ch;
            std::this_thread::sleep_for(200ms);
        }
        return ch;
    }

    std::filesystem::path tmp_dir;
    std::shared_ptr<Database> db;
    std::unique_ptr<LocalStorage> storage;
    };

TEST_F(LocalStorageIntegrationTest, DetectCreateFile) {
    Logger::get().addLogFile("DetectCreateFile", "SyncHarbor.DetectCreateFile.log");
    auto file1 = tmp_dir / "a.txt";
    std::ofstream(file1) << "hello";
    auto changes = waitChanges();

    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::New);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("a.txt"));
}

TEST_F(LocalStorageIntegrationTest, DetectModifyFile) {
    Logger::get().addLogFile("DetectModifyFile", "SyncHarbor.DetectModifyFile.log");
    auto file = tmp_dir / "b.txt";
    {
        std::ofstream(file) << "v1";
    }
    auto changes = waitChanges();

    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::New);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("b.txt"));

    uint64_t hash = storage->computeFileHash(file);
    uint64_t file_id = storage->getFileId(file);
    auto dto = std::make_unique<FileRecordDTO>(
        EntryType::File,
        "b.txt",
        std::filesystem::file_size(file),
        now(),
        hash,
        file_id
    );
    int global_id = db->add_file(*dto);

    std::ofstream(file, std::ios::app) << "v2";

    changes = waitChanges();

    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::Update);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("b.txt"));
}

TEST_F(LocalStorageIntegrationTest, DetectMoveFile) {
    Logger::get().addLogFile("DetectMoveFile", "SyncHarbor.DetectMoveFile.log");
    auto oldp = tmp_dir / "c.txt";
    std::ofstream(oldp) << "x";

    auto changes = waitChanges();

    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::New);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("c.txt"));

    uint64_t hash = storage->computeFileHash(oldp);
    uint64_t file_id = storage->getFileId(oldp);
    FileRecordDTO dto{
        EntryType::File,
        "c.txt",
        std::filesystem::file_size(oldp),
        now(),
        hash,
        file_id
    };
    int global_id = db->add_file(dto);

    auto newp = tmp_dir / "d.txt";
    std::filesystem::rename(oldp, newp);

    changes = waitChanges();

    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::Move);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("c.txt"));
}

TEST_F(LocalStorageIntegrationTest, EditorAtomicSave) {
    Logger::get().addLogFile("EditorAtomicSave", "SyncHarbor.EditorAtomicSave.log");
    auto orig = tmp_dir / "e.txt";
    std::ofstream(orig) << "old";

    auto changes = waitChanges();
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::New);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("e.txt"));

    uint64_t hash = storage->computeFileHash(orig);
    uint64_t file_id = storage->getFileId(orig);
    FileRecordDTO dto{
        EntryType::File,
        "e.txt",
        std::filesystem::file_size(orig),
        now(),
        hash,
        file_id
    };
    int global_id = db->add_file(dto);

    auto tmpf = tmp_dir / ".-tmp-SyncHarbor-e.txt";
    std::ofstream(tmpf) << "new";

    std::filesystem::remove(orig);

    std::filesystem::rename(tmpf, orig);

    changes = waitChanges();

    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::Update);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("e.txt"));
}

TEST_F(LocalStorageIntegrationTest, DetectDeleteFile) {
    Logger::get().addLogFile("DetectDeleteFile", "SyncHarbor.DetectDeleteFile.log");
    auto file = tmp_dir / "f.txt";
    std::ofstream(file) << "foo";

    auto changes = waitChanges();
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::New);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("f.txt"));

    uint64_t hash = storage->computeFileHash(file);
    uint64_t file_id = storage->getFileId(file);
    FileRecordDTO dto{
        EntryType::File,
        "f.txt",
        std::filesystem::file_size(file),
        now(),
        hash,
        file_id
    };
    int global_id = db->add_file(dto);

    std::filesystem::remove(file);

    changes = waitChanges();
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::Delete);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("f.txt"));
}

TEST_F(LocalStorageIntegrationTest, DetectCreateDirectory) {
    Logger::get().addLogFile("DetectCreateDirectory", "SyncHarbor.DetectCreateDirectory.log");
    auto dir = tmp_dir / "subdir";
    std::filesystem::create_directory(dir);

    auto changes = waitChanges();
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::New);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("subdir"));
    EXPECT_EQ(changes[0]->getTargetType(), EntryType::Directory);
}

TEST_F(LocalStorageIntegrationTest, DetectNestedCreate) {
    Logger::get().addLogFile("DetectNestedCreate", "SyncHarbor.DetectNestedCreate.log");
    auto dir1 = tmp_dir / "x";
    std::this_thread::sleep_for(100ms);

    std::filesystem::create_directories(dir1);

    std::this_thread::sleep_for(100ms);

    auto file = dir1 / "inside.txt";
    std::ofstream(file) << "hi";

#ifdef _WIN32
    auto changes = waitChanges();
    ASSERT_EQ(changes.size(), 1);

    EXPECT_EQ(changes[0]->getType(), ChangeType::New);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("x/inside.txt"));
    EXPECT_EQ(changes[0]->getTargetType(), EntryType::File);
#else
    auto changes = waitChanges(2);
    ASSERT_EQ(changes.size(), 2);

    EXPECT_EQ(changes[0]->getType(), ChangeType::New);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("x"));
    EXPECT_EQ(changes[0]->getTargetType(), EntryType::Directory);

    EXPECT_EQ(changes[1]->getType(), ChangeType::New);
    EXPECT_EQ(changes[1]->getTargetPath(), std::filesystem::path("x/inside.txt"));
    EXPECT_EQ(changes[1]->getTargetType(), EntryType::File);
#endif
}

TEST_F(LocalStorageIntegrationTest, GetFileIdOnCreate) {
    Logger::get().addLogFile("GetFileIdOnCreate", "SyncHarbor.GetFileIdOnCreate.log");
    auto file = tmp_dir / "g.txt";
    std::ofstream(file) << "hello";

    auto changes = waitChanges();
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::New);

    uint64_t fid1 = storage->getFileId(tmp_dir / std::filesystem::path("g.txt"));
    ASSERT_GT(fid1, 0ULL) << "file_id should bee > 0";

    uint64_t fid2 = storage->getFileId(tmp_dir / std::filesystem::path("g.txt"));
    EXPECT_EQ(fid1, fid2);
}

