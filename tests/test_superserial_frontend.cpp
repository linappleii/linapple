// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <cerrno>
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
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "core/Log.h"
#include "doctest.h"
#include "frontends/common/SuperSerialFrontend.h"
#include "test_fixtures.h"

namespace {

using TestFixtures::ScopedTempDir_t;
using TestFixtures::ScopedTempFile_t;
using TestFixtures::ScopedTestConfig_t;

constexpr int card_slot = 2;
constexpr int second_card_slot = 4;
constexpr int peer_wait_ms = 2000;

// A machine with the card in slot 2 and the given port, plus any further
// [Configuration] keys a case needs.
auto serial_in_slot_2(
    const std::string& port,
    const std::vector<ScopedTestConfig_t::Entry_t>& extras = {})
    -> ScopedTestConfig_t::Description_t {
  ScopedTestConfig_t::Description_t description;
  description.slots[card_slot - 1] = "Super Serial Card";
  description.extras.push_back({"Configuration", "Serial Port", port});
  for (const auto& entry : extras) {
    description.extras.push_back(entry);
  }
  return description;
}

auto sink() -> const ByteSink_t& { return super_serial_frontend_sink(); }

// The card composes $C0n1 and $C0n2 from its switch image on every read, so
// the register says what the frontend sent.
auto read_switch_register(int slot, int offset) -> uint8_t {
  const auto address = static_cast<uint16_t>(0xC080 + (slot * 0x10) + offset);
  return io_map_dispatch(0, address, 0, 0, 0);
}

struct LogLines_t {
  std::vector<std::string> lines;
};

auto collect_log_line(LogLevel_t level, const char* message, void* user_data)
    -> void {
  (void)level;
  auto* lines = static_cast<LogLines_t*>(user_data);
  if (lines != nullptr && message != nullptr) {
    lines->lines.emplace_back(message);
  }
}

// Captures every log line while in scope and puts the logger back as found.
class ScopedLogCapture_t {
 public:
  ScopedLogCapture_t() : verbosity_(Logger::get_verbosity()) {
    Logger::set_verbosity(LogLevel_t::info);
    Logger::set_callback_with_context(collect_log_line, &lines_);
  }
  ~ScopedLogCapture_t() {
    Logger::set_callback_with_context(nullptr, nullptr);
    Logger::set_verbosity(verbosity_);
  }
  ScopedLogCapture_t(const ScopedLogCapture_t&) = delete;
  auto operator=(const ScopedLogCapture_t&) -> ScopedLogCapture_t& = delete;
  ScopedLogCapture_t(ScopedLogCapture_t&&) = delete;
  auto operator=(ScopedLogCapture_t&&) -> ScopedLogCapture_t& = delete;

  auto lines() const -> const std::vector<std::string>& { return lines_.lines; }
  auto count_containing(const std::string& needle) const -> size_t {
    size_t count = 0;
    for (const std::string& line : lines_.lines) {
      if (line.find(needle) != std::string::npos) {
        ++count;
      }
    }
    return count;
  }
  auto joined() const -> std::string {
    std::string out;
    for (const std::string& line : lines_.lines) {
      out += line;
    }
    return out;
  }

 private:
  LogLevel_t verbosity_;
  LogLines_t lines_;
};

// The far end of the pseudo-terminal, as a terminal program would hold it.
class Peer_t {
 public:
  explicit Peer_t(const std::string& path)
      : fd_(open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK)),
        error_(errno) {}
  ~Peer_t() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }
  Peer_t(const Peer_t&) = delete;
  auto operator=(const Peer_t&) -> Peer_t& = delete;
  Peer_t(Peer_t&&) = delete;
  auto operator=(Peer_t&&) -> Peer_t& = delete;

  auto fd() const -> int { return fd_; }
  auto error_text() const -> const char* { return std::strerror(error_); }

  auto write_byte(uint8_t byte) const -> bool {
    return write(fd_, &byte, 1) == 1;
  }

  // Returns the next byte, or -1 if none arrives within the wait; the
  // kernel moves a byte across a pseudo-terminal at once, so the wait only
  // turns a hang into a failure.
  auto read_byte() const -> int {
    pollfd request{};
    request.fd = fd_;
    request.events = POLLIN;
    if (poll(&request, 1, peer_wait_ms) <= 0) {
      return -1;
    }
    uint8_t byte = 0;
    if (read(fd_, &byte, 1) != 1) {
      return -1;
    }
    return byte;
  }

  auto has_byte() const -> bool {
    pollfd request{};
    request.fd = fd_;
    request.events = POLLIN;
    return poll(&request, 1, 0) > 0 && (request.revents & POLLIN) != 0;
  }

 private:
  int fd_;
  int error_;
};

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

