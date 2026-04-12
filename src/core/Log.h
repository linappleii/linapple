#include <cstdint>
#pragma once

enum class LogLevel {
    Silent = 0,
    Error = 1,
    Warn = 2,
    Info = 3,
    Perf = 4,
    Debug = 5
};

namespace Logger {
    void Initialize();
    void SetVerbosity(LogLevel level);
    void Error(const char* format, ...);
    void Warn(const char* format, ...);
    void Info(const char* format, ...);
    void Perf(const char* format, ...);
    void Debug(const char* format, ...);
    void Destroy();
}

#ifndef _VC71
#define LOG(format, ...) Logger::Debug(format, __VA_ARGS__)
#endif
