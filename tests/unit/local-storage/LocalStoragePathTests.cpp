// tests/unit/LocalStoragePathTests.cpp
#include "LocalStorageTestFixture.h"

TEST_F(LocalStorageUnitTest, CreatePathMakesAllSegments) {
    std::filesystem::create_directory(tmp / "x");
    auto dtos = ls->createPath("x/y/z", std::filesystem::path{ "y" } / "z");
    ASSERT_EQ(dtos.size(), 2u);
    EXPECT_TRUE(std::filesystem::is_directory(tmp / "x" / "y"));
    EXPECT_TRUE(std::filesystem::is_directory(tmp / "x" / "y" / "z"));
    EXPECT_EQ(dtos[0]->rel_path, std::filesystem::path("x/y"));
    EXPECT_EQ(dtos[1]->rel_path, std::filesystem::path("x/y/z"));
}

TEST_F(LocalStorageUnitTest, InitialFilesEmpty) {
    EXPECT_TRUE(ls->initialFiles().empty());
}

TEST_F(LocalStorageUnitTest, InitialFilesNested) {
    std::filesystem::create_directories(tmp / "A" / "B");
    std::ofstream(tmp / "A" / "f1.txt") << "1";
    std::ofstream(tmp / "A" / "B" / "f2.bin") << "2";
    auto v = ls->initialFiles();
    EXPECT_EQ(v.size(), 4u);
    std::set<std::string> S;
    for (auto& d : v) S.insert(d->rel_path.string());
    EXPECT_TRUE(S.count("A"));
    EXPECT_TRUE(S.count("A/f1.txt"));
    EXPECT_TRUE(S.count("A/B"));
    EXPECT_TRUE(S.count("A/B/f2.bin"));
}

TEST_F(LocalStorageUnitTest, InitialFilesTypeClassification) {
    std::ofstream(tmp / "d.docx") << "";
    std::ofstream(tmp / "p.png") << "";
    std::filesystem::create_directory(tmp / "dir1");

    auto v = ls->initialFiles();
    std::map<std::string, EntryType> M;
    for (auto& dto : v) M[dto->rel_path.string()] = dto->type;

    EXPECT_EQ(M["d.docx"], EntryType::Document);
    EXPECT_EQ(M["p.png"], EntryType::File);
    EXPECT_EQ(M["dir1"], EntryType::Directory);
}
