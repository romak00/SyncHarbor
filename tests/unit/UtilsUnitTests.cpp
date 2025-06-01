#include <gtest/gtest.h>
#include "utils.h"

#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>

static bool near_time(std::time_t a, std::time_t b, int tol = 2) {
    return std::abs(static_cast<long>(a - b)) <= tol;
}

TEST(UtilsUnitTest, NormalizePath) {
    namespace fs = std::filesystem;
    EXPECT_EQ(normalizePath(fs::path{ "/" }), fs::path{});
    EXPECT_EQ(normalizePath(fs::path{ "/foo/bar" }), fs::path{ "foo/bar" });
    EXPECT_EQ(normalizePath(fs::path{ "baz" }), fs::path{ "baz" });
    EXPECT_EQ(normalizePath(fs::path{ "" }), fs::path{ "" });
}

TEST(UtilsUnitTest, CloudProviderToCstrAndToString) {
    using CPT = CloudProviderType;
    EXPECT_STREQ(to_cstr(CPT::LocalStorage), "LocalStorage");
    EXPECT_EQ(to_string(CPT::LocalStorage), "LocalStorage");
    EXPECT_STREQ(to_cstr(CPT::GoogleDrive), "GoogleDrive");
    EXPECT_STREQ(to_cstr(CPT::Dropbox), "Dropbox");
    EXPECT_STREQ(to_cstr(CPT::OneDrive), "OneDrive");
    EXPECT_STREQ(to_cstr(CPT::Yandex), "Yandex");
    EXPECT_STREQ(to_cstr(CPT::MailRu), "MailRu");
    EXPECT_STREQ(to_cstr(CPT::FakeTest), "FakeTest");
    EXPECT_STREQ(to_cstr(static_cast<CPT>(-1)), "Unknown");
}

TEST(UtilsUnitTest, CloudTypeFromString) {
    EXPECT_EQ(cloud_type_from_string("LocalStorage"), CloudProviderType::LocalStorage);
    EXPECT_EQ(cloud_type_from_string("GoogleDrive"), CloudProviderType::GoogleDrive);
    EXPECT_EQ(cloud_type_from_string("Dropbox"), CloudProviderType::Dropbox);
    EXPECT_EQ(cloud_type_from_string("OneDrive"), CloudProviderType::OneDrive);
    EXPECT_EQ(cloud_type_from_string("Yandex"), CloudProviderType::Yandex);
    EXPECT_EQ(cloud_type_from_string("MailRu"), CloudProviderType::MailRu);
    EXPECT_EQ(cloud_type_from_string("FakeTest"), CloudProviderType::FakeTest);
    EXPECT_THROW(cloud_type_from_string("Nonexistent"), std::out_of_range);
}

TEST(UtilsUnitTest, ConvertCloudTime) {
    EXPECT_EQ(convertCloudTime("1970-01-01T00:00:00Z"), 0);
    EXPECT_EQ(convertCloudTime("1970-01-01T00:00:01.123Z"), 1);
    EXPECT_EQ(convertCloudTime("1970-01-01T00:00:02"), 2);
}

TEST(UtilsUnitTest, ConvertSystemTime) {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "utime_test.txt";
    {
        std::ofstream out(tmp);
        out << "abc";
    }
    auto t1 = std::time(nullptr);
    auto t2 = convertSystemTime(tmp);
    EXPECT_TRUE(near_time(t1, t2));
    fs::remove(tmp);
}

TEST(UtilsUnitTest, EntryTypeToCstrAndToString) {
    EXPECT_STREQ(to_cstr(EntryType::File), "File");
    EXPECT_EQ(to_string(EntryType::File), "File");
    EXPECT_STREQ(to_cstr(EntryType::Directory), "Directory");
    EXPECT_STREQ(to_cstr(EntryType::Document), "Document");
    EXPECT_STREQ(to_cstr(EntryType::Null), "Null");
}

TEST(UtilsUnitTest, EntryTypeFromString) {
    EXPECT_EQ(entry_type_from_string("File"), EntryType::File);
    EXPECT_EQ(entry_type_from_string("Directory"), EntryType::Directory);
    EXPECT_EQ(entry_type_from_string("Document"), EntryType::Document);
    EXPECT_EQ(entry_type_from_string("Null"), EntryType::Null);
    EXPECT_EQ(entry_type_from_string("Other"), EntryType::Null);
}

TEST(UtilsUnitTest, ChangeTypeToString) {
    EXPECT_EQ(to_string(ChangeType::New), "New");
    EXPECT_EQ(to_string(ChangeType::Rename), "Rename");
    EXPECT_EQ(to_string(ChangeType::Update), "Update");
    EXPECT_EQ(to_string(ChangeType::Move), "Move");
    EXPECT_EQ(to_string(ChangeType::Delete), "Delete");
    EXPECT_EQ(to_string(static_cast<ChangeType>(0)), "Null");
}
