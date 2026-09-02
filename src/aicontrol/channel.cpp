#include "aicontrol/channel.hpp"
#include "core/log.hpp"
#include <cstdio>
#include <iostream>

using nlohmann::json;

namespace eng {

ControlChannel::ControlChannel(bool quit_on_eof) {
    reader_ = std::thread([this, quit_on_eof] {
        std::string line;
        while (!stop_ && std::getline(std::cin, line)) {
            if (line.empty()) continue;
            try {
                json j = json::parse(line);
                std::lock_guard<std::mutex> lk(mtx_);
                in_.push(std::move(j));
            } catch (const std::exception& ex) {
                json err = {{"ok", false}, {"error", std::string("parse: ") + ex.what()}};
                respond(err);
            }
        }
        // stdin closed -> ask main loop to quit (unless a human is driving the editor)
        if (quit_on_eof) {
            std::lock_guard<std::mutex> lk(mtx_);
            in_.push(json{{"method", "quit"}, {"id", nullptr}});
        }
    });
}

ControlChannel::~ControlChannel() {
    stop_ = true;
    // getline may still block; detach so shutdown is not stuck on it.
    if (reader_.joinable()) reader_.detach();
}

bool ControlChannel::poll(json& out) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (in_.empty()) return false;
    out = std::move(in_.front());
    in_.pop();
    return true;
}

void ControlChannel::respond(const json& response) {
    std::lock_guard<std::mutex> lk(out_mtx_);
    std::string s = response.dump();
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

} // namespace eng
