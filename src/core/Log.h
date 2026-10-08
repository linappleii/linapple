// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>

enum class LogLevel : uint8_t {
  silent = 0,
  error = 1,
  warning = 2,
  info = 3,
  perf = 4,
  debug = 5,
};

using LogCallback = void (*)(LogLevel level, const char* message);
using LogCallbackWithContext = void (*)(LogLevel level, const char* message,
                                          void* user_data);

namespace Logger {

constexpr size_t max_stack_log_size = 1024;

auto initialize() -> void;
auto destroy() -> void;

auto set_verbosity(LogLevel level) noexcept -> void;
auto get_verbosity() noexcept -> LogLevel;

auto set_callback(LogCallback callback) -> void;
auto set_callback_with_context(LogCallbackWithContext callback,
                               void* user_data) -> void;

auto set_log_path(const char* path) -> void;
auto enable_file_logging(bool enable) noexcept -> void;
auto is_file_logging_enabled() noexcept -> bool;

[[gnu::format(printf, 1, 2)]] auto error(const char* format, ...) -> void;
[[gnu::format(printf, 1, 2)]] auto warning(const char* format, ...) -> void;
[[gnu::format(printf, 1, 2)]] auto info(const char* format, ...) -> void;
[[gnu::format(printf, 1, 2)]] auto perf(const char* format, ...) -> void;
[[gnu::format(printf, 1, 2)]] auto debug(const char* format, ...) -> void;

[[gnu::format(printf, 2, 0)]] auto log_message_v(LogLevel level,
                                                 const char* format,
                                                 va_list args) -> void;
auto log_level_to_string(LogLevel) noexcept -> const char*;

}  // namespace Logger
