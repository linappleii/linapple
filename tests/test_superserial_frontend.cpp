// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <fcntl.h>
#include <sys/poll.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <vector>

#include "HeadlessHarness.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "doctest.h"
#include "frontends/common/SuperSerialFrontend.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

using TestFixtures::ScopedLogCapture;
using TestFixtures::ScopedTempDir;
using TestFixtures::ScopedTempFile;
using TestFixtures::ScopedTestConfig;

constexpr int card_slot = 2;
constexpr int second_card_slot = 4;
constexpr int peer_wait_ms = 2000;

auto serial_in_slot_2(
    const std::string& port,
    const std::vector<ScopedTestConfig::Entry>& extras = {})
    -> ScopedTestConfig::Description {
  ScopedTestConfig::Description description;
  description.slots[card_slot - 1] = "Super Serial Card";
  description.extras.push_back({"Configuration", "Serial Port", port});
  for (const auto& entry : extras) {
    description.extras.push_back(entry);
  }
  return description;
}

auto sink() -> const ByteSink& { return super_serial_frontend_sink(); }

auto read_switch_register(int slot, int offset) -> uint8_t {
  const auto address = static_cast<uint16_t>(0xC080 + (slot * 0x10) + offset);
  return io_map_dispatch(0, address, 0, 0, 0);
}

class Peer {
 public:
  explicit Peer(const std::string& path)
      : fd_(open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK)),
        error_(errno) {}
  ~Peer() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }
  Peer(const Peer&) = delete;
  auto operator=(const Peer&) -> Peer& = delete;
  Peer(Peer&&) = delete;
  auto operator=(Peer&&) -> Peer& = delete;

  auto fd() const -> int { return fd_; }
  auto error_text() const -> const char* { return std::strerror(error_); }

  auto write_byte(uint8_t byte) const -> bool {
    return write(fd_, &byte, 1) == 1;
  }

  auto write_text(const std::string& text) const -> bool {
    return write(fd_, text.data(), text.size()) ==
           static_cast<ssize_t>(text.size());
  }

  // The kernel moves a byte across a pseudo-terminal at once; the wait only
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

  // One wait overall, so a stream that stops short fails on its contents
  // rather than on time.
  auto read_bytes(size_t count) const -> std::vector<uint8_t> {
    using Clock = std::chrono::steady_clock;
    const auto deadline =
        Clock::now() + std::chrono::milliseconds(peer_wait_ms);
    std::vector<uint8_t> out;
    while (out.size() < count) {
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - Clock::now());
      if (left.count() <= 0) {
        break;
      }
      pollfd request{};
      request.fd = fd_;
      request.events = POLLIN;
      if (poll(&request, 1, static_cast<int>(left.count())) <= 0) {
        break;
      }
      std::array<uint8_t, 64> chunk{};
      const size_t want = std::min(chunk.size(), count - out.size());
      const ssize_t got = read(fd_, chunk.data(), want);
      if (got <= 0) {
        break;
      }
      out.insert(out.end(), chunk.begin(), chunk.begin() + got);
    }
    return out;
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

auto hex(const std::vector<uint8_t>& bytes) -> std::string {
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

auto file_bytes_hex(const std::string& path) -> std::string {
  std::ifstream in(path, std::ios::binary);
  return hex(std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>()));
}

auto write_file(const std::string& path, const std::string& text) -> void {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

struct TokenProbe {
  HostInterface* host = nullptr;
  void* token = nullptr;
  int slot = 0;
};

TokenProbe serial_probe;
TokenProbe printer_probe;

auto serial_probe_init(int slot, HostInterface* host) -> void* {
  serial_probe = TokenProbe();
  serial_probe.host = host;
  serial_probe.slot = slot;
  serial_probe.token =
      host->SinkOpen(&serial_probe, slot, peripheral_sink_serial);
  return &serial_probe;
}

auto printer_probe_init(int slot, HostInterface* host) -> void* {
  printer_probe = TokenProbe();
  printer_probe.host = host;
  printer_probe.slot = slot;
  printer_probe.token =
      host->SinkOpen(&printer_probe, slot, peripheral_sink_printer);
  return &printer_probe;
}

auto probe_shutdown(void* instance) -> void {
  auto* probe = static_cast<TokenProbe*>(instance);
  probe->host->SinkClose(probe->token);
}

Peripheral serial_probe_card = {
    LINAPPLE_ABI_VERSION,
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
    nullptr,
};

Peripheral printer_probe_card = {
    LINAPPLE_ABI_VERSION,
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
    nullptr,
};

constexpr int serial_probe_slot = 3;
constexpr int printer_probe_slot = 5;

}  // namespace

