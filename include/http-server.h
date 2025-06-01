#pragma once

#include "httplib.h"

#include <atomic>
#include <thread>
#include <string>
#include <mutex>
#include <condition_variable>


class LocalHttpServer {
public:
    LocalHttpServer(int port);
    ~LocalHttpServer();

    void startListening();
    void stopListening();

    int getPort() const;

    std::string waitForCode();

private:

    httplib::Server _svr;

    std::string _code;
    std::condition_variable _cv;
    
    std::unique_ptr<std::thread> _listener_thread;
    std::mutex _mtx;
    std::atomic<bool> _running{ false };
    int _port;
};