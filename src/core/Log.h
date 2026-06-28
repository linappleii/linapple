// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdarg>
#include <cstddef>

enum class LogLevel_t {
  k_silent = 0,
  k_error,
  k_warning,
  k_info,
  k_perf,
  k_debug,

  // Legacy enum member aliases for backwards compatibility
  kSilent = k_silent,
  kError = k_error,
  kWarning = k_warning,
  kInfo = k_info,
  kPerf = k_perf,
  kDebug = k_debug
};

using LogCallback_t = void (*)(LogLevel_t level, const char* message);

// Backwards compatibility aliases
using LogLevel = LogLevel_t;
using LogCallback = LogCallback_t;

namespace Logger {

constexpr size_t k_max_stack_log_size = 1024;
constexpr size_t kMaxStackLogSize = k_max_stack_log_size;

auto initialize() -> void;
auto destroy() -> void;

auto set_verbosity(LogLevel_t level) -> void;
auto set_callback(LogCallback_t callback) -> void;

#if defined(__GNUC__) || defined(__clang__)
#define ATTRIBUTE_FORMAT_PRINTF(fmt, first) \
  __attribute__((format(printf, fmt, first)))
#else
#define ATTRIBUTE_FORMAT_PRINTF(fmt, first)
#endif

auto error(const char* format, ...) -> void ATTRIBUTE_FORMAT_PRINTF(1, 2);
auto warning(const char* format, ...) -> void ATTRIBUTE_FORMAT_PRINTF(1, 2);
auto info(const char* format, ...) -> void ATTRIBUTE_FORMAT_PRINTF(1, 2);
auto perf(const char* format, ...) -> void ATTRIBUTE_FORMAT_PRINTF(1, 2);
auto debug(const char* format, ...) -> void ATTRIBUTE_FORMAT_PRINTF(1, 2);

auto log_message_v(LogLevel_t level, const char* format, va_list args) -> void;

// Legacy PascalCase forwarding inline functions for backwards compatibility
static inline auto Initialize() -> void { initialize(); }
static inline auto Destroy() -> void { destroy(); }
static inline auto SetVerbosity(LogLevel_t level) -> void {
  set_verbosity(level);
}
static inline auto SetCallback(LogCallback_t callback) -> void {
  set_callback(callback);
}
static inline auto LogMessageV(LogLevel_t level, const char* format,
                               va_list args) -> void {
  log_message_v(level, format, args);
}

static inline auto Error(const char* format, ...)
    -> void ATTRIBUTE_FORMAT_PRINTF(1, 2);
static inline auto Error(const char* format, ...) -> void {
  va_list args;
  va_start(args, format);
  log_message_v(LogLevel_t::k_error, format, args);
  va_end(args);
}

static inline auto Warning(const char* format, ...)
    -> void ATTRIBUTE_FORMAT_PRINTF(1, 2);
static inline auto Warning(const char* format, ...) -> void {
  va_list args;
  va_start(args, format);
  log_message_v(LogLevel_t::k_warning, format, args);
  va_end(args);
}

static inline auto Info(const char* format, ...)
    -> void ATTRIBUTE_FORMAT_PRINTF(1, 2);
static inline auto Info(const char* format, ...) -> void {
  va_list args;
  va_start(args, format);
  log_message_v(LogLevel_t::k_info, format, args);
  va_end(args);
}

static inline auto Perf(const char* format, ...)
    -> void ATTRIBUTE_FORMAT_PRINTF(1, 2);
static inline auto Perf(const char* format, ...) -> void {
  va_list args;
  va_start(args, format);
  log_message_v(LogLevel_t::k_perf, format, args);
  va_end(args);
}

static inline auto Debug(const char* format, ...)
    -> void ATTRIBUTE_FORMAT_PRINTF(1, 2);
static inline auto Debug(const char* format, ...) -> void {
  va_list args;
  va_start(args, format);
  log_message_v(LogLevel_t::k_debug, format, args);
  va_end(args);
}

#undef ATTRIBUTE_FORMAT_PRINTF

}  // namespace Logger

#if defined(DEBUG) || defined(_DEBUG)
#define LOG(format, ...) Logger::debug(format, ##__VA_ARGS__)
#else
#define LOG(format, ...) ((void)0)
#endif
