// SPDX-License-Identifier: GPL-2.0-only
#include "core/Log.h"

// C standard library variadic va_list formatting and system logging operations
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-owning-memory,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-type-cstyle-cast, misc-include-cleaner,
// cppcoreguidelines-avoid-magic-numbers, modernize-use-scoped-lock,
// cppcoreguidelines-init-variables)
#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "core/Util_Path.h"

namespace Logger {

static std::atomic<LogLevel_t> g_current_verbosity{LogLevel_t::k_info};
static LogCallback_t g_external_callback = nullptr;
static std::unique_ptr<FILE, int (*)(FILE*)> g_log_file(nullptr, std::fclose);
static std::mutex g_log_mutex;

static auto output_log_message(LogLevel_t level, const char* format,
                               va_list args) -> void {
  if (format == nullptr) {
    return;
  }

  // Atomic load prevents thread data races during verbosity filtering
  if (level > g_current_verbosity.load(std::memory_order_relaxed)) {
    return;
  }

  // Small-buffer optimization: Try a stack buffer first
  std::array<char, k_max_stack_log_size> stack_buffer{};
  va_list args_copy;

  va_copy(args_copy, args);
  int length = std::vsnprintf(stack_buffer.data(), stack_buffer.size(), format,
                              args_copy);
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

  std::lock_guard<std::mutex> lock(g_log_mutex);

  if (g_log_file) {
    std::fprintf(g_log_file.get(), "%s", final_message);
    std::fflush(g_log_file.get());
  }

  if (g_external_callback != nullptr) {
    g_external_callback(level, final_message);
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
  if (!g_log_file) {
    std::string data_dir = Path::get_user_data_dir();
    Path::ensure_dir_exists(data_dir);
    g_log_file.reset(std::fopen((data_dir + "linapple.log").c_str(), "a+t"));
  }

  if (g_log_file) {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &in_time_t);
#else
    localtime_r(&in_time_t, &tm_buf);
#endif
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    std::fprintf(g_log_file.get(), "*** Logging started: %s\n",
                 ss.str().c_str());
  }
}

auto set_verbosity(LogLevel_t level) -> void {
  g_current_verbosity.store(level, std::memory_order_relaxed);
}

auto set_callback(LogCallback_t callback) -> void {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_external_callback = callback;
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
    g_log_file.reset();
  }
}

}  // namespace Logger
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-owning-memory,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-type-cstyle-cast, misc-include-cleaner,
// cppcoreguidelines-avoid-magic-numbers, modernize-use-scoped-lock,
// cppcoreguidelines-init-variables)
