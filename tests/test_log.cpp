// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/Log.h"
#include "core/Util_Path.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

struct LogCapture {
  std::vector<LogLevel> levels;
  std::vector<std::string> messages;

  void clear() {
    levels.clear();
    messages.clear();
  }
};

LogCapture test_capture;

auto global_test_callback(LogLevel level, const char* message) -> void {
  test_capture.levels.push_back(level);
  test_capture.messages.emplace_back(message != nullptr ? message : "");
}

auto context_test_callback(LogLevel level, const char* message, void* user_data)
    -> void {
  auto* cap = static_cast<LogCapture*>(user_data);
  if (cap != nullptr) {
    cap->levels.push_back(level);
    cap->messages.emplace_back(message != nullptr ? message : "");
  }
}

[[gnu::format(printf, 2, 3)]] auto invoke_test_log_v(LogLevel level,
                                                     const char* format, ...)
    -> void {
  va_list args;
  va_start(args, format);
  Logger::log_message_v(level, format, args);
  va_end(args);
}

struct ScopedLoggerReset {
  LogLevel orig_level;
  bool orig_file_logging;

  ScopedLoggerReset()
      : orig_level(Logger::get_verbosity()),
        orig_file_logging(Logger::is_file_logging_enabled()) {
    Logger::destroy();
    Logger::enable_file_logging(false);
    test_capture.clear();
  }

  ~ScopedLoggerReset() {
    Logger::destroy();
    Logger::set_verbosity(orig_level);
    Logger::enable_file_logging(orig_file_logging);
    test_capture.clear();
  }

  ScopedLoggerReset(const ScopedLoggerReset&) = delete;
  auto operator=(const ScopedLoggerReset&) -> ScopedLoggerReset& = delete;
  ScopedLoggerReset(ScopedLoggerReset&&) = delete;
  auto operator=(ScopedLoggerReset&&) -> ScopedLoggerReset& = delete;
};

}  // namespace

// LOG-01: LogLevel enum values and string conversion
TEST_CASE("Logger: [LOG-01] Enum Values and String Conversion") {
  static_assert(sizeof(LogLevel) == 1, "LogLevel must be 1 byte");

  CHECK(static_cast<uint8_t>(LogLevel::silent) == 0);
  CHECK(static_cast<uint8_t>(LogLevel::error) == 1);
  CHECK(static_cast<uint8_t>(LogLevel::warning) == 2);
  CHECK(static_cast<uint8_t>(LogLevel::info) == 3);
  CHECK(static_cast<uint8_t>(LogLevel::perf) == 4);
  CHECK(static_cast<uint8_t>(LogLevel::debug) == 5);

  CHECK(std::strcmp(Logger::log_level_to_string(LogLevel::silent), "SILENT") ==
        0);
  CHECK(std::strcmp(Logger::log_level_to_string(LogLevel::error), "ERROR") ==
        0);
  CHECK(std::strcmp(Logger::log_level_to_string(LogLevel::warning), "WARN") ==
        0);
  CHECK(std::strcmp(Logger::log_level_to_string(LogLevel::info), "INFO") == 0);
  CHECK(std::strcmp(Logger::log_level_to_string(LogLevel::perf), "PERF") == 0);
  CHECK(std::strcmp(Logger::log_level_to_string(LogLevel::debug), "DEBUG") ==
        0);
  CHECK(std::strcmp(Logger::log_level_to_string(static_cast<LogLevel>(99)),
                    "UNKNOWN") == 0);
}

// LOG-02: Verbosity Filtering
TEST_CASE("Logger: [LOG-02] Verbosity Filtering") {
  ScopedLoggerReset guard;
  Logger::set_callback(global_test_callback);

  // Set to warning: error and warning should pass, info/perf/debug dropped
  Logger::set_verbosity(LogLevel::warning);
  CHECK(Logger::get_verbosity() == LogLevel::warning);

  Logger::error("error msg\n");
  Logger::warning("warn msg\n");
  Logger::info("info msg\n");
  Logger::perf("perf msg\n");
  Logger::debug("debug msg\n");

  REQUIRE(test_capture.messages.size() == 2);
  CHECK(test_capture.levels[0] == LogLevel::error);
  CHECK(test_capture.messages[0] == "error msg\n");
  CHECK(test_capture.levels[1] == LogLevel::warning);
  CHECK(test_capture.messages[1] == "warn msg\n");

  // Set to silent: everything dropped
  test_capture.clear();
  Logger::set_verbosity(LogLevel::silent);
  Logger::error("another error\n");
  CHECK(test_capture.messages.empty());
}

// LOG-03: Format string argument expansion
TEST_CASE("Logger: [LOG-03] Format String Argument Expansion") {
  ScopedLoggerReset guard;
  Logger::set_callback(global_test_callback);
  Logger::set_verbosity(LogLevel::debug);

  Logger::info("Test %d %s 0x%04X\n", 42, "hello", 0xC000);
  REQUIRE(test_capture.messages.size() == 1);
  CHECK(test_capture.messages[0] == "Test 42 hello 0xC000\n");
}

