#include "LocalStorageTestFixture.h"
#include <fstream>
#include <utility>

TEST_F(LocalStorageUnitTest, GetHomeDir) {
    EXPECT_EQ(ls->getHomeDir(), tmp.string());
}

TEST_F(LocalStorageUnitTest, GetFileIdNonexistent) {
    EXPECT_EQ(ls->getFileId(tmp / "no_such.file"), 0u);
}

TEST_F(LocalStorageUnitTest, ComputeFileHashNonexistent) {
    EXPECT_EQ(ls->computeFileHash(tmp / "nope.bin"), 0u);
}

TEST_F(LocalStorageUnitTest, StartWatchingIdempotence) {
    EXPECT_NO_THROW(ls->stopWatching());

    EXPECT_NO_THROW(ls->startWatching());

    EXPECT_NO_THROW(ls->startWatching());

    EXPECT_NO_THROW(ls->stopWatching());

    EXPECT_NO_THROW(ls->stopWatching());
}

TEST_F(LocalStorageUnitTest, ComputeFileHashChanges) {
    auto f = tmp / "x.txt";
    std::ofstream(f) << "foo";
    auto h1 = ls->computeFileHash(f);
    std::ofstream(f, std::ios::app) << "bar";
    auto h2 = ls->computeFileHash(f);
    EXPECT_NE(h1, h2);
}

TEST_F(LocalStorageUnitTest, FromWatcherTime) {
    long long ns = 2'000'000'000LL;
    auto got = ls->fromWatcherTime(ns);
    using namespace std::chrono;
    auto exp = system_clock::to_time_t(
        system_clock::time_point{
            duration_cast<system_clock::duration>(nanoseconds(ns))
        }
    );
    EXPECT_EQ(got, exp);
}


class ThatFileTmpExistsTest
    : public LocalStorageUnitTest
    , public ::testing::WithParamInterface<std::pair<std::string, bool>> {
};

TEST_P(ThatFileTmpExistsTest, DetectTmpNeighbor) {
    auto [affix, isPrefix] = GetParam();
    auto f = tmp / "test.txt";
    std::ofstream(f) << "X";
    auto neighbor = tmp / (isPrefix
        ? affix + f.filename().string()
        : f.filename().string() + affix);
    std::ofstream(neighbor) << "!";
    EXPECT_TRUE(ls->thatFileTmpExists(f));
    std::filesystem::remove(neighbor);
}

INSTANTIATE_TEST_SUITE_P(
    TmpAffixes,
    ThatFileTmpExistsTest,
    ::testing::Values(
        
        std::make_pair(".-tmp-SyncHarbor-", true),
        std::make_pair(".goutputstream-", true),
        std::make_pair(".kate-swp", true),
        std::make_pair(".#", true),
        std::make_pair(".~lock.", true),
       
        std::make_pair(".swp", false),
        std::make_pair(".swo", false),
        std::make_pair(".swx", false),
        std::make_pair(".tmp", false),
        std::make_pair(".temp", false),
        std::make_pair(".bak", false),
        std::make_pair(".orig", false),
        std::make_pair("~", false)
    )
);

class IsDocTest
    : public LocalStorageUnitTest
    , public ::testing::WithParamInterface<std::pair<std::string, bool>> {
};

TEST_P(IsDocTest, ClassifyByExtension) {
    auto [name, exp] = GetParam();
    EXPECT_EQ(ls->isDoc(name), exp);
}

INSTANTIATE_TEST_SUITE_P(
    DocExtensions,
    IsDocTest,
    ::testing::Values(
        std::make_pair("a.doc", true),
        std::make_pair("b.docx", true),
        std::make_pair("c.csv", true),
        std::make_pair("d.pptx", true),
        std::make_pair("e.url", true),
        std::make_pair("f.txt", false),
        std::make_pair("g.png", false),
        std::make_pair("h.zip", false)
    )
);