// SPDX-License-Identifier: GPL-2.0-only
#include "core/Log.h"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#include "core/Util_Path.h"

namespace Logger {

namespace {

constexpr size_t k_time_buffer_size = 32;

std::atomic<LogLevel_t> g_current_verbosity{LogLevel_t::k_info};
std::atomic<bool> g_file_logging_enabled{true};
std::string g_custom_log_path;
LogCallback_t g_external_callback = nullptr;
LogCallbackWithContext_t g_context_callback = nullptr;
void* g_callback_user_data = nullptr;
FilePtr_t g_log_file{nullptr, std::fclose};
std::mutex g_log_mutex;

auto format_current_time(char* out_buf, size_t buf_size) -> void {
  const std::time_t now = std::time(nullptr);
  struct tm tm_buf{};
  localtime_r(&now, &tm_buf);
  std::strftime(out_buf, buf_size, "%Y-%m-%d %H:%M:%S", &tm_buf);
}

}  // namespace

auto log_level_to_string(LogLevel_t level) noexcept -> const char* {
  switch (level) {
    case LogLevel_t::silent:
      return "SILENT";
    case LogLevel_t::error:
      return "ERROR";
    case LogLevel_t::warning:
      return "WARN";
    case LogLevel_t::info:
      return "INFO";
    case LogLevel_t::perf:
      return "PERF";
    case LogLevel_t::debug:
      return "DEBUG";
    default:
      return "UNKNOWN";
  }
}

static auto output_log_message(LogLevel_t level, const char* format,
                               va_list args) -> void {
  if (format == nullptr) {
    return;
  }

  if (level > g_current_verbosity.load(std::memory_order_relaxed)) {
    return;
  }

  std::array<char, k_max_stack_log_size> stack_buffer{};
  va_list args_copy;
  va_copy(args_copy, args);
  const int length = std::vsnprintf(stack_buffer.data(), stack_buffer.size(),
                                    format, args_copy);
  va_end(args_copy);

  if (length < 0) {
    return;
  }

  const char* final_message = nullptr;
  std::vector<char> heap_buffer;

  if (static_cast<size_t>(length) < stack_buffer.size()) {
    final_message = stack_buffer.data();
  } else {
    heap_buffer.resize(static_cast<size_t>(length) + 1);
    va_list heap_args;
    va_copy(heap_args, args);
    std::vsnprintf(heap_buffer.data(), heap_buffer.size(), format, heap_args);
    va_end(heap_args);
    final_message = heap_buffer.data();
  }

  LogCallback_t ext_cb = nullptr;
  LogCallbackWithContext_t ctx_cb = nullptr;
  void* user_data = nullptr;

  {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    ext_cb = g_external_callback;
    ctx_cb = g_context_callback;
    user_data = g_callback_user_data;

    if (g_log_file && g_file_logging_enabled.load(std::memory_order_relaxed)) {
      std::array<char, k_time_buffer_size> time_str{};
      format_current_time(time_str.data(), time_str.size());
      std::fprintf(g_log_file.get(), "[%s] [%-5s] %s", time_str.data(),
                   log_level_to_string(level), final_message);
      std::fflush(g_log_file.get());
    }
  }

  if (ext_cb != nullptr) {
    ext_cb(level, final_message);
  }
  if (ctx_cb != nullptr) {
    ctx_cb(level, final_message, user_data);
  }

  if (level <= LogLevel_t::k_error) {
    std::fprintf(stderr, "ERROR: %s", final_message);
    std::fflush(stderr);
  } else if (level == LogLevel_t::k_perf) {
    std::printf("PERF: %s", final_message);
    std::fflush(stdout);
  } else if (level <= LogLevel_t::k_info) {
    std::printf("%s", final_message);
    std::fflush(stdout);
  }
}

auto initialize() -> void {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  if (!g_log_file && g_file_logging_enabled.load(std::memory_order_relaxed)) {
    std::string log_path;
    if (!g_custom_log_path.empty()) {
      log_path = g_custom_log_path;
    } else {
      const std::string data_dir = Path::get_user_data_dir();
      Path::ensure_dir_exists(data_dir);
      log_path = Path::join(data_dir, "linapple.log");
    }
    g_log_file.reset(std::fopen(log_path.c_str(), "a"));
  }

  if (g_log_file && g_file_logging_enabled.load(std::memory_order_relaxed)) {
    std::array<char, k_time_buffer_size> time_str{};
    format_current_time(time_str.data(), time_str.size());
    std::fprintf(g_log_file.get(), "*** Logging started: %s\n",
                 time_str.data());
    std::fflush(g_log_file.get());
  }
}

auto set_verbosity(LogLevel_t level) noexcept -> void {
  g_current_verbosity.store(level, std::memory_order_relaxed);
}

auto get_verbosity() noexcept -> LogLevel_t {
  return g_current_verbosity.load(std::memory_order_relaxed);
}

auto set_callback(LogCallback_t callback) -> void {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_external_callback = callback;
}

auto set_callback_with_context(LogCallbackWithContext_t callback,
                               void* user_data) -> void {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_context_callback = callback;
  g_callback_user_data = user_data;
}

auto set_log_path(const char* path) -> void {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  if (g_log_file) {
    g_log_file.reset();
  }
  if (path != nullptr && path[0] != '\0') {
    g_custom_log_path = path;
  } else {
    g_custom_log_path.clear();
  }
}

auto enable_file_logging(bool enable) noexcept -> void {
  g_file_logging_enabled.store(enable, std::memory_order_relaxed);
}

auto is_file_logging_enabled() noexcept -> bool {
  return g_file_logging_enabled.load(std::memory_order_relaxed);
}

auto log_message_v(LogLevel_t level, const char* format, va_list args) -> void {
  output_log_message(level, format, args);
}

auto error(const char* format, ...) -> void {
  va_list args;
  va_start(args, format);
  output_log_message(LogLevel_t::k_error, format, args);
  va_end(args);
}

auto warning(const char* format, ...) -> void {
  va_list args;
  va_start(args, format);
  output_log_message(LogLevel_t::k_warning, format, args);
  va_end(args);
}

auto info(const char* format, ...) -> void {
  va_list args;
  va_start(args, format);
  output_log_message(LogLevel_t::k_info, format, args);
  va_end(args);
}

auto perf(const char* format, ...) -> void {
  va_list args;
  va_start(args, format);
  output_log_message(LogLevel_t::k_perf, format, args);
  va_end(args);
}

auto debug(const char* format, ...) -> void {
  va_list args;
  va_start(args, format);
  output_log_message(LogLevel_t::k_debug, format, args);
  va_end(args);
}

auto destroy() -> void {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  if (g_log_file) {
    std::fprintf(g_log_file.get(), "*** Logging ended\n\n");
    std::fflush(g_log_file.get());
    g_log_file.reset();
  }
  g_external_callback = nullptr;
  g_context_callback = nullptr;
  g_callback_user_data = nullptr;
  g_custom_log_path.clear();
}

}  // namespace Logger
