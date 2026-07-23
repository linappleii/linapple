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

};

using LogCallback_t = void (*)(LogLevel_t level, const char* message);


namespace Logger {

constexpr size_t k_max_stack_log_size = 1024;

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

}  // namespace Logger

#if defined(DEBUG) || defined(_DEBUG)
#define LOG(format, ...) Logger::debug(format, ##__VA_ARGS__)
#else
#define LOG(format, ...) ((void)0)
#endif
