#include <gtest/gtest.h>
#include <httplib.h>

#include "Networking.h"
#include "commands.h"
#include "CallbackDispatcher.h"

struct MockServer {
    httplib::Server srv;
    std::thread     thr;

    std::atomic<int> ok{ 0 };
    std::atomic<int> flaky_sync{ 0 };
    std::atomic<int> async_ok{ 0 };
    std::atomic<int> flaky_async{ 0 };
    std::atomic<int> bad_sync{ 0 };
    std::atomic<int> bad_async{ 0 };

    explicit MockServer(uint16_t port = 8081)
    {
        srv.Get(R"(/ok)", [&](const auto&, auto& res) {
            ++ok;
            res.set_content(R"({"status":"ok"})", "application/json");
            });

        srv.Get(R"(/flaky)", [&](const auto&, auto& res) {
            ++flaky_sync;
            if (flaky_sync == 1) { res.status = 503; }
            else { res.set_content(R"({"status":"ok"})", "application/json"); }
            });

        srv.Get(R"(/async)", [&](const auto&, auto& res) {
            ++async_ok;
            res.set_content(R"({"status":"done"})", "application/json");
            });

        srv.Get(R"(/flaky_async)", [&](const auto&, auto& res) {
            ++flaky_async;
            if (flaky_async == 1) { res.status = 503; }
            else { res.set_content(R"({"status":"done"})", "application/json"); }
            });

        srv.Get(R"(/bad)", [&](const auto&, auto& res) {
            ++bad_sync;  res.status = 400;
            res.set_content("bad request", "text/plain");
            });

        srv.Get(R"(/bad_async)", [&](const auto&, auto& res) {
            ++bad_async; res.status = 400;
            res.set_content("bad request", "text/plain");
            });

        thr = std::thread([&, port] { srv.listen("localhost", port); });
    }
    ~MockServer() { srv.stop(); thr.join(); }
};

class SimpleCommand : public ICommand {
    int _cloud_id;
    std::string _target;
    std::unique_ptr<RequestHandle> _h;
public:
    SimpleCommand(int cid, std::string url, std::string tgt) :
        _cloud_id(cid), _target(std::move(tgt)), _h(std::make_unique<RequestHandle>())
    {
        curl_easy_setopt(_h->_curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(_h->_curl, CURLOPT_URL, url.c_str());
        _h->setCommonCURLOpt();
    }
    void execute(const std::shared_ptr<BaseStorage>& cloud) override {}
    RequestHandle& getHandle() override { return *_h; }
    std::string getTarget() const override { return _target; }
    int getId() const override { return _cloud_id; }
    void completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud)  override {}
    void continueChain() override {}
    void addNext(std::unique_ptr<ICommand> next_command) override {}
    EntryType getTargetType() const override { return EntryType::Null; }
    bool needRepeat() const override { return false; }
};

class HttpClientIntegrationTest : public ::testing::Test {
protected:
    MockServer mock{ 8081 };

    void SetUp() override {
        CallbackDispatcher::get().start();
        HttpClient::get().start();
    }
    void TearDown() override {
        HttpClient::get().waitUntilIdle();
        CallbackDispatcher::get().waitUntilIdle();
        HttpClient::get().shutdown();
    }
};

TEST_F(HttpClientIntegrationTest, SyncSuccess)
{
    auto h = std::make_unique<RequestHandle>();
    curl_easy_setopt(h->_curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(h->_curl, CURLOPT_URL, "http://127.0.0.1:8081/ok");
    h->setCommonCURLOpt();

    HttpClient::get().syncRequest(h);
    EXPECT_EQ(mock.ok, 1);
    EXPECT_NE(h->_response.find("\"status\":\"ok\""), std::string::npos);
}

TEST_F(HttpClientIntegrationTest, SyncRetry503)
{
    auto h = std::make_unique<RequestHandle>();
    curl_easy_setopt(h->_curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(h->_curl, CURLOPT_URL, "http://127.0.0.1:8081/flaky");
    h->setCommonCURLOpt();

    HttpClient::get().syncRequest(h);
    EXPECT_EQ(mock.flaky_sync, 2);
}

TEST_F(HttpClientIntegrationTest, SyncHard4xx)
{
    auto h = std::make_unique<RequestHandle>();
    curl_easy_setopt(h->_curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(h->_curl, CURLOPT_URL, "http://127.0.0.1:8081/bad");
    h->setCommonCURLOpt();

    HttpClient::get().syncRequest(h);
    EXPECT_EQ(mock.bad_sync, 1);
}

TEST_F(HttpClientIntegrationTest, SyncCurlError)
{
    auto h = std::make_unique<RequestHandle>();
    curl_easy_setopt(h->_curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(h->_curl, CURLOPT_URL, "http://nonexistent.localhost/");
    curl_easy_setopt(h->_curl, CURLOPT_TIMEOUT_MS, 1000L);
    h->setCommonCURLOpt();

    HttpClient::get().syncRequest(h);
    SUCCEED();
}

TEST_F(HttpClientIntegrationTest, AsyncPipelineOk)
{
    for (int i = 0; i < 3; ++i) {
        HttpClient::get().submit(std::make_unique<SimpleCommand>(
            0, "http://127.0.0.1:8081/async", "ASYNC"));
    }
    HttpClient::get().waitUntilIdle();
    EXPECT_EQ(mock.async_ok, 3);
}

TEST_F(HttpClientIntegrationTest, AsyncRetry503)
{
    HttpClient::get().submit(std::make_unique<SimpleCommand>(
        0, "http://127.0.0.1:8081/flaky_async", "FLAKY-A"));
    std::this_thread::sleep_for(std::chrono::seconds(2));
    HttpClient::get().waitUntilIdle();

    EXPECT_EQ(mock.flaky_async, 2);
}

TEST_F(HttpClientIntegrationTest, AsyncHard4xx)
{
    HttpClient::get().submit(std::make_unique<SimpleCommand>(
        0, "http://127.0.0.1:8081/bad_async", "BAD-A"));
    HttpClient::get().waitUntilIdle();
    EXPECT_EQ(mock.bad_async, 1);
}

TEST_F(HttpClientIntegrationTest, CleanShutdown)
{
    HttpClient::get().shutdown();
    HttpClient::get().shutdown();
    SUCCEED();
}
