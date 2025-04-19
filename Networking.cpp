#include "Networking.h"
#include "command.h"

HttpClient::HttpClient()
{
    _MAX_CONCURRENT = _MAX_CONCURRENT_SMALL;
    curl_global_init(CURL_GLOBAL_DEFAULT);

    _large_multi_handle = curl_multi_init();

    _large_worker = std::make_unique<std::thread>(&HttpClient::largeRequestsWorker, this);
}

void HttpClient::shutdown() {
    _should_stop = true;
    _large_queue.notify();

    if (_large_worker && _large_worker->joinable()) {
        _large_worker->join();
    }

    curl_multi_cleanup(_large_multi_handle);
    curl_global_cleanup();

    CallbackDispatcher::get().finish();
}

void HttpClient::submit(std::unique_ptr<ICommand> command) {
    int cloud_id = command->getId();
    command->execute(*_clouds[cloud_id]);
    _large_queue.push(std::move(command));
}

HttpClient& HttpClient::get() {
    static HttpClient instance;
    return instance;
}

void HttpClient::setMod(const std::string& mod) {
    if (mod == "Small") {
        _MAX_CONCURRENT = _MAX_CONCURRENT_SMALL;
    }
    else {
        _MAX_CONCURRENT = _MAX_CONCURRENT_LARGE;
    }
}

void HttpClient::setClouds(const std::unordered_map<int, std::shared_ptr<BaseStorage>>& clouds) {
    _clouds = clouds;
}

void HttpClient::largeRequestsWorker() {
    int still_running = 0;

    while (!_should_stop || !_large_queue.empty() || still_running > 0 || !_delayed_requests.empty()) {
        {
            std::unique_ptr<ICommand> request_command;
            while (_large_active_count < _MAX_CONCURRENT &&
                _large_queue.pop(request_command, [&]() { return _should_stop.load(); }))
            {
                CURL* easy = request_command->getHandle()._curl;
                curl_multi_add_handle(_large_multi_handle, easy);
                _active_handles.insert({ easy, std::move(request_command) });
                _large_active_count++;
            }
        }

        CURLMcode mc = curl_multi_perform(_large_multi_handle, &still_running);
        if (mc != CURLM_OK) {
            break;
        }

        int numfds = 0;
        mc = curl_multi_wait(_large_multi_handle, nullptr, 0, 1000, &numfds);
        if (mc != CURLM_OK) {
            std::cerr << "curl_multi_wait() failed, code: " << mc << '\n';
            break;
        }
        CURLMsg* msg;
        int msgs_left;
        while ((msg = curl_multi_info_read(_large_multi_handle, &msgs_left))) {
            if (msg->msg == CURLMSG_DONE) {
                CURL* easy = msg->easy_handle;
                curl_multi_remove_handle(_large_multi_handle, easy);
                _large_active_count--;

                long http_code = 0;
                curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);

                if (msg->data.result != CURLE_OK) {
                    _active_handles.erase(easy);
                }
                else if (http_code == 403 || http_code == 429 || http_code == 408 || http_code >= 500 && http_code < 600) {
                    std::cout << "file response: " << _active_handles[easy]->getHandle()._response << '\n';
                    _active_handles[easy]->getHandle().scheduleRetry();
                    _delayed_requests.emplace_back(std::move(_active_handles[easy]));
                    _active_handles.erase(easy);
                }
                else if (http_code != 200) {
                    std::cout << "http code: " << http_code << '\n';
                    std::cout << "file response: " << _active_handles[easy]->getHandle()._response << '\n';
                    _active_handles.erase(easy);
                }
                else {
                    std::cout << "file response: " << _active_handles[easy]->getHandle()._response << '\n';
                    CallbackDispatcher::get().submit(std::move(_active_handles[easy]));
                    _active_handles.erase(easy);
                }
            }
        }
        checkDelayedRequests();
    }
}


void HttpClient::checkDelayedRequests() {
    auto now = std::chrono::steady_clock::now();

    auto it = _delayed_requests.begin();
    while (it != _delayed_requests.end()) {
        if (it->get()->getHandle()._timer <= now) {
            _large_queue.push(std::move(*it));
            it = _delayed_requests.erase(it);
        }
        else {
            ++it;
        }
    }
}