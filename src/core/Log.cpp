#include "core/Log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <array>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "core/Util_Path.h"

namespace Logger {

static LogLevel g_current_verbosity = LogLevel::kInfo;
static LogCallback g_external_callback = nullptr;
static std::unique_ptr<FILE, int (*)(FILE*)> g_log_file(nullptr, fclose);
static std::mutex g_log_mutex;

/**
 * @brief Thread-safe internal logging function with small-buffer optimization.
 */
static void OutputLogMessage(LogLevel level, const char* format, va_list args) {
  if (level > g_current_verbosity) {
    return;
  }

  // Small-buffer optimization: Try a stack buffer first.
  std::array<char, kMaxStackLogSize> stack_buffer{};
  va_list args_copy;

  // Justification for NOLINT: va_list is often an array type that decays to a pointer
  // when passed to C-library functions like va_copy, vsnprintf, and va_end.
  // This is an unavoidable consequence of using the C standard library for formatting.
  va_copy(args_copy, args); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  int length = vsnprintf(stack_buffer.data(), stack_buffer.size(), format, args_copy); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  va_end(args_copy); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)

  if (length < 0) {
    return;
  }

  const char* final_message = nullptr;
  std::vector<char> heap_buffer;

  if (static_cast<size_t>(length) < stack_buffer.size()) {
    // Fits in stack buffer
    final_message = stack_buffer.data();
  } else {
    // Fallback to heap for large messages
    heap_buffer.resize(static_cast<size_t>(length) + 1);
    vsnprintf(heap_buffer.data(), heap_buffer.size(), format, args);
    final_message = heap_buffer.data();
  }

  // Ensure thread-safe output to all sinks
  std::lock_guard<std::mutex> lock(g_log_mutex);

  // 1. Output to file
  if (g_log_file) {
    fprintf(g_log_file.get(), "%s", final_message);
    fflush(g_log_file.get());
  }

  // 2. Output to external callback (Frontend/Debugger)
  if (g_external_callback) {
    g_external_callback(level, final_message);
  }

  // 3. Output to terminal (Console sinks)
  if (level <= LogLevel::kError) {
    fprintf(stderr, "ERROR: %s", final_message);
    fflush(stderr);
  } else if (level == LogLevel::kPerf) {
    printf("PERF: %s", final_message);
    fflush(stdout);
  } else if (level <= LogLevel::kInfo) {
    printf("%s", final_message);
    fflush(stdout);
  }
}

void Initialize() {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  if (!g_log_file) {
    std::string data_dir = Path::GetUserDataDir();
    Path::EnsureDirExists(data_dir);
    // Justification for NOLINT: fopen returns a raw pointer that is immediately
    // wrapped in a unique_ptr for safe resource management.
    g_log_file.reset(fopen((data_dir + "linapple.log").c_str(), "a+t")); // NOLINT(cppcoreguidelines-owning-memory)
  }

  if (g_log_file) {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    fprintf(g_log_file.get(), "*** Logging started: %s\n", ss.str().c_str());
  }
}

void SetVerbosity(LogLevel level) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_current_verbosity = level;
}

void SetCallback(LogCallback callback) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_external_callback = callback;
}

void LogMessageV(LogLevel level, const char* format, va_list args) {
  OutputLogMessage(level, format, args);
}

void Error(const char* format, ...) {
  va_list args;
  // Justification for NOLINT: va_start/va_end and passing va_list triggers
  // unavoidable pointer decay as part of the C standard library variadic mechanism.
  va_start(args, format); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  OutputLogMessage(LogLevel::kError, format, args); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  va_end(args); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
}

void Warning(const char* format, ...) {
  va_list args;
  va_start(args, format); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  OutputLogMessage(LogLevel::kWarning, format, args); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  va_end(args); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
}

void Info(const char* format, ...) {
  va_list args;
  va_start(args, format); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  OutputLogMessage(LogLevel::kInfo, format, args); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  va_end(args); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
}

void Perf(const char* format, ...) {
  va_list args;
  va_start(args, format); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  OutputLogMessage(LogLevel::kPerf, format, args); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  va_end(args); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
}

void Debug(const char* format, ...) {
  va_list args;
  va_start(args, format); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  OutputLogMessage(LogLevel::kDebug, format, args); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  va_end(args); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
}

void Destroy() {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  if (g_log_file) {
    fprintf(g_log_file.get(), "*** Logging ended\n\n");
    g_log_file.reset();
  }
}

}  // namespace Logger