auto write_file(const std::string& path, const std::string& text) -> void {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

// A bench card that opens the kind of token it is told to and keeps the host
// it was handed, so a case can drive the frontend's sink through the same
// members a real card uses.
struct TokenProbe_t {
  HostInterface_t* host = nullptr;
  void* token = nullptr;
  int slot = 0;
};

TokenProbe_t g_serial_probe;
TokenProbe_t g_printer_probe;

auto serial_probe_init(int slot, HostInterface_t* host) -> void* {
  g_serial_probe = TokenProbe_t();
  g_serial_probe.host = host;
  g_serial_probe.slot = slot;
  g_serial_probe.token =
      host->SinkOpen(&g_serial_probe, slot, peripheral_sink_serial);
  return &g_serial_probe;
}

auto printer_probe_init(int slot, HostInterface_t* host) -> void* {
  g_printer_probe = TokenProbe_t();
  g_printer_probe.host = host;
  g_printer_probe.slot = slot;
  g_printer_probe.token =
      host->SinkOpen(&g_printer_probe, slot, peripheral_sink_printer);
  return &g_printer_probe;
}

auto probe_shutdown(void* instance) -> void {
  auto* probe = static_cast<TokenProbe_t*>(instance);
  probe->host->SinkClose(probe->token);
}

Peripheral_t g_serial_probe_card = {LINAPPLE_ABI_VERSION,
                                    "test.serial_token_probe",
                                    "SerialTokenProbe",
                                    "Opens a serial token at init",
                                    "LinApple Contributors",
                                    "1.0.0",
                                    PERIPHERAL_MASK_EXPANSION,
                                    -1,
                                    serial_probe_init,
                                    nullptr,
                                    probe_shutdown,
                                    nullptr,
                                    nullptr,
                                    nullptr,
                                    nullptr,
                                    nullptr,
                                    nullptr};

Peripheral_t g_printer_probe_card = {LINAPPLE_ABI_VERSION,
                                     "test.printer_token_probe",
                                     "PrinterTokenProbe",
                                     "Opens a printer token at init",
                                     "LinApple Contributors",
                                     "1.0.0",
                                     PERIPHERAL_MASK_EXPANSION,
                                     -1,
                                     printer_probe_init,
                                     nullptr,
                                     probe_shutdown,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr};

constexpr int serial_probe_slot = 3;
constexpr int printer_probe_slot = 5;

}  // namespace

TEST_CASE(
    "Serial Frontend: the frontend's sink is installed, and a serial token "
    "opened through it reads no byte and reports no lines") {
  ScopedTestConfig_t config(ScopedTestConfig_t::enhanced_2e_only());
  HeadlessHarness_t harness(config);
  REQUIRE(peripheral_register(&g_serial_probe_card, serial_probe_slot) == 0);
  REQUIRE(peripheral_register(&g_printer_probe_card, printer_probe_slot) == 0);
  REQUIRE(g_serial_probe.token != nullptr);
  REQUIRE(g_printer_probe.token != nullptr);
  HostInterface_t* host = g_serial_probe.host;

  // The printer side of the installed sink is live: a printer slot is ready
  // the moment it is opened, so the dispatcher is in and forwarding by kind.
  CHECK(host->SinkReady(g_printer_probe.token));

  // The serial side has no device behind it.
  uint8_t byte = 0x5A;
  CHECK(host->SinkRead(g_serial_probe.token, &byte) == false);
  CHECK(byte == 0x5A);
  uint8_t lines = 0xA5;
  CHECK(host->SinkGetLines(g_serial_probe.token, &lines) == false);
  CHECK(lines == 0xA5);
  CHECK(host->SinkReady(g_serial_probe.token) == false);
  host->SinkWrite(g_serial_probe.token, 0xC8);
  const PeripheralSerialLine_t line = {9600, 8, 0, 2, 1, 1, 0, {0, 0}};
  host->SinkSetLine(g_serial_probe.token, &line);
  CHECK(host->SinkRead(g_serial_probe.token, &byte) == false);
  CHECK(byte == 0x5A);

  peripheral_unregister(serial_probe_slot);
  peripheral_unregister(printer_probe_slot);
}

