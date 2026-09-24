#pragma once
#include <cstdio>
#include <string>

namespace eng::log {

// Logs go to stderr so stdout stays a clean JSON channel for the AI.
// With no arguments the message is written verbatim (never parsed as a format),
// so a stray '%' in a plain message cannot read garbage off the stack.
template <class... A> void write(const char* tag, const char* fmt, A... a) {
    std::fputs(tag, stderr);
    if constexpr (sizeof...(A) == 0) std::fputs(fmt, stderr);
    else std::fprintf(stderr, fmt, a...);
    std::fputc('\n', stderr);
}
template <class... A> void info(const char* fmt, A... a) { write("[info] ", fmt, a...); }
template <class... A> void warn(const char* fmt, A... a) { write("[warn] ", fmt, a...); }
template <class... A> void error(const char* fmt, A... a) { write("[error] ", fmt, a...); }

} // namespace eng::log
