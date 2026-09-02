#pragma once
#include <cstdio>
#include <string>

namespace eng::log {

// Logs go to stderr so stdout stays a clean JSON channel for the AI.
template <class... A> void info(const char* fmt, A... a) {
    std::fprintf(stderr, "[info] ");
    std::fprintf(stderr, fmt, a...);
    std::fprintf(stderr, "\n");
}
template <class... A> void warn(const char* fmt, A... a) {
    std::fprintf(stderr, "[warn] ");
    std::fprintf(stderr, fmt, a...);
    std::fprintf(stderr, "\n");
}
template <class... A> void error(const char* fmt, A... a) {
    std::fprintf(stderr, "[error] ");
    std::fprintf(stderr, fmt, a...);
    std::fprintf(stderr, "\n");
}

} // namespace eng::log