TEST_CASE(
    "Serial Frontend: Serial Port = pty creates a pseudo-terminal whose peer "
    "receives what the card writes, and a byte the peer writes reaches read "
    "within one tick") {
  ScopedTestConfig_t config(serial_in_slot_2("pty"));
  ScopedLogCapture_t log;
  HeadlessHarness_t harness(config);
  REQUIRE(super_serial_frontend_primary_slot() == card_slot);
  const std::string path = super_serial_frontend_device_path(card_slot);
  REQUIRE_MESSAGE(!path.empty(),
                  "the pseudo-terminal could not be created: " << log.joined());
  CHECK(path.rfind("/dev/pts/", 0) == 0);
  Peer_t peer(path);
  REQUIRE_MESSAGE(peer.fd() >= 0,
                  "cannot open " << path << ": " << peer.error_text());

  CHECK(sink().ready(nullptr, card_slot));
  uint8_t lines = 0;
  CHECK(sink().get_lines(nullptr, card_slot, &lines));
  CHECK(lines == 0x07);

  sink().write(nullptr, card_slot, 0xC8);
  CHECK(peer.read_byte() == 0xC8);

  REQUIRE(peer.write_byte(0x41));
  uint8_t byte = 0;
  sink().tick(nullptr);
  CHECK(sink().read(nullptr, card_slot, &byte));
  CHECK(byte == 0x41);
  CHECK(sink().read(nullptr, card_slot, &byte) == false);

  // Raw mode is the master's, so the peer gets the card's bytes unechoed and
  // untranslated: 0x8D is not turned into CR LF and never comes back.
  sink().write(nullptr, card_slot, 0x8D);
  CHECK(peer.read_byte() == 0x8D);
  CHECK(peer.has_byte() == false);
  sink().tick(nullptr);
  CHECK(sink().read(nullptr, card_slot, &byte) == false);
}

TEST_CASE(
    "Serial Frontend: a peer that writes and closes is followed by a second "
    "peer on the same path, which still crosses a byte each way, with no log "
    "line for the gap") {
  ScopedTestConfig_t config(serial_in_slot_2("pty"));
  HeadlessHarness_t harness(config);
  const std::string path = super_serial_frontend_device_path(card_slot);
  REQUIRE(!path.empty());
  ScopedLogCapture_t log;

  uint8_t byte = 0;
  {
    Peer_t first(path);
    REQUIRE_MESSAGE(first.fd() >= 0, first.error_text());
    REQUIRE(first.write_byte(0x31));
    sink().tick(nullptr);
    CHECK(sink().read(nullptr, card_slot, &byte));
    CHECK(byte == 0x31);
  }

  // No peer: the master reports hang-up and EIO, which is the line idle.
  for (int i = 0; i < 5; ++i) {
    sink().tick(nullptr);
  }
  CHECK(sink().read(nullptr, card_slot, &byte) == false);
  CHECK(sink().ready(nullptr, card_slot));
  uint8_t lines = 0;
  CHECK(sink().get_lines(nullptr, card_slot, &lines));
  CHECK(lines == 0x07);

  {
    Peer_t second(path);
    REQUIRE_MESSAGE(second.fd() >= 0, second.error_text());
    sink().write(nullptr, card_slot, 0x33);
    CHECK(second.read_byte() == 0x33);
    REQUIRE(second.write_byte(0x34));
    sink().tick(nullptr);
    CHECK(sink().read(nullptr, card_slot, &byte));
    CHECK(byte == 0x34);
  }
  CHECK(log.lines().empty());
}