// LOG-04: Large message heap reallocation (>1024 bytes)
TEST_CASE("Logger: [LOG-04] Large Buffer Heap Reallocation") {
  ScopedLoggerReset guard;
  Logger::set_callback(global_test_callback);
  Logger::set_verbosity(LogLevel::info);

  // Create a 2000-character test pattern
  std::string large_payload(2000, 'X');
  Logger::info("Header: %s\n", large_payload.c_str());

  REQUIRE(test_capture.messages.size() == 1);
  CHECK(test_capture.messages[0].size() ==
        2000 + 8 + 1);  // "Header: " + 2000 + "\n"
  CHECK(test_capture.messages[0].substr(0, 8) == "Header: ");
  CHECK(test_capture.messages[0].back() == '\n');
}

// LOG-05: Contextual callback with user_data pointer
TEST_CASE("Logger: [LOG-05] Contextual Callback Dispatch") {
  ScopedLoggerReset guard;
  LogCapture local_capture;
  Logger::set_callback_with_context(context_test_callback, &local_capture);
  Logger::set_verbosity(LogLevel::info);

  Logger::info("contextual test\n");
  REQUIRE(local_capture.messages.size() == 1);
  CHECK(local_capture.messages[0] == "contextual test\n");
}

// LOG-06: Reentrancy & Deadlock Prevention
TEST_CASE("Logger: [LOG-06] Reentrancy & Deadlock Safety") {
  ScopedLoggerReset guard;
  static int recursion_depth = 0;
  recursion_depth = 0;

  Logger::set_callback([](LogLevel, const char*) -> void {
    if (recursion_depth < 3) {
      ++recursion_depth;
      Logger::info("recursive call %d\n", recursion_depth);
    }
  });
  Logger::set_verbosity(LogLevel::info);

  // This would deadlock with a non-recursive lock held across callback
  // invocation
  Logger::info("initial trigger\n");
  CHECK(recursion_depth == 3);
}

// LOG-07: Custom log file output & ISO timestamp formatting
TEST_CASE("Logger: [LOG-07] File Logging Output and Timestamps") {
  ScopedLoggerReset guard;
  TestFixtures::ScopedTempFile tmp_log(".log");

  Logger::set_log_path(tmp_log.c_str());
  Logger::enable_file_logging(true);
  Logger::set_verbosity(LogLevel::debug);
  Logger::initialize();

  Logger::error("Disk error occurred\n");
  Logger::warning("Memory high\n");
  Logger::destroy();

  // Read back the temporary log file
  FilePtr f(std::fopen(tmp_log.c_str(), "rb"), std::fclose);
  REQUIRE(f != nullptr);

  std::vector<char> content(4096, '\0');
  size_t bytes = std::fread(content.data(), 1, content.size() - 1, f.get());
  std::string log_text(content.data(), bytes);

  // Check header
  CHECK(log_text.find("*** Logging started:") != std::string::npos);
  // Check formatted severity and messages
  CHECK(log_text.find("[ERROR] Disk error occurred\n") != std::string::npos);
  CHECK(log_text.find("[WARN ] Memory high\n") != std::string::npos);
  // Check footer
  CHECK(log_text.find("*** Logging ended") != std::string::npos);
}

// LOG-08: File logging disable toggle
TEST_CASE("Logger: [LOG-08] File Logging Disabled Toggle") {
  ScopedLoggerReset guard;
  TestFixtures::ScopedTempFile tmp_log(".log");

  Logger::set_log_path(tmp_log.c_str());
  Logger::enable_file_logging(false);
  CHECK_FALSE(Logger::is_file_logging_enabled());
  Logger::initialize();

  Logger::error("Should not appear in file\n");
  Logger::destroy();

  // The file should be empty because file logging was disabled
  FilePtr f(std::fopen(tmp_log.c_str(), "rb"), std::fclose);
  REQUIRE(f != nullptr);
  char buf[64] = {};
  size_t bytes = std::fread(buf, 1, sizeof(buf), f.get());
  CHECK(bytes == 0);
}

// LOG-09: Variadic list dispatch, null safety, and silent suppression
TEST_CASE("Logger: [LOG-09] Variadic List Dispatch and Edge Cases") {
  ScopedLoggerReset guard;
  Logger::set_callback(global_test_callback);
  Logger::set_verbosity(LogLevel::debug);

  // Null format string should be safely ignored
  Logger::error(nullptr);
  invoke_test_log_v(LogLevel::info, nullptr);
  CHECK(test_capture.messages.empty());

  // Silent log level should never emit messages
  invoke_test_log_v(LogLevel::silent, "silent message\n");
  CHECK(test_capture.messages.empty());

  // Direct log_message_v invocation
  invoke_test_log_v(LogLevel::info, "log_v %s %d\n", "test", 123);
  REQUIRE(test_capture.messages.size() == 1);
  CHECK(test_capture.levels[0] == LogLevel::info);
  CHECK(test_capture.messages[0] == "log_v test 123\n");
}
