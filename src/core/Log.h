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
auto get_verbosity() -> LogLevel_t;
auto set_callback(LogCallback_t callback) -> void;

[[gnu::format(printf, 1, 2)]] auto error(const char* format, ...) -> void;
[[gnu::format(printf, 1, 2)]] auto warning(const char* format, ...) -> void;
[[gnu::format(printf, 1, 2)]] auto info(const char* format, ...) -> void;
[[gnu::format(printf, 1, 2)]] auto perf(const char* format, ...) -> void;
[[gnu::format(printf, 1, 2)]] auto debug(const char* format, ...) -> void;

auto log_message_v(LogLevel_t level, const char* format, va_list args) -> void;

}  // namespace Logger