TEST_CASE(
    "Serial Frontend: Serial Port = loopback returns the byte the card "
    "writes to the same slot") {
  ScopedTestConfig_t config(serial_in_slot_2("loopback"));
  HeadlessHarness_t harness(config);
  REQUIRE(super_serial_frontend_primary_slot() == card_slot);
  CHECK(super_serial_frontend_device_path(card_slot).empty());
  CHECK(sink().ready(nullptr, card_slot));

  uint8_t byte = 0;
  CHECK(sink().read(nullptr, card_slot, &byte) == false);
  sink().write(nullptr, card_slot, 0xC8);
  sink().write(nullptr, card_slot, 0xC5);
  CHECK(sink().read(nullptr, card_slot, &byte));
  CHECK(byte == 0xC8);
  CHECK(sink().read(nullptr, card_slot, &byte));
  CHECK(byte == 0xC5);
  CHECK(sink().read(nullptr, card_slot, &byte) == false);
  uint8_t lines = 0;
  CHECK(sink().get_lines(nullptr, card_slot, &lines));
  CHECK(lines == 0x07);
}

TEST_CASE(
    "Serial Frontend: an empty Serial Port drops writes, reads nothing, "
    "reports no lines and is not ready") {
  ScopedTestConfig_t config(serial_in_slot_2(""));
  HeadlessHarness_t harness(config);
  REQUIRE(super_serial_frontend_primary_slot() == card_slot);
  CHECK(super_serial_frontend_device_path(card_slot).empty());

  sink().write(nullptr, card_slot, 0xC8);
  sink().tick(nullptr);
  uint8_t byte = 0x5A;
  CHECK(sink().read(nullptr, card_slot, &byte) == false);
  CHECK(byte == 0x5A);
  uint8_t lines = 0xA5;
  CHECK(sink().get_lines(nullptr, card_slot, &lines) == false);
  CHECK(lines == 0xA5);
  CHECK(sink().ready(nullptr, card_slot) == false);
}

TEST_CASE(
    "Serial Frontend: a path that cannot be opened is logged once, reads "
    "nothing, reports CTS deasserted, and is retried after sixty ticks, not "
    "before") {
  ScopedTempDir_t dir("linapple_serial_test_");
  const std::string path = dir.path() + "/line.txt";
  ScopedTestConfig_t config(serial_in_slot_2(path));
  ScopedLogCapture_t log;
  HeadlessHarness_t harness(config);
  REQUIRE(super_serial_frontend_primary_slot() == card_slot);
  CHECK(super_serial_frontend_device_path(card_slot) == path);
  CHECK(log.count_containing("cannot open") == 1);
  CHECK(log.count_containing(path) == 1);

  CHECK(sink().ready(nullptr, card_slot) == false);
  uint8_t byte = 0x5A;
  CHECK(sink().read(nullptr, card_slot, &byte) == false);
  uint8_t lines = 0;
  CHECK(sink().get_lines(nullptr, card_slot, &lines));
  CHECK(lines == 0x06);

  // The file exists from now on; only the sixtieth tick looks again.
  write_file(path, "");
  for (int tick = 1; tick < 60; ++tick) {
    sink().tick(nullptr);
    CHECK(sink().ready(nullptr, card_slot) == false);
  }
  CHECK(log.count_containing("cannot open") == 1);
  sink().tick(nullptr);
  CHECK(sink().ready(nullptr, card_slot));
  CHECK(sink().get_lines(nullptr, card_slot, &lines));
  CHECK(lines == 0x07);
  CHECK(log.count_containing("cannot open") == 1);
  CHECK(log.count_containing("opened") == 1);

  sink().write(nullptr, card_slot, 0xC8);
  CHECK(file_bytes_hex(path) == "C8");
}

TEST_CASE(
    "Serial Frontend: a regular file receives the written bytes after what "
    "it held and delivers none") {
  ScopedTempFile_t file(".txt");
  write_file(file.path(), "AB");
  ScopedTestConfig_t config(serial_in_slot_2(file.path()));
  ScopedLogCapture_t log;
  HeadlessHarness_t harness(config);
  REQUIRE(super_serial_frontend_primary_slot() == card_slot);
  CHECK(sink().ready(nullptr, card_slot));

  sink().write(nullptr, card_slot, 0xC8);
  sink().write(nullptr, card_slot, 0x8D);
  sink().tick(nullptr);
  uint8_t byte = 0x5A;
  CHECK(sink().read(nullptr, card_slot, &byte) == false);
  CHECK(byte == 0x5A);
  uint8_t lines = 0;
  CHECK(sink().get_lines(nullptr, card_slot, &lines));
  CHECK(lines == 0x07);
  CHECK(file_bytes_hex(file.path()) == "41 42 C8 8D");
  INFO(log.joined());
  CHECK(log.count_containing("serial port") == 0);
  CHECK(log.count_containing(file.path()) == 0);
}

