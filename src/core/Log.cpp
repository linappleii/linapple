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

constexpr size_t time_buffer_size = 32;

std::atomic<LogLevel> current_verbosity{LogLevel::info};
std::atomic<bool> file_logging_enabled{true};
std::string custom_log_path;
LogCallback external_callback = nullptr;
LogCallbackWithContext context_callback = nullptr;
void* callback_user_data = nullptr;
FilePtr log_file{nullptr, std::fclose};
std::mutex log_mutex;

struct ConsoleConfig {
  bool to_stderr;
  const char* prefix;
};

constexpr std::array<ConsoleConfig, 6> console_configs = {
    {
        {false, nullptr},   // silent (no console output)
        {true, "ERROR: "},  // error -> stderr
        {true, "WARN: "},   // warning -> stderr
        {false, ""},        // info -> stdout
        {false, "PERF: "},  // perf -> stdout
        {false, ""},        // debug -> stdout
    },
};

auto format_current_time(char* out_buf, size_t buf_size) -> void {
  if (out_buf == nullptr || buf_size == 0) {
    return;
  }
  out_buf[0] = '\0';
  const std::time_t now = std::time(nullptr);
  struct tm calendar_time{};
  if (localtime_r(&now, &calendar_time) == nullptr) {
    return;
  }
  std::strftime(out_buf, buf_size, "%Y-%m-%d %H:%M:%S", &calendar_time);
}

}  // namespace

auto log_level_to_string(LogLevel level) noexcept -> const char* {
  static constexpr std::array<const char*, 6> level_names = {
      {"SILENT", "ERROR", "WARN", "INFO", "PERF", "DEBUG"},
  };

  const auto index = static_cast<size_t>(level);
  if (index < level_names.size()) {
    return level_names[index];
  }
  return "UNKNOWN";
}

static auto output_log_message(LogLevel level, const char* format,
                               va_list args) -> void {
  if (format == nullptr || level == LogLevel::silent) {
    return;
  }

  if (level > current_verbosity.load(std::memory_order_relaxed)) {
    return;
  }

  std::array<char, max_stack_log_size> stack_buffer{};
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

  LogCallback active_external_callback = nullptr;
  LogCallbackWithContext active_context_callback = nullptr;
  void* active_user_data = nullptr;

  {
    std::lock_guard<std::mutex> lock(log_mutex);
    active_external_callback = external_callback;
    active_context_callback = context_callback;
    active_user_data = callback_user_data;

    if (log_file && file_logging_enabled.load(std::memory_order_relaxed)) {
      std::array<char, time_buffer_size> time_str{};
      format_current_time(time_str.data(), time_str.size());
      std::fprintf(log_file.get(), "[%s] [%-5s] %s", time_str.data(),
                   log_level_to_string(level), final_message);
      std::fflush(log_file.get());
    }
  }

  if (active_external_callback != nullptr) {
    active_external_callback(level, final_message);
  }
  if (active_context_callback != nullptr) {
    active_context_callback(level, final_message, active_user_data);
  }

  const auto index = static_cast<size_t>(level);
  if (index < console_configs.size() &&
      console_configs[index].prefix != nullptr) {
    const auto& cfg = console_configs[index];
    FILE* stream = cfg.to_stderr ? stderr : stdout;
    std::fprintf(stream, "%s%s", cfg.prefix, final_message);
    std::fflush(stream);
  }
}

auto initialize() -> void {
  std::lock_guard<std::mutex> lock(log_mutex);
  if (!file_logging_enabled.load(std::memory_order_relaxed) || log_file) {
    return;
  }

  std::string log_path = custom_log_path;
  if (log_path.empty()) {
    const std::string data_dir = Path::get_user_data_dir();
    Path::ensure_dir_exists(data_dir);
    log_path = Path::join(data_dir, "linapple.log");
  }

  log_file.reset(std::fopen(log_path.c_str(), "a"));
  if (!log_file) {
    return;
  }

  std::array<char, time_buffer_size> time_str{};
  format_current_time(time_str.data(), time_str.size());
  std::fprintf(log_file.get(), "*** Logging started: %s\n", time_str.data());
  std::fflush(log_file.get());
}

auto set_verbosity(LogLevel level) noexcept -> void {
  current_verbosity.store(level, std::memory_order_relaxed);
}

auto get_verbosity() noexcept -> LogLevel {
  return current_verbosity.load(std::memory_order_relaxed);
}

auto set_callback(LogCallback callback) -> void {
  std::lock_guard<std::mutex> lock(log_mutex);
  external_callback = callback;
}

auto set_callback_with_context(LogCallbackWithContext callback,
                               void* user_data) -> void {
  std::lock_guard<std::mutex> lock(log_mutex);
  context_callback = callback;
  callback_user_data = user_data;
}

auto set_log_path(const char* path) -> void {
  std::lock_guard<std::mutex> lock(log_mutex);
  if (log_file) {
    log_file.reset();
  }
  if (path != nullptr && path[0] != '\0') {
    custom_log_path = path;
  } else {
    custom_log_path.clear();
  }
}

auto enable_file_logging(bool enable) noexcept -> void {
  file_logging_enabled.store(enable, std::memory_order_relaxed);
}

auto is_file_logging_enabled() noexcept -> bool {
  return file_logging_enabled.load(std::memory_order_relaxed);
}

auto log_message_v(LogLevel level, const char* format, va_list args) -> void {
  output_log_message(level, format, args);
}

auto error(const char* format, ...) -> void {
  va_list args;
  va_start(args, format);
  output_log_message(LogLevel::error, format, args);
  va_end(args);
}

auto warning(const char* format, ...) -> void {
  va_list args;
  va_start(args, format);
  output_log_message(LogLevel::warning, format, args);
  va_end(args);
}

auto info(const char* format, ...) -> void {
  va_list args;
  va_start(args, format);
  output_log_message(LogLevel::info, format, args);
  va_end(args);
}

auto perf(const char* format, ...) -> void {
  va_list args;
  va_start(args, format);
  output_log_message(LogLevel::perf, format, args);
  va_end(args);
}

auto debug(const char* format, ...) -> void {
  va_list args;
  va_start(args, format);
  output_log_message(LogLevel::debug, format, args);
  va_end(args);
}

auto destroy() -> void {
  std::lock_guard<std::mutex> lock(log_mutex);
  if (log_file) {
    std::fprintf(log_file.get(), "*** Logging ended\n\n");
    std::fflush(log_file.get());
    log_file.reset();
  }
  external_callback = nullptr;
  context_callback = nullptr;
  callback_user_data = nullptr;
  custom_log_path.clear();
}

}  // namespace Logger
