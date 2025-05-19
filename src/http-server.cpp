#include "http-server.h"
#include "logger.h"

LocalHttpServer::LocalHttpServer(int port)
    : _port(port), _svr(), _running(false)
{
    std::string path = "/oauth2callback";
    _svr.Get(path.c_str(),
        [this](const httplib::Request& req, httplib::Response& res) {
            auto code = req.get_param_value("code");
            if (code.empty()) {
                res.status = 400;
                res.set_content("Missing code", "text/plain");
                return;
            }
            {
                std::lock_guard lock(_mtx);
                _code = code;
            }
            _cv.notify_all();

            res.status = 200;
            res.set_content(
                "Authorization code for cloud received. You can close this window.",
                "text/plain");
        }
    );
}

std::string LocalHttpServer::waitForCode() {
    std::unique_lock lock(_mtx);
    _cv.wait(lock, [&] {
        return !_code.empty();
        });
    std::string code = _code;
    _code.clear();
    return code;
}

LocalHttpServer::~LocalHttpServer() {
    stopListening();
}

void LocalHttpServer::startListening() {
    if (_running.exchange(true, std::memory_order::release)) {
        LOG_WARNING("WebhookServer", "startListening called but server already running");
        return;
    }
    LOG_INFO("WebhookServer", "Starting listening on http://localhost:%d", _port);
    _listener_thread = std::make_unique<std::thread>([this]() {
        _svr.listen("localhost", _port);
    });
}

void LocalHttpServer::stopListening() {
    if (!_running.exchange(false)) {
        LOG_WARNING("WebhookServer", "stopListening called but server not running");
        return;
    }
    LOG_INFO("WebhookServer", "Stopping server on port %d", _port);
    _svr.stop();
    if (_listener_thread && _listener_thread->joinable()) {
        _listener_thread->join();
    }
    _running = false;
}

int LocalHttpServer::getPort() const {
    return _port;
}