TEST_CASE(
    "Serial Frontend: /dev/null is a character device that is not a tty, "
    "takes writes, delivers nothing and logs nothing") {
  ScopedTestConfig_t config(serial_in_slot_2("/dev/null"));
  ScopedLogCapture_t log;
  HeadlessHarness_t harness(config);
  REQUIRE(super_serial_frontend_primary_slot() == card_slot);
  CHECK(sink().ready(nullptr, card_slot));

  sink().write(nullptr, card_slot, 0xC8);
  for (int i = 0; i < 3; ++i) {
    sink().tick(nullptr);
  }
  uint8_t byte = 0x5A;
  CHECK(sink().read(nullptr, card_slot, &byte) == false);
  uint8_t lines = 0;
  CHECK(sink().get_lines(nullptr, card_slot, &lines));
  CHECK(lines == 0x07);
  const PeripheralSerialLine_t line = {9600, 8, 0, 2, 1, 1, 0, {0, 0}};
  sink().set_line(nullptr, card_slot, &line);
  INFO(log.joined());
  CHECK(log.count_containing("serial port") == 0);
  CHECK(log.count_containing("/dev/null") == 0);
}

TEST_CASE(
    "Serial Frontend: set_line at 9600 8N1 sets B9600, CS8, no PARENB and no "
    "CSTOPB on the pseudo-terminal, odd parity sets PARODD, 7E2 sets CSTOPB "
    "and B300 (a pseudo-terminal forces CS8), and baud 0 leaves the speed") {
  ScopedTestConfig_t config(serial_in_slot_2("pty"));
  HeadlessHarness_t harness(config);
  const std::string path = super_serial_frontend_device_path(card_slot);
  REQUIRE(!path.empty());
  Peer_t peer(path);
  REQUIRE_MESSAGE(peer.fd() >= 0, peer.error_text());

  PeripheralSerialLine_t line = {
      9600, 8, peripheral_serial_parity_none, 2, 1, 1, 0, {0, 0}};
  sink().set_line(nullptr, card_slot, &line);
  termios settings{};
  REQUIRE(tcgetattr(peer.fd(), &settings) == 0);
  CHECK(cfgetospeed(&settings) == B9600);
  CHECK(cfgetispeed(&settings) == B9600);
  CHECK((settings.c_cflag & CSIZE) == CS8);
  CHECK((settings.c_cflag & PARENB) == 0);
  CHECK((settings.c_cflag & CSTOPB) == 0);
  CHECK((settings.c_cflag & CLOCAL) != 0);
  CHECK((settings.c_cflag & CREAD) != 0);
  CHECK((settings.c_cflag & HUPCL) == 0);
  CHECK((settings.c_lflag & ECHO) == 0);
  CHECK((settings.c_lflag & ICANON) == 0);
  CHECK((settings.c_oflag & OPOST) == 0);

  // The kernel's pseudo-terminal driver forces CS8 and clears PARENB on
  // every change (a pseudo-terminal has no word length or parity), so of
  // the parity bits only PARODD can be read back here; the word length and
  // the parity enable are observable on a real port alone.
  line.parity = peripheral_serial_parity_odd;
  sink().set_line(nullptr, card_slot, &line);
  REQUIRE(tcgetattr(peer.fd(), &settings) == 0);
  CHECK((settings.c_cflag & PARODD) != 0);

  line.parity = peripheral_serial_parity_even;
  line.data_bits = 7;
  line.stop_half_bits = 4;
  line.baud = 300;
  sink().set_line(nullptr, card_slot, &line);
  REQUIRE(tcgetattr(peer.fd(), &settings) == 0);
  CHECK((settings.c_cflag & PARODD) == 0);
  CHECK((settings.c_cflag & CSTOPB) != 0);
  CHECK(cfgetospeed(&settings) == B300);

  line.baud = 0;
  sink().set_line(nullptr, card_slot, &line);
  REQUIRE(tcgetattr(peer.fd(), &settings) == 0);
  CHECK(cfgetospeed(&settings) == B300);
}

