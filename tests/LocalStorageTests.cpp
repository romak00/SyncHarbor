#include <gtest/gtest.h>
#include "LocalStorage.h"
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

class LocalStorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto tmp_dir = std::filesystem::temp_directory_path() / "-tmp-clousync-test-local_storage-";
  
        std::filesystem::create_directory(tmp_dir);
        db = std::make_shared<Database>(":memory:");
        storage = std::make_unique<LocalStorage>(tmp_dir, /*cloudId=*/0, db);
        storage->startWatching();
    }

    void TearDown() override {
        storage->stopWatching();
        std::filesystem::remove_all(tmp_dir);
    }

    std::filesystem::path tmp_dir;
    std::shared_ptr<Database> db;
    std::unique_ptr<LocalStorage> storage;
    std::shared_ptr<RawSignal> rawSignal = std::make_shared<RawSignal>();
};

TEST_F(LocalStorageTest, DetectCreateFile) {
    auto file = tmp_dir / "a.txt";
    std::ofstream(file) << "hello";
    safeSleep();

    auto changes = storage->proccessChanges();
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::New);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("a.txt"));
}

TEST_F(LocalStorageTest, DetectModifyFile) {
    auto file = tmp_dir / "b.txt";
    {
        std::ofstream(file) << "v1";
    }

    uint64_t hash = storage->computeFileHash(file);
    uint64_t file_id = storage->getFileId(file);
    FileRecordDTO dto{
        EntryType::File,
        "b.txt",
        std::filesystem::file_size(file),
        now(),
        hash,
        file_id
    };
    int global_id = db->add_file(dto);

    safeSleep();
    std::ofstream(file, std::ios::app) << "v2";
    safeSleep();

    storage->getChanges();
    auto changes = storage->proccessChanges();
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::Update);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("b.txt"));
}

TEST_F(LocalStorageTest, DetectMoveFile) {
    auto oldp = tmp_dir / "c.txt";
    std::ofstream(oldp) << "x";

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

    safeSleep();

    auto newp = tmp_dir / "d.txt";
    std::filesystem::rename(oldp, newp);
    safeSleep();

    storage->getChanges();
    auto changes = storage->proccessChanges();
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::Move);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("d.txt"));
}

TEST_F(LocalStorageTest, EditorAtomicSave) {
    auto orig = tmp_dir / "e.txt";
    std::ofstream(orig) << "old";

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
    
    safeSleep();

    auto tmpf = tmp_dir / ".-tmp-cloudsync-e.txt";
    std::ofstream(tmpf) << "new";
    safeSleep();

    std::filesystem::remove(orig);
    safeSleep();

    std::filesystem::rename(tmpf, orig);
    safeSleep();

    storage->getChanges();
    auto changes = storage->proccessChanges();
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::Update);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("e.txt"));
}

TEST_F(LocalStorageTest, DetectDeleteFile) {
    auto file = tmp_dir / "f.txt";
    std::ofstream(file) << "foo";

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
    
    safeSleep();

    std::filesystem::remove(file);
    safeSleep();

    storage->getChanges();
    auto changes = storage->proccessChanges();
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::Delete);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("f.txt"));
}

TEST_F(LocalStorageTest, DetectCreateDirectory) {
    auto dir = tmp_dir / "subdir";
    std::filesystem::create_directory(dir);
    safeSleep();

    storage->getChanges();
    auto changes = storage->proccessChanges();
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::New);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("subdir"));
    EXPECT_EQ(changes[0]->getTargetType(), EntryType::Directory);
}

TEST_F(LocalStorageTest, DetectNestedCreate) {
    auto dir = tmp_dir / "x" / "y";
    std::filesystem::create_directories(dir);
    auto file = dir / "inside.txt";
    std::ofstream(file) << "hi";
    safeSleep();

    storage->getChanges();
    auto changes = storage->proccessChanges();
    ASSERT_EQ(changes.size(), 2);

    EXPECT_EQ(changes[0]->getType(), ChangeType::New);
    EXPECT_EQ(changes[0]->getTargetPath(), std::filesystem::path("x/y"));
    EXPECT_EQ(changes[0]->getTargetType(), EntryType::Directory);

    EXPECT_EQ(changes[1]->getType(), ChangeType::New);
    EXPECT_EQ(changes[1]->getTargetPath(), std::filesystem::path("x/y/inside.txt"));
    EXPECT_EQ(changes[1]->getTargetType(), EntryType::File);
}

TEST_F(LocalStorageTest, GetFileIdOnCreate) {
    auto file = tmp_dir / "g.txt";
    std::ofstream(file) << "hello";
    safeSleep();

    storage->getChanges();
    auto changes = storage->proccessChanges();
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]->getType(), ChangeType::New);

    uint64_t fid1 = storage->getFileId(std::filesystem::path("g.txt"));
    ASSERT_GT(fid1, 0ULL) << "file_id должен быть положительным";

    uint64_t fid2 = storage->getFileId(std::filesystem::path("g.txt"));
    EXPECT_EQ(fid1, fid2);
}

