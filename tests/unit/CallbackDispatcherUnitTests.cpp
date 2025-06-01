#include <gtest/gtest.h>
#include "CallbackDispatcher.h"
#include "DatabaseTestFixture.h"
#include "BaseStorage.h"
#include "commands.h"

struct FakeStorage : BaseStorage {
    void proccesUpload(std::unique_ptr<FileRecordDTO>&, const std::string&) const override {}
    void proccesUpdate(std::unique_ptr<FileUpdatedDTO>&, const std::string&) const override {}
    void proccesMove(std::unique_ptr<FileMovedDTO>&, const std::string&) const override {}
    void proccesDelete(std::unique_ptr<FileDeletedDTO>&, const std::string&) const override {}
    std::vector<std::unique_ptr<FileRecordDTO>> initialFiles() override { return {}; }
    CloudProviderType getType() const override { return CloudProviderType::FakeTest; }
    void setupUploadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const override {}
    void setupUpdateHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileUpdatedDTO>& dto) const override {}
    void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const override {}
    void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileUpdatedDTO>& dto) const override {}
    void setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) const override {}
    void setupMoveHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileMovedDTO>& dto) const override {}
    void proccesDownload(std::unique_ptr<FileUpdatedDTO>& dto, const std::string& response) const override {}
  
    std::vector<std::unique_ptr<FileRecordDTO>> createPath(const std::filesystem::path& path, const std::filesystem::path& missing) override { return {}; };

    std::string buildAuthURL(int local_port) const override { return ""; }
    std::string getRefreshToken(const std::string& code, const int local_port) override {}
    void refreshAccessToken() override {}
    void proccessAuth(const std::string& responce) override {}
    std::string getDeltaToken() override { return ""; }
    std::string getHomeDir() const override { return ""; }
    void getChanges() override{}
    int id() const override { return 98; }
    bool hasChanges() const override { return false; }
    void setOnChange(std::function<void()> cb) override {}
    std::vector<std::shared_ptr<Change>> proccessChanges() override { return {}; }
    void ensureRootExists() override {}
};

class FakeCommand : public ICommand {
public:
    FakeCommand(int id, bool will_repeat = false)
        : _id(id), _will_repeat(will_repeat) {    
}

    int getId() const override { return _id; }
    std::string getTarget() const override { return "T"; }

    void execute(const std::shared_ptr<BaseStorage>&) override {
        _executed = true;
    }

    void completionCallback(
        const std::unique_ptr<Database>& db,
        const std::shared_ptr<BaseStorage>& storage
    ) override {
        _called = true;
    }

    bool needRepeat() const override {
        if (_will_repeat && !_repeated_once) {
            _repeated_once = true;
            return true;
        }
        return false;
    }

    RequestHandle& getHandle() override {
        static RequestHandle dummy;
        return dummy;
    }

    bool executed() const { return _executed; }
    bool called()   const { return _called; }
    void continueChain() override {}
    void addNext(std::unique_ptr<ICommand> next_command) override {}

private:
    int _id;
    bool _will_repeat = false;
    mutable bool _repeated_once = false;
    std::atomic<bool> _executed{ false };
    std::atomic<bool> _called{ false };
};

struct CallbackDispatcherUnitTest : public ::testing::Test {
    void TearDown() override {
        auto& disp = CallbackDispatcher::get();
        disp.finish();
        disp.setDB(std::make_shared<Database>(std::string{":memory:"}));
        disp.setClouds({});
    }

    void SetUp() override {
    }
};

TEST_F(CallbackDispatcherUnitTest, SubmitAndCallbackRuns) {
    auto& disp = CallbackDispatcher::get();
    disp.setDB(std::make_shared<Database>(std::string{":memory:"}));
    disp.setClouds({ {42, std::make_shared<FakeStorage>()} });

    disp.start();
    ASSERT_TRUE(disp.isIdle());

    auto cmd = std::make_unique<FakeCommand>(42);
    FakeCommand* raw = cmd.get();
    disp.submit(std::move(cmd));

    disp.waitUntilIdle();
    ASSERT_TRUE(raw->called());
    EXPECT_FALSE(raw->needRepeat());
}

TEST_F(CallbackDispatcherUnitTest, RepeatRequeues) {
    auto& disp = CallbackDispatcher::get();
    disp.setDB(std::make_shared<Database>(std::string{":memory:"}));
    disp.setClouds({ {1, std::make_shared<FakeStorage>()} });
    disp.start();

    auto cmd = std::make_unique<FakeCommand>(1, true);
    FakeCommand* raw = cmd.get();
    disp.submit(std::move(cmd));

    disp.waitUntilIdle();
    EXPECT_TRUE(raw->called());
    disp.waitUntilIdle();
    EXPECT_TRUE(raw->called());
}

