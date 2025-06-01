#include <gtest/gtest.h>
#include "http-server.h"
#include <thread>
#include <chrono>

static int find_free_port() {
    for (int p = 20000; p < 20010; ++p) {
        httplib::Client cli("localhost", p);
        auto res = cli.Get("/");
        if (!res) return p;
    }
    return 0;
}

class HttpServerUnitTest : public ::testing::Test {
protected:
    void SetUp() override {
        port = find_free_port();
        ASSERT_NE(port, 0);
    }

    int port;
};

TEST_F(HttpServerUnitTest, GetPort) {
    LocalHttpServer srv(port);
    EXPECT_EQ(srv.getPort(), port);
}

TEST_F(HttpServerUnitTest, StartListenAndReceiveCode) {
    LocalHttpServer srv(port);
    srv.startListening();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    httplib::Client cli("localhost", port);
    auto res = cli.Get("/oauth2callback?code=XYZ123");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);

    auto code = srv.waitForCode();
    EXPECT_EQ(code, "XYZ123");

    srv.stopListening();
}

TEST_F(HttpServerUnitTest, MissingCodeReturns400AndDoesNotUnblock) {
    LocalHttpServer srv(port);
    srv.startListening();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    httplib::Client cli("localhost", port);

    auto res1 = cli.Get("/oauth2callback");
    ASSERT_TRUE(res1);
    EXPECT_EQ(res1->status, 400);

    std::atomic<bool> finished{ false };
    std::thread waiter([&] {
        srv.waitForCode();
        finished = true;
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(finished);

    auto res2 = cli.Get("/oauth2callback?code=DUMMY");
    ASSERT_TRUE(res2);
    EXPECT_EQ(res2->status, 200);

    waiter.join();
    EXPECT_TRUE(finished);

    srv.stopListening();
}


TEST_F(HttpServerUnitTest, IdempotentStartStop) {
    LocalHttpServer srv(port);
    EXPECT_NO_THROW(srv.stopListening());
    srv.startListening();
    EXPECT_NO_THROW(srv.startListening());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_NO_THROW(srv.stopListening());
    EXPECT_NO_THROW(srv.stopListening());
}

TEST_F(HttpServerUnitTest, DestructorStopsServer) {
    {
        LocalHttpServer srv(port);
        srv.startListening();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    httplib::Client cli("localhost", port);
    auto res = cli.Get("/");
    EXPECT_FALSE(res);
}