TEST_CASE(
    "Serial Frontend: the frontend's sink is installed, and a serial token "
    "opened through it reads no byte and reports no lines") {
  ScopedTestConfig config(ScopedTestConfig::enhanced_2e_only());
  HeadlessHarness harness(config);
  REQUIRE(peripheral_register(&serial_probe_card, serial_probe_slot) == 0);
  REQUIRE(peripheral_register(&printer_probe_card, printer_probe_slot) == 0);
  REQUIRE(serial_probe.token != nullptr);
  REQUIRE(printer_probe.token != nullptr);
  HostInterface* host = serial_probe.host;

  // A printer slot is ready the moment it is opened, which proves the
  // dispatcher is in and forwarding by kind.
  CHECK(host->SinkReady(printer_probe.token));

  uint8_t byte = 0x5A;
  CHECK(host->SinkRead(serial_probe.token, &byte) == false);
  CHECK(byte == 0x5A);
  uint8_t lines = 0xA5;
  CHECK(host->SinkGetLines(serial_probe.token, &lines) == false);
  CHECK(lines == 0xA5);
  CHECK(host->SinkReady(serial_probe.token) == false);
  host->SinkWrite(serial_probe.token, 0xC8);
  const PeripheralSerialLine line = {9600, 8, 0, 2, 1, 1, 0, {0, 0}};
  host->SinkSetLine(serial_probe.token, &line);
  CHECK(host->SinkRead(serial_probe.token, &byte) == false);
  CHECK(byte == 0x5A);

  peripheral_unregister(serial_probe_slot);
  peripheral_unregister(printer_probe_slot);
}

