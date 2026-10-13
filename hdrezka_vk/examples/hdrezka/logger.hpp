#pragma once
#include <cstdio>
#include <string>
#include <cstdarg>
#include <mutex>

// Several threads log at once (render loop + network workers): serialise.
inline std::mutex &write_log_mutex()
{
    static std::mutex mu;
    return mu;
}

inline void write_log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(write_log_mutex());
    FILE* f = fopen("/data/homebrew/PPSA99380/data/debug.log", "a");
    if (!f) f = fopen("/mnt/usb0/debug.log", "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fclose(f);
}

extern "C" void console_write_log(const char* fmt, ...);

