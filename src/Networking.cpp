#include "Networking.h"
#include "commands.h"
#include "logger.h"

HttpClient::HttpClient()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);

    _large_multi_handle = curl_multi_init();
}

void HttpClient::syncRequest(const std::unique_ptr<RequestHandle>& handle) {
    if (!handle->_curl) {
        LOG_ERROR("HttpClient", "No CURL handle in syncRequest");
        return;
    }

    while (true) {
        CURLcode res = curl_easy_perform(handle->_curl);
        if (res != CURLE_OK) {
            LOG_ERROR("HttpClient", "curl_easy_perform() failed with code: %i and err msg: %s", res, curl_easy_strerror(res));
            break;
        }
        long http_code = 0;
        curl_easy_getinfo(handle->_curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (http_code == 403 || http_code == 429 || http_code == 408 || http_code >= 500 && http_code < 600) {
            LOG_WARNING("HttpClient", "HTTP code %i, scheduling retry", http_code);

            handle->scheduleRetry();
            std::this_thread::sleep_until(handle->_timer);
            continue;
        }
        else if (http_code != 200) {
            LOG_ERROR("HttpClient", "Unexpected HTTP code %i with response: %s", http_code, handle->_response);
            break;
        }
        else {
            LOG_DEBUG(
                "HttpClient",
                "Request succeeded with response: %s",
                handle->_response
            );
            break;
        }
    }

}

void HttpClient::shutdown() {
    LOG_INFO("HttpClient", "Shutting down...");

    _should_stop.store(true, std::memory_order_release);
    _large_queue.close();

    if (_large_worker && _large_worker->joinable()) {
        _large_worker->join();
    }

    LOG_INFO("HttpClient", "Worker thread joined, cleaning up CURL");

    curl_multi_cleanup(_large_multi_handle);
    curl_global_cleanup();

    CallbackDispatcher::get().finish();

    LOG_INFO("HttpClient", "Shutdown complete");
}

void HttpClient::start() {
    LOG_INFO("HttpClient", "Starting large worker...");
    _large_worker = std::make_unique<std::thread>(&HttpClient::largeRequestsWorker, this);
}

HttpClient::~HttpClient() {
    this->shutdown();
}

bool HttpClient::isIdle() const noexcept {
    return _large_active_count.isIdle();
}

void HttpClient::waitUntilIdle() const {
    _large_active_count.waitUntilIdle();
}

void HttpClient::submit(std::unique_ptr<ICommand> command) {
    int cloud_id = command->getId();
    LOG_DEBUG(
        "HttpClient",
        "Request submited for file: %s and cloud: %s",
        command->getTarget(),
        CloudResolver::getName(command->getId())
    );
    command->execute(_clouds[cloud_id]);
    if (!command->getHandle()._curl) {
        LOG_ERROR("HttpClient", "No CURL handle in multi_perform: for target:", command->getTarget());
        return;
    }
    _large_active_count.increment();
    _large_queue.push(std::move(command));
}

HttpClient& HttpClient::get() {
    static HttpClient instance;
    return instance;
}

void HttpClient::setClouds(const std::unordered_map<int, std::shared_ptr<BaseStorage>>& clouds) {
    _clouds = clouds;
}

void HttpClient::largeRequestsWorker() {
    ThreadNamer::setThreadName("HttpClient Worker");
    LOG_INFO("HttpClient", "Started");
    int still_running = 0;

    while (!_should_stop || !_large_queue.empty() || still_running > 0 || !_delayed_requests.empty()) {
        std::unique_ptr<ICommand> request_command;
        if (_large_active_count.isIdle()) {
            if (_large_queue.pop(request_command)) {
                LOG_DEBUG("HttpClient", "Adding CURL handle for file: %s and cloud: %s", request_command->getTarget(), CloudResolver::getName(request_command->getId()));
                curl_multi_add_handle(_large_multi_handle, request_command->getHandle()._curl);
                _active_handles.emplace(request_command->getHandle()._curl, std::move(request_command));
            }
        }
        else {
            while (_large_queue.try_pop(request_command) && _large_active_count.get() < _MAX_CONCURRENT) {
                LOG_DEBUG("HttpClient", "Adding CURL handle for file: %s and cloud: %s", request_command->getTarget(), CloudResolver::getName(request_command->getId()));
                curl_multi_add_handle(_large_multi_handle, request_command->getHandle()._curl);
                _active_handles.emplace(request_command->getHandle()._curl, std::move(request_command));
            }
        }

        CURLMcode mc = curl_multi_perform(_large_multi_handle, &still_running);
        if (mc != CURLM_OK) {
            LOG_ERROR("HttpClient", "curl_muli_perform() failed, code: %i", mc);
            break;
        }

        int numfds = 0;
        mc = curl_multi_wait(_large_multi_handle, nullptr, 0, 1000, &numfds);
        if (mc != CURLM_OK) {
            LOG_ERROR("HttpClient", "curl_muli_wait() failed, code: %i", mc);
            break;
        }
        CURLMsg* msg;
        int msgs_left;
        while ((msg = curl_multi_info_read(_large_multi_handle, &msgs_left))) {
            if (msg->msg == CURLMSG_DONE) {
                CURL* easy = msg->easy_handle;
                curl_multi_remove_handle(_large_multi_handle, easy);

                if (_active_handles[easy]->getTarget() != "CONFIG") {
                    LOG_INFO("HttpClient",
                        "Request completed for file: %s and cloud: %s",
                        _active_handles[easy]->getTarget(),
                        CloudResolver::getName(_active_handles[easy]->getId())
                    );
                }
                else {
                    LOG_INFO("HttpClient",
                        "Configuration request completed for: %s",
                        CloudResolver::getName(_active_handles[easy]->getId())
                    );
                }

                long http_code = 0;
                curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);

                if (msg->data.result != CURLE_OK) {
                    LOG_ERROR("HttpClient", "curl failed with code: %i and msg: %s", mc, curl_easy_strerror(msg->data.result));
                    _large_active_count.decrement();
                }
                else if (http_code == 403 || http_code == 429 || http_code == 408 || http_code >= 500 && http_code < 600) {
                    LOG_WARNING("HttpClient", "Scheduling retry for reponcse: %s", _active_handles[easy]->getHandle()._response);

                    _active_handles[easy]->getHandle().scheduleRetry();
                    _delayed_requests.emplace_back(std::move(_active_handles[easy]));
                }
                else if (http_code != 200) {
                    LOG_ERROR("HttpClient", "Unexpected HTTP code %i with response: %s", http_code, _active_handles[easy]->getHandle()._response);
                    _large_active_count.decrement();
                }
                else {
                    LOG_DEBUG(
                        "HttpClient",
                        "Request succeeded with response: %s",
                        _active_handles[easy]->getHandle()._response
                    );
                    CallbackDispatcher::get().submit(std::move(_active_handles[easy]));
                    _large_active_count.decrement();
                }
                _active_handles.erase(easy);
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