TEST_CASE(
    "Serial Frontend: Serial Port = pty creates a pseudo-terminal whose peer "
    "receives what the card writes, and a byte the peer writes reaches read "
    "within one tick") {
  ScopedTestConfig config(serial_in_slot_2("pty"));
  ScopedLogCapture log;
  HeadlessHarness harness(config);
  REQUIRE(super_serial_frontend_primary_slot() == card_slot);
  const std::string path = super_serial_frontend_device_path(card_slot);
  REQUIRE_MESSAGE(!path.empty(),
                  "the pseudo-terminal could not be created: " << log.joined());
  CHECK(path.rfind("/dev/pts/", 0) == 0);
  Peer peer(path);
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

  // Raw mode: 0x8D is neither expanded to CR LF nor echoed back.
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
  ScopedTestConfig config(serial_in_slot_2("pty"));
  HeadlessHarness harness(config);
  const std::string path = super_serial_frontend_device_path(card_slot);
  REQUIRE(!path.empty());
  ScopedLogCapture log;

  uint8_t byte = 0;
  {
    Peer first(path);
    REQUIRE_MESSAGE(first.fd() >= 0, first.error_text());
    REQUIRE(first.write_byte(0x31));
    sink().tick(nullptr);
    CHECK(sink().read(nullptr, card_slot, &byte));
    CHECK(byte == 0x31);
  }

  // With no peer the master reports hang-up and EIO: the line idle.
  for (int i = 0; i < 5; ++i) {
    sink().tick(nullptr);
  }
  CHECK(sink().read(nullptr, card_slot, &byte) == false);
  CHECK(sink().ready(nullptr, card_slot));
  uint8_t lines = 0;
  CHECK(sink().get_lines(nullptr, card_slot, &lines));
  CHECK(lines == 0x07);

  {
    Peer second(path);
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
  ScopedTestConfig config(serial_in_slot_2("loopback"));
  HeadlessHarness harness(config);
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
  ScopedTestConfig config(serial_in_slot_2(""));
  HeadlessHarness harness(config);
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
  ScopedTempDir dir("linapple_serial_test_");
  const std::string path = dir.path() + "/line.txt";
  ScopedTestConfig config(serial_in_slot_2(path));
  ScopedLogCapture log;
  HeadlessHarness harness(config);
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
  ScopedTempFile file(".txt");
  write_file(file.path(), "AB");
  ScopedTestConfig config(serial_in_slot_2(file.path()));
  ScopedLogCapture log;
  HeadlessHarness harness(config);
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
  ScopedTestConfig config(serial_in_slot_2("/dev/null"));
  ScopedLogCapture log;
  HeadlessHarness harness(config);
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
  const PeripheralSerialLine line = {9600, 8, 0, 2, 1, 1, 0, {0, 0}};
  sink().set_line(nullptr, card_slot, &line);
  INFO(log.joined());
  CHECK(log.count_containing("serial port") == 0);
  CHECK(log.count_containing("/dev/null") == 0);
}

TEST_CASE(
    "Serial Frontend: set_line at 9600 8N1 sets B9600, CS8, no PARENB and no "
    "CSTOPB on the pseudo-terminal, odd parity sets PARODD, 7E2 sets CSTOPB "
    "and B300 (a pseudo-terminal forces CS8), and baud 0 leaves the speed") {
  ScopedTestConfig config(serial_in_slot_2("pty"));
  HeadlessHarness harness(config);
  const std::string path = super_serial_frontend_device_path(card_slot);
  REQUIRE(!path.empty());
  Peer peer(path);
  REQUIRE_MESSAGE(peer.fd() >= 0, peer.error_text());

  PeripheralSerialLine line = {
      9600, 8, peripheral_serial_parity_none, 2, 1, 1, 0, {0, 0},
  };
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

  // The kernel's pty driver forces CS8 and clears PARENB on every change, so
  // only PARODD can be read back here.
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
    ScopedTestConfig config(serial_in_slot_2(
        "", {
                {"Configuration", "Serial Switches 1", "ON ON MAYBE"},
                {"Configuration", "Serial Switches 2", ""},
            }));
    ScopedLogCapture log;
    HeadlessHarness harness(config);
    CHECK(read_switch_register(card_slot, 1) == 0xEC);
    CHECK(read_switch_register(card_slot, 2) == 0x52);
    CHECK(log.count_containing("Serial Switches 1") == 1);
    CHECK(log.count_containing("Serial Switches 2") == 0);
  }
  SUBCASE("six tokens and eight tokens are both malformed") {
    ScopedTestConfig config(serial_in_slot_2(
        "",
        {
            {"Configuration", "Serial Switches 1", "ON ON ON ON ON ON"},
            {"Configuration", "Serial Switches 2", "ON ON ON ON ON ON ON ON"},
        }));
    ScopedLogCapture log;
    HeadlessHarness harness(config);
    CHECK(read_switch_register(card_slot, 1) == 0xEC);
    CHECK(read_switch_register(card_slot, 2) == 0x52);
    CHECK(log.count_containing("Serial Switches 1") == 1);
    CHECK(log.count_containing("Serial Switches 2") == 1);
  }
  SUBCASE("the manual's printer-mode rows, in any case and with commas") {
    ScopedTestConfig config(serial_in_slot_2(
        "",
        {
            {"Configuration", "Serial Switches 1", "off off off on off on on"},
            {
                "Configuration",
                "Serial Switches 2",
                "ON, ON, OFF, ON, OFF, OFF, OFF",
            },
        }));
    ScopedLogCapture log;
    HeadlessHarness harness(config);
    CHECK(read_switch_register(card_slot, 1) == 0xEE);
    CHECK(read_switch_register(card_slot, 2) == 0x5A);
    CHECK(log.count_containing("Serial Switches") == 0);
  }
}