TEST_F(CallbackDispatcherUnitTest, SyncDbWrite_RecordAndLink) {
    auto& disp = CallbackDispatcher::get();
    auto db_ptr = std::make_shared<Database>(std::string{":memory:"});
    disp.setDB(db_ptr);

    auto dto = std::make_unique<FileRecordDTO>(
        EntryType::File, std::filesystem::path("f.txt"),
        10, 0, 123, 7
    );
    disp.syncDbWrite(dto);
    ASSERT_GT(dto->global_id, 0);
    EXPECT_TRUE(db_ptr->quickPathCheck("f.txt"));

    int cid = db_ptr->add_cloud("name", CloudProviderType::FakeTest, nlohmann::json::object());
    auto dto2 = std::make_unique<FileRecordDTO>(
        dto->global_id,
        cid,
        "P",
        "CF",
        11,
        "hh", 1
    );
    disp.setClouds({ {cid, std::make_shared<FakeStorage>()} });
    disp.syncDbWrite(dto2);
    EXPECT_EQ(db_ptr->getCloudFileIdByPath("f.txt", cid), "CF");
}

TEST_F(CallbackDispatcherUnitTest, SyncDbWrite_Updated) {
    auto& disp = CallbackDispatcher::get();
    auto db_ptr = std::make_shared<Database>(std::string{":memory:"});
    disp.setDB(db_ptr);

    FileRecordDTO base{ EntryType::File, "x", 0, 0, 0, 1 };
    int gid = db_ptr->add_file(base);
    auto up = std::make_unique<FileUpdatedDTO>(
        EntryType::File, gid,
        999u, 2,
        std::filesystem::path("x"),
        42, 1
    );
    disp.syncDbWrite(up);
    auto rec = db_ptr->getFileByGlobalId(gid);
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->size, 42u);

    int cid = db_ptr->add_cloud("name", CloudProviderType::FakeTest, nlohmann::json::object());

    auto dto2 = std::make_unique<FileRecordDTO>(
        gid,
        cid,
        "P",
        "CF",
        11,
        "hh", 1
    );

    db_ptr->add_file_link(*dto2);

    auto up2 = std::make_unique<FileUpdatedDTO>(
        EntryType::File, gid,
        cid, "CF",
        "hhh", 3,
        std::filesystem::path("x"),
        "P", 99
    );

    disp.setClouds({ {cid, std::make_shared<FakeStorage>()} });
    disp.syncDbWrite(up2);
    EXPECT_EQ(db_ptr->getCloudFileIdByPath("x", cid), "CF");
}

TEST_F(CallbackDispatcherUnitTest, SyncDbWrite_Vector_RecordAndLink) {
    auto& disp = CallbackDispatcher::get();
    auto db_ptr = std::make_shared<Database>(std::string{ ":memory:" });
    disp.setDB(db_ptr);

    auto dto_new1 = std::make_unique<FileRecordDTO>(
        EntryType::File,
        std::filesystem::path("batch_file1.txt"),
        100,
        0,
        321,
        6
    );

    auto dto_new2 = std::make_unique<FileRecordDTO>(
        EntryType::File,
        std::filesystem::path("batch_file2.txt"),
        453,
        0,
        123,
        15
    );

    std::vector<std::unique_ptr<FileRecordDTO>> batch;
    batch.push_back(std::move(dto_new1));
    batch.push_back(std::move(dto_new2));

    disp.syncDbWrite(batch);

    EXPECT_TRUE(db_ptr->quickPathCheck("batch_file1.txt"));
    EXPECT_TRUE(db_ptr->quickPathCheck("batch_file2.txt"));

    int gid1 = 0, gid2 = 0;

    gid1 = db_ptr->getGlobalIdByPath("batch_file1.txt");
    gid2 = db_ptr->getGlobalIdByPath("batch_file2.txt");

    ASSERT_GT(gid1, 0);
    ASSERT_GT(gid2, 0);

    int cid = db_ptr->add_cloud("fake", CloudProviderType::FakeTest, nlohmann::json::object());

    auto dto_link1 = std::make_unique<FileRecordDTO>(
        gid1,
        cid,
        "Par1",
        "CLOUD_ID_abc",
        100,
        "haaash",
        0
    );

    auto dto_link2 = std::make_unique<FileRecordDTO>(
        gid2,
        cid,
        "Par3",
        "CLOUD_ID_xyz",
        453,
        "hshhhh",
        0
    );

    batch.clear();
    batch.push_back(std::move(dto_link1));
    batch.push_back(std::move(dto_link2));

    disp.setClouds({ { cid, std::make_shared<FakeStorage>() } });

    disp.syncDbWrite(batch);

    EXPECT_EQ(db_ptr->getCloudFileIdByPath("batch_file1.txt", cid), "CLOUD_ID_abc");
    EXPECT_EQ(db_ptr->getCloudFileIdByPath("batch_file2.txt", cid), "CLOUD_ID_xyz");
}

TEST_F(CallbackDispatcherUnitTest, SetDB_StringOverload) {
    auto& disp = CallbackDispatcher::get();
    disp.setDB(std::string{ ":memory:" });

    auto dto = std::make_unique<FileRecordDTO>(
        EntryType::File,
        std::filesystem::path("only_string_db.txt"),
        42,
        0,
        999,
        0
    );

    disp.syncDbWrite(dto);
    ASSERT_GT(dto->global_id, 0);
}