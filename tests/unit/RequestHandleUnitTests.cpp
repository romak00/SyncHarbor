// tests/unit/RequestHandleUnitTests.cpp

#include <gtest/gtest.h>
#include "request-handle.h"

#include <fstream>
#include <filesystem>

TEST(RequestHandleUnitTest, DefaultConstructorInitializesCurl) {
    RequestHandle rh;
    EXPECT_NE(rh._curl, nullptr);
    EXPECT_EQ(rh._mime, nullptr);
    EXPECT_EQ(rh._headers, nullptr);
    EXPECT_EQ(rh._retry_count, 0);
}

TEST(RequestHandleUnitTest, MoveAssignmentTransfersResourcesAndNullsSource) {
    RequestHandle a;
    a.addHeaders("X-Test: 1");
    CURL* a_curl = a._curl;
    curl_slist* a_hdr = a._headers;

    RequestHandle b;
    b = std::move(a);

    EXPECT_EQ(b._curl, a_curl);
    EXPECT_EQ(b._headers, a_hdr);
    EXPECT_EQ(a._curl, nullptr);
    EXPECT_EQ(a._mime, nullptr);
    EXPECT_EQ(a._headers, nullptr);
}

TEST(RequestHandleUnitTest, MoveConstructorTransfersResourcesAndNullsSource) {
    RequestHandle a;
    a.addHeaders("Y-Test: 2");
    CURL* a_curl = a._curl;
    curl_slist* a_hdr = a._headers;

    RequestHandle b(std::move(a));

    EXPECT_EQ(b._curl, a_curl);
    EXPECT_EQ(b._headers, a_hdr);
    EXPECT_EQ(a._curl, nullptr);
    EXPECT_EQ(a._mime, nullptr);
    EXPECT_EQ(a._headers, nullptr);
}

TEST(RequestHandleUnitTest, SetFileStreamInAndScheduleRetryResetsStream) {
    auto path = std::filesystem::path("tmp_in.bin");
    {
        std::ofstream out(path, std::ios::binary);
        out << "hello";
    }

    RequestHandle rh;
    ASSERT_NO_THROW(rh.setFileStream(path, std::ios::in));
    EXPECT_TRUE(rh._iofd.is_open());

    EXPECT_NO_THROW(rh.scheduleRetry());

    std::filesystem::remove(path);
}

TEST(RequestHandleUnitTest, SetFileStreamOutAndScheduleRetryResetsStream) {
    auto path = std::filesystem::path("tmp_out.bin");
    {
        std::ofstream out(path, std::ios::binary);
        out << "data";
    }

    RequestHandle rh;
    ASSERT_NO_THROW(rh.setFileStream(path, std::ios::out));
    EXPECT_TRUE(rh._iofd.is_open());
    EXPECT_NO_THROW(rh.scheduleRetry());

    std::filesystem::remove(path);
}

TEST(RequestHandleUnitTest, WriteCallbackAppendsCorrectly) {
    std::string out = "Hello";
    const char add[] = " World";
    size_t ret = RequestHandle::writeCallback(
        (void*)add, 1, strlen(add), &out
    );
    EXPECT_EQ(ret, strlen(add));
    EXPECT_EQ(out, "Hello World");
}

TEST(RequestHandleUnitTest, ReadDataReadsWholeFile) {
    auto path = std::filesystem::path("tmp_read.txt");
    std::ofstream(path, std::ios::binary) << "12345";
    std::ifstream in(path, std::ios::binary);
    char buf[10];
    size_t got = RequestHandle::readData(buf, 1, 5, &in);
    EXPECT_EQ(got, 5u);
    EXPECT_EQ(std::string(buf, buf + got), "12345");
    std::filesystem::remove(path);
}

TEST(RequestHandleUnitTest, WriteDataWritesToFile) {
    auto path = std::filesystem::path("tmp_write.txt");
    std::ofstream out(path, std::ios::binary);
    const char data[] = "abcde";
    size_t wr = RequestHandle::writeData((void*)data, 1, 5, &out);
    out.close();
    EXPECT_EQ(wr, 5u);
    std::ifstream in(path, std::ios::binary);
    std::string read((std::istreambuf_iterator<char>(in)), {});
    EXPECT_EQ(read, "abcde");
    std::filesystem::remove(path);
}

TEST(RequestHandleUnitTest, ScheduleRetryThrowsAfterMaxRetries) {
    RequestHandle rh;
    for (int i = 0; i < 6; ++i) {
        EXPECT_NO_THROW(rh.scheduleRetry());
    }
    EXPECT_THROW(rh.scheduleRetry(), std::runtime_error);
}

TEST(RequestHandleUnitTest, SetFileStreamThrowsOnMissingFile) {
    RequestHandle rh;
    EXPECT_THROW(rh.setFileStream("does_not_exist.bin", std::ios::in),
        std::runtime_error);
}

TEST(RequestHandleUnitTest, SetFileStreamSucceedsOnExistingFile) {
    auto path = std::filesystem::path("tmp_exist.txt");
    std::ofstream(path) << "ok";
    RequestHandle rh;
    EXPECT_NO_THROW(rh.setFileStream(path, std::ios::in));
    std::filesystem::remove(path);
}

TEST(RequestHandleUnitTest, AddAndClearHeadersNoCrash) {
    RequestHandle rh;
    rh.addHeaders("X-Test: 1");
    rh.addHeaders("Y-Test: 2");
    EXPECT_NO_THROW(rh.setCommonCURLOpt());
    EXPECT_NO_THROW(rh.clearHeaders());
    EXPECT_EQ(rh._headers, nullptr);
}

TEST(RequestHandleUnitTest, SetCommonCURLOptDoesNotCrash) {
    RequestHandle rh;
    EXPECT_NO_THROW(rh.setCommonCURLOpt());
}

TEST(RequestHandleUnitTest, SetGlobalResolve) {
    RequestHandle rh;
    EXPECT_NO_THROW(rh.addGlobalResolve("www.googleapis.com", 443, "127.0.0.1", 9443));
    RequestHandle rh2;
    EXPECT_FALSE(rh2._global_resolve == nullptr);
}