TEST_CASE(
    "Serial Frontend: with two cards the lower slot is the primary, and the "
    "second reads no lines and drops writes") {
  ScopedTestConfig::Description description = serial_in_slot_2("loopback");
  description.slots[second_card_slot - 1] = "Super Serial Card";
  ScopedTestConfig config(description);
  HeadlessHarness harness(config);
  REQUIRE(super_serial_frontend_primary_slot() == card_slot);
  CHECK(super_serial_frontend_device_path(second_card_slot).empty());

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
  ScopedTestConfig::Description description =
      ScopedTestConfig::enhanced_2e_only();
  description.extras.push_back({"Configuration", "Serial Port", "loopback"});
  ScopedTestConfig config(description);
  ScopedLogCapture log;
  HeadlessHarness harness(config);
  CHECK(super_serial_frontend_primary_slot() == 0);
  for (int slot = 1; slot <= 7; ++slot) {
    CHECK(super_serial_frontend_device_path(slot).empty());
    CHECK(sink().ready(nullptr, slot) == false);
  }
  CHECK(log.count_containing("serial port") == 0);
  CHECK(log.count_containing("Super Serial Card") == 0);
}

namespace {

// The sessions type at the Applesoft prompt, which needs the keyboard card.
#ifdef ENABLE_PERIPHERAL_KEYBOARD
constexpr uint32_t prompt_frame_cap = 300;
constexpr uint32_t session_frame_cap = 60;

auto screen_has_row(const HeadlessHarness& harness, const std::string& text)
    -> bool {
  for (int row = 0; row < 24; ++row) {
    if (harness.get_text_row(row) == text) {
      return true;
    }
  }
  return false;
}

// No disk controller, so the Autostart scan falls through to Applesoft.
auto boot_to_prompt(HeadlessHarness& harness) -> void {
  harness.boot();
  uint32_t frames = 0;
  while (!screen_has_row(harness, "]") && frames < prompt_frame_cap) {
    harness.run_frames(1);
    ++frames;
  }
  CAPTURE(frames);
  REQUIRE(screen_has_row(harness, "]"));
}

// The cap turns a byte that never arrives into a failed check, not a hang.
template <typename Condition>
auto run_frames_until(HeadlessHarness& harness, Condition condition)
    -> uint32_t {
  uint32_t frames = 0;
  while (!condition() && frames < session_frame_cap) {
    harness.run_frames(1);
    ++frames;
  }
  return frames;
}

// Zeroed through the write page as well, so a stale byte from the prompt's
// own line cannot stand in for a received one.
auto clear_input_buffer() -> void {
  for (uint16_t address = 0x0200; address < 0x0210; ++address) {
    mem[address] = 0;
    uint8_t* page = memwrite[address >> 8];
    if (page != nullptr) {
      page[address & 0xFF] = 0;
    }
  }
}
#endif

auto open_peer(const ScopedLogCapture& log) -> std::string {
  REQUIRE(super_serial_frontend_primary_slot() == card_slot);
  const std::string path = super_serial_frontend_device_path(card_slot);
  REQUIRE_MESSAGE(!path.empty(),
                  "the pseudo-terminal could not be created: " << log.joined());
  return path;
}

}  // namespace

