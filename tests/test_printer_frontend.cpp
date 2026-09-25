// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "HeadlessHarness.h"
#include "apple2/Memory.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "doctest.h"
#include "frontends/common/PrinterFrontend.h"
#include "test_fixtures.h"

namespace {

using TestFixtures::ScopedEnvVar_t;
using TestFixtures::ScopedTempDir_t;
using TestFixtures::ScopedTempFile_t;
using TestFixtures::ScopedTestConfig_t;

// A machine with the printer in slot 1 and its output sent to the given
// file, so nothing a case prints can land in the working directory.
auto printer_in_slot_1(
    const std::string& filename,
    const std::vector<ScopedTestConfig_t::Entry_t>& extras = {})
    -> ScopedTestConfig_t::Description_t {
  ScopedTestConfig_t::Description_t description;
  description.slots[0] = "Parallel Printer";
  description.extras.push_back(
      {"Configuration", "Parallel Printer Filename", filename});
  for (const auto& entry : extras) {
    description.extras.push_back(entry);
  }
  return description;
}

// Every access to the card's sixteen addresses strobes the byte on the bus to
// the sink; a write from the test stands in for the firmware's STA.
auto strobe(int slot, uint8_t byte) -> void {
  const auto address = static_cast<uint16_t>(0xC080 + slot * 0x10);
  io_map_dispatch(0, address, 1, byte, 0);
}

auto file_bytes_hex(const std::string& path) -> std::string {
  std::ifstream in(path, std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
  std::string out;
  std::array<char, 4> cell{};
  for (uint8_t byte : bytes) {
    std::snprintf(cell.data(), cell.size(), "%02X", byte);
    if (!out.empty()) {
      out += ' ';
    }
    out += cell.data();
  }
  return out;
}

auto file_exists(const std::string& path) -> bool {
  return access(path.c_str(), F_OK) == 0;
}

std::string g_watched_file;
std::vector<std::string> g_lines_naming_the_file;

auto capture_log(LogLevel_t level, const char* message) -> void {
  (void)level;
  if (message != nullptr &&
      std::strstr(message, g_watched_file.c_str()) != nullptr) {
    g_lines_naming_the_file.emplace_back(message);
  }
}

// Collects the log lines that name the printer's file; the card's own wait
// line names the slot only, so it is not counted here.
struct ScopedLogCapture_t {
  explicit ScopedLogCapture_t(std::string path) {
    g_watched_file = std::move(path);
    g_lines_naming_the_file.clear();
    Logger::set_callback(capture_log);
  }
  ~ScopedLogCapture_t() { Logger::set_callback(nullptr); }
  ScopedLogCapture_t(const ScopedLogCapture_t&) = delete;
  auto operator=(const ScopedLogCapture_t&) -> ScopedLogCapture_t& = delete;
  ScopedLogCapture_t(ScopedLogCapture_t&&) = delete;
  auto operator=(ScopedLogCapture_t&&) -> ScopedLogCapture_t& = delete;

  static auto lines() -> const std::vector<std::string>& {
    return g_lines_naming_the_file;
  }
};

}  // namespace

TEST_CASE(
    "Printer Frontend: the default sink strips bit 7, so C8 C9 8D 8A prints "
    "as 48 49 0D 0A") {
  ScopedTempFile_t file(".txt");
  ScopedTestConfig_t config(printer_in_slot_1(file.path()));
  {
    HeadlessHarness_t harness(config);
    for (uint8_t byte : {0xC8, 0xC9, 0x8D, 0x8A}) {
      strobe(1, byte);
    }
  }
  CHECK(file_bytes_hex(file.path()) == "48 49 0D 0A");
}

TEST_CASE(
    "Printer Frontend: the 8-bit key passes every byte through unchanged") {
  ScopedTempFile_t file(".txt");
  ScopedTestConfig_t config(printer_in_slot_1(
      file.path(), {{"Configuration", "Printer 8-bit output", "1"}}));
  {
    HeadlessHarness_t harness(config);
    for (uint8_t byte : {0xC8, 0xC9, 0x8D, 0x8A}) {
      strobe(1, byte);
    }
  }
  CHECK(file_bytes_hex(file.path()) == "C8 C9 8D 8A");
}

TEST_CASE("Printer Frontend: a second run appends to the file by default") {
  ScopedTempFile_t file(".txt");
  ScopedTestConfig_t config(printer_in_slot_1(file.path()));
  {
    HeadlessHarness_t first_run(config);
    strobe(1, 0xC1);
    strobe(1, 0x8D);
  }
  CHECK(file_bytes_hex(file.path()) == "41 0D");
  {
    HeadlessHarness_t second_run(config);
    strobe(1, 0xC2);
    strobe(1, 0x8D);
  }
  CHECK(file_bytes_hex(file.path()) == "41 0D 42 0D");
}

TEST_CASE(
    "Printer Frontend: with append off a run starts the file over once, at "
    "its first byte") {
  ScopedTempFile_t file(".txt");
  ScopedTestConfig_t config(printer_in_slot_1(
      file.path(), {{"Configuration", "Append to printer file", "0"}}));
  {
    HeadlessHarness_t first_run(config);
    strobe(1, 0xC1);
    strobe(1, 0x8D);
  }
  CHECK(file_bytes_hex(file.path()) == "41 0D");
  {
    HeadlessHarness_t second_run(config);
    for (uint8_t byte : {0xC2, 0x8D, 0xC3, 0x8D}) {
      strobe(1, byte);
    }
  }
  CHECK(file_bytes_hex(file.path()) == "42 0D 43 0D");
}

TEST_CASE(
    "Printer Frontend: a carriage return flushes the line before the sink "
    "closes") {
  ScopedTempFile_t file(".txt");
  ScopedTestConfig_t config(printer_in_slot_1(file.path()));
  HeadlessHarness_t harness(config);
  for (uint8_t byte : {0xC8, 0xC9, 0x8D}) {
    strobe(1, byte);
  }
  CHECK(file_bytes_hex(file.path()) == "48 49 0D");
}

TEST_CASE(
    "Printer Frontend: a file that cannot be opened switches the printer off "
    "with one log line, and a frame switches it back on") {
  ScopedTempDir_t dir("linapple_printer_test_");
  const std::string spool = dir.path() + "/spool";
  const std::string path = spool + "/Printer.txt";
  ScopedTestConfig_t config(printer_in_slot_1(path));
  ScopedLogCapture_t log(path);
  {
    HeadlessHarness_t harness(config);
    harness.boot();

    strobe(1, 0xC8);
    REQUIRE(ScopedLogCapture_t::lines().size() == 1);
    CHECK(ScopedLogCapture_t::lines().at(0).find("cannot open") !=
          std::string::npos);
    CHECK(ScopedLogCapture_t::lines().at(0).find("slot 1 is off") !=
          std::string::npos);
    CHECK_FALSE(file_exists(path));

    // The directory now exists, but readiness is a state query: a thousand
    // strobes, each of which polls the sink, must neither open the file nor
    // say anything.
    REQUIRE(mkdir(spool.c_str(), 0755) == 0);
    for (int poll = 0; poll < 1000; ++poll) {
      strobe(1, 0xC9);
    }
    CHECK(ScopedLogCapture_t::lines().size() == 1);
    CHECK_FALSE(file_exists(path));

    harness.run_frames(1);
    REQUIRE(ScopedLogCapture_t::lines().size() == 2);
    CHECK(ScopedLogCapture_t::lines().at(1).find("on again") !=
          std::string::npos);
    CHECK(file_exists(path));

    strobe(1, 0xCA);
    strobe(1, 0x8D);
  }
  CHECK(ScopedLogCapture_t::lines().size() == 2);
  CHECK(file_bytes_hex(path) == "4A 0D");
}

TEST_CASE(
    "Printer Frontend: a second printer card writes its own file, named with "
    "its slot") {
  ScopedTempDir_t dir("linapple_printer_test_");
  const std::string path = dir.path() + "/Printer.txt";
  ScopedTestConfig_t::Description_t description = printer_in_slot_1(path);
  description.slots[1] = "Parallel Printer";
  ScopedTestConfig_t config(description);
  {
    HeadlessHarness_t harness(config);
    CHECK(printer_frontend_output_path(1) == path);
    CHECK(printer_frontend_output_path(2) == dir.path() + "/Printer-slot2.txt");
    strobe(2, 0xC2);
    strobe(2, 0x8D);
    strobe(1, 0xC1);
    strobe(1, 0x8D);
  }
  CHECK(file_bytes_hex(path) == "41 0D");
  CHECK(file_bytes_hex(dir.path() + "/Printer-slot2.txt") == "42 0D");
}

TEST_CASE(
    "Printer Frontend: a relative filename lives in the save-state "
    "directory") {
  ScopedTestConfig_t config(printer_in_slot_1("Printer.txt"));
  std::string path;
  {
    HeadlessHarness_t harness(config);
    path = std::string(g_state.save_state_dir.data()) + "/Printer.txt";
    CHECK(printer_frontend_output_path(1) == path);
    strobe(1, 0xC8);
    strobe(1, 0x8D);
  }
  CHECK(file_bytes_hex(path) == "48 0D");
}

TEST_CASE(
    "Printer Frontend: a leading ~/ in the filename is the home directory") {
  ScopedTempDir_t home("linapple_printer_test_");
  ScopedEnvVar_t home_var("HOME", home.path());
  ScopedTestConfig_t config(printer_in_slot_1("~/Printer.txt"));
  const std::string path = home.path() + "/Printer.txt";
  {
    HeadlessHarness_t harness(config);
    CHECK(printer_frontend_output_path(1) == path);
    strobe(1, 0xC8);
    strobe(1, 0x8D);
  }
  CHECK(file_bytes_hex(path) == "48 0D");
}
