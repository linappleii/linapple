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
#include <vector>

#include "HeadlessHarness.h"
#include "apple2/Memory.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/PrinterFrontend.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

using TestFixtures::ScopedEnvVar;
using TestFixtures::ScopedLogCapture;
using TestFixtures::ScopedTempDir;
using TestFixtures::ScopedTempFile;
using TestFixtures::ScopedTestConfig;

// A machine with the printer in slot 1 and its output sent to the given
// file, so nothing a case prints can land in the working directory.
auto printer_in_slot_1(
    const std::string& filename,
    const std::vector<ScopedTestConfig::Entry>& extras = {})
    -> ScopedTestConfig::Description {
  ScopedTestConfig::Description description;
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
  const auto address = static_cast<uint16_t>(0xC080 + (slot * 0x10));
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

}  // namespace

TEST_CASE(
    "Printer Frontend: the default sink strips bit 7, so C8 C9 8D 8A prints "
    "as 48 49 0D 0A") {
  ScopedTempFile file(".txt");
  ScopedTestConfig config(printer_in_slot_1(file.path()));
  {
    HeadlessHarness harness(config);
    for (uint8_t byte : {0xC8, 0xC9, 0x8D, 0x8A}) {
      strobe(1, byte);
    }
  }
  CHECK(file_bytes_hex(file.path()) == "48 49 0D 0A");
}

TEST_CASE(
    "Printer Frontend: the 8-bit key passes every byte through unchanged") {
  ScopedTempFile file(".txt");
  ScopedTestConfig config(printer_in_slot_1(
      file.path(), {{"Configuration", "Printer 8-bit output", "1"}}));
  {
    HeadlessHarness harness(config);
    for (uint8_t byte : {0xC8, 0xC9, 0x8D, 0x8A}) {
      strobe(1, byte);
    }
  }
  CHECK(file_bytes_hex(file.path()) == "C8 C9 8D 8A");
}

TEST_CASE("Printer Frontend: a second run appends to the file by default") {
  ScopedTempFile file(".txt");
  ScopedTestConfig config(printer_in_slot_1(file.path()));
  {
    HeadlessHarness first_run(config);
    strobe(1, 0xC1);
    strobe(1, 0x8D);
  }
  CHECK(file_bytes_hex(file.path()) == "41 0D");
  {
    HeadlessHarness second_run(config);
    strobe(1, 0xC2);
    strobe(1, 0x8D);
  }
  CHECK(file_bytes_hex(file.path()) == "41 0D 42 0D");
}

TEST_CASE(
    "Printer Frontend: with append off a run starts the file over once, at "
    "its first byte") {
  ScopedTempFile file(".txt");
  ScopedTestConfig config(printer_in_slot_1(
      file.path(), {{"Configuration", "Append to printer file", "0"}}));
  {
    HeadlessHarness first_run(config);
    strobe(1, 0xC1);
    strobe(1, 0x8D);
  }
  CHECK(file_bytes_hex(file.path()) == "41 0D");
  {
    HeadlessHarness second_run(config);
    for (uint8_t byte : {0xC2, 0x8D, 0xC3, 0x8D}) {
      strobe(1, byte);
    }
  }
  CHECK(file_bytes_hex(file.path()) == "42 0D 43 0D");
}

TEST_CASE(
    "Printer Frontend: a carriage return flushes the line before the sink "
    "closes") {
  ScopedTempFile file(".txt");
  ScopedTestConfig config(printer_in_slot_1(file.path()));
  HeadlessHarness harness(config);
  for (uint8_t byte : {0xC8, 0xC9, 0x8D}) {
    strobe(1, byte);
  }
  CHECK(file_bytes_hex(file.path()) == "48 49 0D");
}

TEST_CASE(
    "Printer Frontend: a file that cannot be opened switches the printer off "
    "with one log line, and a frame switches it back on") {
  ScopedTempDir dir("linapple_printer_test_");
  const std::string spool = dir.path() + "/spool";
  const std::string path = spool + "/Printer.txt";
  ScopedTestConfig config(printer_in_slot_1(path));
  // The card's own wait line names the slot only, so counting the lines that
  // name the file leaves it out.
  ScopedLogCapture log;
  const auto naming_the_file = [&log, &path]() -> std::vector<std::string> {
    return log.lines_containing(path);
  };
  {
    HeadlessHarness harness(config);
    harness.boot();

    strobe(1, 0xC8);
    REQUIRE(naming_the_file().size() == 1);
    CHECK(naming_the_file().at(0).find("cannot open") != std::string::npos);
    CHECK(naming_the_file().at(0).find("slot 1 is off") != std::string::npos);
    CHECK_FALSE(file_exists(path));

    // The directory now exists, but readiness is a state query: a thousand
    // strobes, each of which polls the sink, must neither open the file nor
    // say anything.
    REQUIRE(mkdir(spool.c_str(), 0755) == 0);
    for (int poll = 0; poll < 1000; ++poll) {
      strobe(1, 0xC9);
    }
    CHECK(naming_the_file().size() == 1);
    CHECK_FALSE(file_exists(path));

    harness.run_frames(1);
    REQUIRE(naming_the_file().size() == 2);
    CHECK(naming_the_file().at(1).find("on again") != std::string::npos);
    CHECK(file_exists(path));

    strobe(1, 0xCA);
    strobe(1, 0x8D);
  }
  CHECK(naming_the_file().size() == 2);
  CHECK(file_bytes_hex(path) == "4A 0D");
}

TEST_CASE(
    "Printer Frontend: a second printer card writes its own file, named with "
    "its slot") {
  ScopedTempDir dir("linapple_printer_test_");
  const std::string path = dir.path() + "/Printer.txt";
  ScopedTestConfig::Description description = printer_in_slot_1(path);
  description.slots[1] = "Parallel Printer";
  ScopedTestConfig config(description);
  {
    HeadlessHarness harness(config);
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
  ScopedTestConfig config(printer_in_slot_1("Printer.txt"));
  std::string path;
  {
    HeadlessHarness harness(config);
    path = std::string(system_state.save_state_dir.data()) + "/Printer.txt";
    CHECK(printer_frontend_output_path(1) == path);
    strobe(1, 0xC8);
    strobe(1, 0x8D);
  }
  CHECK(file_bytes_hex(path) == "48 0D");
}

TEST_CASE(
    "Printer Frontend: a leading ~/ in the filename is the home directory") {
  ScopedTempDir home("linapple_printer_test_");
  ScopedEnvVar home_var("HOME", home.path());
  ScopedTestConfig config(printer_in_slot_1("~/Printer.txt"));
  const std::string path = home.path() + "/Printer.txt";
  {
    HeadlessHarness harness(config);
    CHECK(printer_frontend_output_path(1) == path);
    strobe(1, 0xC8);
    strobe(1, 0x8D);
  }
  CHECK(file_bytes_hex(path) == "48 0D");
}
