#pragma once

#include "BaseStorage.h"
#include "Networking.h"
#include "google.h"
#include "dropbox.h"
#include "httplib.h"

#include <map>
#include <atomic>
#include <thread>
#include <string>


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