#ifdef ENABLE_PERIPHERAL_KEYBOARD
// The firmware ORs $80 into each received byte for GETLN, which stores it at
// $0200 with no echo to the line.
TEST_CASE(
    "Serial Frontend: IN#2 at the Applesoft prompt takes HELLO written to the "
    "pseudo-terminal's peer into the input buffer, and the next line the peer "
    "writes is executed") {
  ScopedTestConfig config(serial_in_slot_2("pty"));
  ScopedLogCapture log;
  HeadlessHarness harness(config);
  Peer peer(open_peer(log));
  REQUIRE_MESSAGE(peer.fd() >= 0, peer.error_text());
  boot_to_prompt(harness);

  harness.type_string("IN#2\r", 2);
  harness.run_frames(4);
  // KSW points at the slot page's input entry, $Cn05.
  CHECK(mem[0x38] == 0x05);
  CHECK(mem[0x39] == 0xC0 + card_slot);

  clear_input_buffer();
  REQUIRE(peer.write_text("HELLO"));
  const uint32_t frames =
      run_frames_until(harness, [] -> bool { return mem[0x0204] != 0; });
  CAPTURE(frames);
  CHECK(mem[0x0200] == 0xC8);
  CHECK(mem[0x0201] == 0xC5);
  CHECK(mem[0x0202] == 0xCC);
  CHECK(mem[0x0203] == 0xCC);
  CHECK(mem[0x0204] == 0xCF);
  CHECK(mem[0x0205] == 0x00);
  CHECK(peer.has_byte() == false);

  // HELLO alone is a syntax error; both it and the 42 stay on the screen.
  REQUIRE(peer.write_text("\r"));
  harness.run_frames(6);
  REQUIRE(peer.write_text("PRINT 7*6\r"));
  run_frames_until(harness,
                   [&]() -> bool { return screen_has_row(harness, "42"); });
  CHECK(screen_has_row(harness, "?SYNTAX ERROR"));
  CHECK(screen_has_row(harness, "42"));
}

TEST_CASE(
    "Serial Frontend: PR#2 at the Applesoft prompt programs the line the peer "
    "sees to 9600 baud and streams the session to the pseudo-terminal's peer "
    "with bit 7 set and no line feeds") {
  ScopedTestConfig config(serial_in_slot_2("pty"));
  ScopedLogCapture log;
  HeadlessHarness harness(config);
  Peer peer(open_peer(log));
  REQUIRE_MESSAGE(peer.fd() >= 0, peer.error_text());
  boot_to_prompt(harness);
  CHECK(peer.has_byte() == false);

  harness.type_string("PR#2\r", 2);
  harness.run_frames(4);
  CHECK(mem[0x36] == 0x07);
  CHECK(mem[0x37] == 0xC0 + card_slot);
  termios settings{};
  REQUIRE(tcgetattr(peer.fd(), &settings) == 0);
  CHECK(cfgetospeed(&settings) == B9600);

  harness.type_string("PRINT \"HELLO\"\r", 2);
  harness.run_frames(4);
  harness.type_string("PR#0\r", 2);
  harness.run_frames(4);
  const std::vector<uint8_t> applesoft_session_stream = {
      0x8D, 0xDD, 0xD0, 0xD2, 0xC9, 0xCE, 0xD4, 0xA0, 0xA2, 0xC8,
      0xC5, 0xCC, 0xCC, 0xCF, 0xA2, 0x8D, 0xC8, 0xC5, 0xCC, 0xCC,
      0xCF, 0x8D, 0x8D, 0xDD, 0xD0, 0xD2, 0xA3, 0xB0, 0x8D,
  };
  CHECK(hex(peer.read_bytes(applesoft_session_stream.size())) ==
        hex(applesoft_session_stream));
  CHECK(peer.has_byte() == false);
  CHECK(screen_has_row(harness, "HELLO"));
}
#endif

TEST_CASE(
    "Serial Frontend: the switch keys set to the manual's printer-mode rows "
    "reach the card before the first frame, so $C0A1 reads $EE and $C0A2 "
    "reads $5A with no think between") {
  ScopedTestConfig config(serial_in_slot_2(
      "pty",
      {
          {"Configuration", "Serial Switches 1", "OFF OFF OFF ON OFF ON ON"},
          {"Configuration", "Serial Switches 2", "ON ON OFF ON OFF OFF OFF"},
      }));
  ScopedLogCapture log;
  HeadlessHarness harness(config);
  open_peer(log);
  // No frame or think has run since the constructor: configure itself
  // drained the switch command.
  CHECK(read_switch_register(card_slot, 1) == 0xEE);
  CHECK(read_switch_register(card_slot, 2) == 0x5A);
  CHECK(log.count_containing("Serial Switches") == 0);
}
