// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>

enum class LogLevel_t : uint8_t {
  silent = 0,
  error = 1,
  warning = 2,
  info = 3,
  perf = 4,
  debug = 5,
};

using LogCallback_t = void (*)(LogLevel_t level, const char* message);
using LogCallbackWithContext_t = void (*)(LogLevel_t level, const char* message,
                                          void* user_data);

namespace Logger {

constexpr size_t max_stack_log_size = 1024;

auto initialize() -> void;
auto destroy() -> void;

auto set_verbosity(LogLevel_t level) noexcept -> void;
[[nodiscard]] auto get_verbosity() noexcept -> LogLevel_t;

auto set_callback(LogCallback_t callback) -> void;
auto set_callback_with_context(LogCallbackWithContext_t callback,
                               void* user_data) -> void;

auto set_log_path(const char* path) -> void;
auto enable_file_logging(bool enable) noexcept -> void;
[[nodiscard]] auto is_file_logging_enabled() noexcept -> bool;

[[gnu::format(printf, 1, 2)]] auto error(const char* format, ...) -> void;
[[gnu::format(printf, 1, 2)]] auto warning(const char* format, ...) -> void;
[[gnu::format(printf, 1, 2)]] auto info(const char* format, ...) -> void;
[[gnu::format(printf, 1, 2)]] auto perf(const char* format, ...) -> void;
[[gnu::format(printf, 1, 2)]] auto debug(const char* format, ...) -> void;

[[gnu::format(printf, 2, 0)]] auto log_message_v(LogLevel_t level,
                                                 const char* format,
                                                 va_list args) -> void;
[[nodiscard]] auto log_level_to_string(LogLevel_t) noexcept -> const char*;

}  // namespace Logger
