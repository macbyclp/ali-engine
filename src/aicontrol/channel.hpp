#pragma once
#include <nlohmann/json.hpp>
#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace eng {

// Line-delimited JSON over stdin/stdout. One request object per line in, one
// response object per line out. A background thread reads stdin so the render
// loop never blocks; the main thread drains and answers.
class ControlChannel {
public:
    ControlChannel();
    ~ControlChannel();

    bool poll(nlohmann::json& out_request);   // false if nothing pending
    void respond(const nlohmann::json& response);

private:
    std::thread reader_;
    std::atomic<bool> stop_{false};
    std::mutex mtx_;
    std::queue<nlohmann::json> in_;
    std::mutex out_mtx_;
};

} // namespace eng