TEST_CASE(
    "Serial Frontend: a malformed switch row falls back to the default and "
    "is logged once, an empty row falls back silently, and a well-formed row "
    "in either case reaches the card") {
  SUBCASE("a malformed first row and an empty second row") {
    ScopedTestConfig_t config(serial_in_slot_2(
        "", {{"Configuration", "Serial Switches 1", "ON ON MAYBE"},
             {"Configuration", "Serial Switches 2", ""}}));
    ScopedLogCapture_t log;
    HeadlessHarness_t harness(config);
    CHECK(read_switch_register(card_slot, 1) == 0xEC);
    CHECK(read_switch_register(card_slot, 2) == 0x52);
    CHECK(log.count_containing("Serial Switches 1") == 1);
    CHECK(log.count_containing("Serial Switches 2") == 0);
  }
  SUBCASE("six tokens and eight tokens are both malformed") {
    ScopedTestConfig_t config(serial_in_slot_2(
        "",
        {{"Configuration", "Serial Switches 1", "ON ON ON ON ON ON"},
         {"Configuration", "Serial Switches 2", "ON ON ON ON ON ON ON ON"}}));
    ScopedLogCapture_t log;
    HeadlessHarness_t harness(config);
    CHECK(read_switch_register(card_slot, 1) == 0xEC);
    CHECK(read_switch_register(card_slot, 2) == 0x52);
    CHECK(log.count_containing("Serial Switches 1") == 1);
    CHECK(log.count_containing("Serial Switches 2") == 1);
  }
  SUBCASE("the manual's printer-mode rows, in any case and with commas") {
    ScopedTestConfig_t config(serial_in_slot_2(
        "", {{"Configuration", "Serial Switches 1", "off off off on off on on"},
             {"Configuration", "Serial Switches 2",
              "ON, ON, OFF, ON, OFF, OFF, OFF"}}));
    ScopedLogCapture_t log;
    HeadlessHarness_t harness(config);
    CHECK(read_switch_register(card_slot, 1) == 0xEE);
    CHECK(read_switch_register(card_slot, 2) == 0x5A);
    CHECK(log.count_containing("Serial Switches") == 0);
  }
}

TEST_CASE(
    "Serial Frontend: with two cards the lower slot is the primary, and the "
    "second reads no lines and drops writes") {
  ScopedTestConfig_t::Description_t description = serial_in_slot_2("loopback");
  description.slots[second_card_slot - 1] = "Super Serial Card";
  ScopedTestConfig_t config(description);
  HeadlessHarness_t harness(config);
  REQUIRE(super_serial_frontend_primary_slot() == card_slot);
  CHECK(super_serial_frontend_device_path(second_card_slot).empty());

  // Both cards took the switches.
  CHECK(read_switch_register(card_slot, 1) == 0xEC);
  CHECK(read_switch_register(second_card_slot, 1) == 0xEC);

  sink().write(nullptr, second_card_slot, 0xC8);
  uint8_t byte = 0x5A;
  CHECK(sink().read(nullptr, second_card_slot, &byte) == false);
  CHECK(sink().read(nullptr, card_slot, &byte) == false);
  CHECK(byte == 0x5A);
  uint8_t lines = 0xA5;
  CHECK(sink().get_lines(nullptr, second_card_slot, &lines) == false);
  CHECK(lines == 0xA5);
  CHECK(sink().ready(nullptr, second_card_slot) == false);

  sink().write(nullptr, card_slot, 0xC9);
  CHECK(sink().read(nullptr, card_slot, &byte));
  CHECK(byte == 0xC9);
}

TEST_CASE(
    "Serial Frontend: configure with no card in any slot sends nothing, has "
    "no primary and opens no device") {
  ScopedTestConfig_t::Description_t description =
      ScopedTestConfig_t::enhanced_2e_only();
  description.extras.push_back({"Configuration", "Serial Port", "loopback"});
  ScopedTestConfig_t config(description);
  ScopedLogCapture_t log;
  HeadlessHarness_t harness(config);
  CHECK(super_serial_frontend_primary_slot() == 0);
  for (int slot = 1; slot <= 7; ++slot) {
    CHECK(super_serial_frontend_device_path(slot).empty());
    CHECK(sink().ready(nullptr, slot) == false);
  }
  CHECK(log.count_containing("serial port") == 0);
  CHECK(log.count_containing("Super Serial Card") == 0);
}
