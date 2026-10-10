// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/SuperSerialFrontend.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Util_Path.h"

namespace {

constexpr int slot_count = 7;
constexpr size_t switch_count = 7;
// Communications mode, 9600 8N1, no LF after CR, interrupts forwarded (1981
// manual Table A-7 p. 54); the card's own default.
constexpr uint8_t default_switches_1 = 0x78;
constexpr uint8_t default_switches_2 = 0x2F;

constexpr uint8_t cts_line = 0x01;
constexpr uint8_t dsr_line = 0x02;
constexpr uint8_t dcd_line = 0x04;
constexpr uint8_t all_modem_lines = cts_line | dsr_line | dcd_line;

// RTS drops at the high-water mark so a far end that honours it has room to
// stop before the ring fills.
constexpr size_t receive_ring_size = 4096;
constexpr size_t receive_high_water = 3072;
// Holds only what the kernel refused; a full ring drops CTS and the card
// keeps its next byte in its own register.
constexpr size_t transmit_ring_size = 64;
// One tick per frame: a second between attempts to open the path.
constexpr uint32_t open_retry_ticks = 60;
constexpr size_t read_chunk = 256;

// The include cleaner attributes these to the kernel header behind
// <sys/ioctl.h>, which differs by architecture.
// NOLINTBEGIN(misc-include-cleaner)
constexpr uint64_t modem_get = TIOCMGET;
constexpr uint64_t modem_set = TIOCMBIS;
constexpr uint64_t modem_clear = TIOCMBIC;
// NOLINTEND(misc-include-cleaner)

enum class DeviceKind : uint8_t { none, pty, loopback, path };
enum class PathKind : uint8_t { tty, file, stream };

// Logged once per kind, or a port wrong at every tick writes a line per frame.
enum LoggedFailure : uint16_t {
  logged_open = 1U << 0,
  logged_termios = 1U << 1,
  logged_speed = 1U << 2,
  logged_parity = 1U << 3,
  logged_modem = 1U << 4,
  logged_break = 1U << 5,
  logged_read = 1U << 6,
  logged_write = 1U << 7,
  logged_pty = 1U << 8,
  logged_overflow = 1U << 9,
};

template <size_t Size>
struct Ring {
  std::array<uint8_t, Size> bytes{};
  size_t head = 0;
  size_t count = 0;

  auto empty() const -> bool { return count == 0; }
  auto full() const -> bool { return count == Size; }
  auto room() const -> size_t { return Size - count; }
  auto front() const -> uint8_t { return bytes.at(head); }
  auto push(uint8_t byte) -> bool {
    if (full()) {
      return false;
    }
    bytes.at((head + count) % Size) = byte;
    ++count;
    return true;
  }
  auto pop(uint8_t* byte) -> bool {
    if (empty()) {
      return false;
    }
    *byte = bytes.at(head);
    head = (head + 1) % Size;
    --count;
    return true;
  }
};

struct Device {
  DeviceKind kind = DeviceKind::none;
  PathKind path_kind = PathKind::stream;
  std::string path;
  int fd = -1;
  // A pseudo-terminal slave given as a path answers TIOCMGET with ENOTTY and
  // is driven as a three-wire line.
  bool has_modem_lines = false;
  bool outputs_applied = false;
  bool dtr_out = false;
  bool rts_out = false;
  bool break_out = false;
  uint8_t inputs = all_modem_lines;
  bool line_known = false;
  PeripheralSerialLine line{};
  uint32_t retry_ticks = 0;
  uint32_t logged = 0;
  bool announce_open = false;
  Ring<receive_ring_size> receive;
  Ring<transmit_ring_size> transmit;
};

// Opened once per process: reopening would mint another /dev/pts/N and
// strand the path that was logged.
struct PtyMaster {
  bool attempted = false;
  int fd = -1;
  std::string path;
};

struct Speed {
  uint32_t baud;
  speed_t constant;
};

// The card's 109.92 and 134.58 baud arrive rounded; 3600 and 7200 have no
// POSIX constant.
constexpr std::array<Speed, 13> serial_speeds = {
    {
        {50, B50},
        {75, B75},
        {110, B110},
        {135, B134},
        {150, B150},
        {300, B300},
        {600, B600},
        {1200, B1200},
        {1800, B1800},
        {2400, B2400},
        {4800, B4800},
        {9600, B9600},
        {19200, B19200},
    },
};

std::array<bool, slot_count> serial_in_use{};
int primary_serial_slot = 0;
Device serial_device;
PtyMaster serial_pty;

auto slot_is_valid(int slot) -> bool { return slot >= 1 && slot <= slot_count; }

auto slot_in_use(int slot) -> bool {
  return slot_is_valid(slot) && serial_in_use.at(static_cast<size_t>(slot - 1));
}

auto is_primary(int slot) -> bool {
  return slot != 0 && slot == primary_serial_slot && slot_in_use(slot);
}

auto device_is_open() -> bool {
  switch (serial_device.kind) {
    case DeviceKind::loopback:
      return true;
    case DeviceKind::pty:
    case DeviceKind::path:
      return serial_device.fd >= 0;
    default:
      return false;
  }
}

auto uses_termios() -> bool {
  if (serial_device.fd < 0) {
    return false;
  }
  return serial_device.kind == DeviceKind::pty ||
         (serial_device.kind == DeviceKind::path &&
          serial_device.path_kind == PathKind::tty);
}

auto uses_modem_ioctls() -> bool {
  return serial_device.kind == DeviceKind::path &&
         serial_device.path_kind == PathKind::tty && serial_device.fd >= 0 &&
         serial_device.has_modem_lines;
}

[[gnu::format(printf, 2, 3)]] auto warn_once(uint32_t kind, const char* format,
                                             ...) -> void {
  if ((serial_device.logged & kind) != 0) {
    return;
  }
  serial_device.logged |= kind;
  va_list args;
  va_start(args, format);
  Logger::log_message_v(LogLevel::warning, format, args);
  va_end(args);
}

auto fail_once(uint32_t kind, const char* action, int error) -> void {
  warn_once(kind, "serial port %s: cannot %s: %s\n", serial_device.path.c_str(),
            action, std::strerror(error));
}

auto inputs_from(int modem_bits) -> uint8_t {
  uint8_t mask = 0;
  if ((modem_bits & TIOCM_CTS) != 0) {
    mask |= cts_line;
  }
  if ((modem_bits & TIOCM_DSR) != 0) {
    mask |= dsr_line;
  }
  if ((modem_bits & TIOCM_CAR) != 0) {
    mask |= dcd_line;
  }
  return mask;
}

auto word_length_flag(uint8_t data_bits) -> tcflag_t {
  switch (data_bits) {
    case 5:
      return CS5;
    case 6:
      return CS6;
    case 7:
      return CS7;
    default:
      return CS8;
  }
}

auto parity_flags(uint8_t parity) -> tcflag_t {
  switch (parity) {
    case peripheral_serial_parity_odd:
      return PARENB | PARODD;
    case peripheral_serial_parity_even:
      return PARENB;
    case peripheral_serial_parity_mark:
    case peripheral_serial_parity_space:
#ifdef CMSPAR
      return PARENB | CMSPAR |
             (parity == peripheral_serial_parity_mark ? PARODD : 0);
#else
      warn_once(logged_parity,
                "serial port %s: the host has no mark or space parity; the "
                "line runs with none\n",
                serial_device.path.c_str());
      return 0;
#endif
    default:
      return 0;
  }
}

auto apply_format(termios* settings) -> void {
  const PeripheralSerialLine& line = serial_device.line;
  tcflag_t clear = PARENB | PARODD | CSTOPB;
#ifdef CMSPAR
  clear |= CMSPAR;
#endif
  tcflag_t set = parity_flags(line.parity);
  // The kernel's pty driver forces CS8 and clears PARENB on every change, and
  // glibc's tcsetattr returns EINVAL when a value did not take, so neither is
  // asked of a pseudo-terminal. PARODD and CMSPAR still tell a peer the
  // card's parity.
  if (serial_device.kind == DeviceKind::pty) {
    set &= ~static_cast<tcflag_t>(PARENB);
  } else {
    clear |= CSIZE;
    set |= word_length_flag(line.data_bits);
  }
  settings->c_cflag &= ~clear;
  settings->c_cflag |= set;

  // POSIX has no 1.5-stop-bit flag, but the 6551 produces 1.5 only with a
  // 5-bit word (SY6551 Fig. 6), where CSTOPB means 1.5 on UARTs following the
  // 8250's convention.
  if (line.stop_half_bits > 2) {
    settings->c_cflag |= CSTOPB;
  }

  // Baud 0 is the card selecting no clock, not a hang-up request.
  if (line.baud == 0) {
    return;
  }
  for (const Speed& speed : serial_speeds) {
    if (speed.baud != line.baud) {
      continue;
    }
    if (cfsetispeed(settings, speed.constant) != 0 ||
        cfsetospeed(settings, speed.constant) != 0) {
      fail_once(logged_speed, "set the line speed", errno);
    }
    return;
  }
  warn_once(logged_speed,
            "serial port %s: the host has no %u baud setting; the line speed "
            "is left as it was\n",
            serial_device.path.c_str(), static_cast<unsigned>(line.baud));
}

auto apply_termios() -> void {
  if (!uses_termios()) {
    return;
  }
  termios settings{};
  if (tcgetattr(serial_device.fd, &settings) != 0) {
    fail_once(logged_termios, "read the line settings", errno);
    return;
  }
  cfmakeraw(&settings);
  // The modem lines are the card's to read, not the kernel's to act on, and
  // closing the port must not hang up a modem the user means to keep.
  settings.c_cflag |= CLOCAL | CREAD;
  settings.c_cflag &= ~static_cast<tcflag_t>(HUPCL);
  if (serial_device.line_known) {
    apply_format(&settings);
  }
  if (tcsetattr(serial_device.fd, TCSANOW, &settings) != 0) {
    fail_once(logged_termios, "set the line settings", errno);
  }
}

auto set_modem_bits(uint64_t request, int bits, const char* action)
    -> void {
  int value = bits;
  if (ioctl(serial_device.fd, request, &value) != 0) {
    fail_once(logged_modem, action, errno);
  }
}

auto apply_outputs() -> void {
  if (!uses_modem_ioctls()) {
    return;
  }
  const bool dtr = serial_device.line_known && serial_device.line.dtr != 0;
  // RTS also stops the far end when the ring is nearly full; one that ignores
  // it overflows the kernel's buffer, never the machine.
  const bool rts = serial_device.line_known && serial_device.line.rts != 0 &&
                   serial_device.receive.count < receive_high_water;
  int set = 0;
  int clear = 0;
  if (!serial_device.outputs_applied || dtr != serial_device.dtr_out) {
    (dtr ? set : clear) |= TIOCM_DTR;
  }
  if (!serial_device.outputs_applied || rts != serial_device.rts_out) {
    (rts ? set : clear) |= TIOCM_RTS;
  }
  serial_device.outputs_applied = true;
  serial_device.dtr_out = dtr;
  serial_device.rts_out = rts;
  if (set != 0) {
    set_modem_bits(modem_set, set, "assert DTR or RTS");
  }
  if (clear != 0) {
    set_modem_bits(modem_clear, clear, "deassert DTR or RTS");
  }

  const bool brk = serial_device.line_known && serial_device.line.brk != 0;
  if (brk == serial_device.break_out) {
    return;
  }
  serial_device.break_out = brk;
  // A level, as long as the card holds its command bits at break;
  // tcsendbreak would block the emulation thread for a fixed time.
#if defined(TIOCSBRK) && defined(TIOCCBRK)
  if (ioctl(serial_device.fd, brk ? TIOCSBRK : TIOCCBRK) != 0) {
    fail_once(logged_break, brk ? "start a break" : "end a break", errno);
  }
#else
  warn_once(logged_break, "serial port %s: the host cannot send a break\n",
            serial_device.path.c_str());
#endif
}

auto refresh_inputs() -> void {
  if (!uses_modem_ioctls()) {
    return;
  }
  int bits = 0;
  if (ioctl(serial_device.fd, modem_get, &bits) != 0) {
    fail_once(logged_modem, "read the modem lines", errno);
    serial_device.has_modem_lines = false;
    serial_device.inputs = all_modem_lines;
    return;
  }
  serial_device.inputs = inputs_from(bits);
}

auto open_path_device() -> void {
  struct stat info{};
  if (stat(serial_device.path.c_str(), &info) != 0) {
    fail_once(logged_open, "open", errno);
    serial_device.announce_open = true;
    return;
  }
  int fd = -1;
  if (S_ISREG(info.st_mode)) {
    serial_device.path_kind = PathKind::file;
    fd = open(serial_device.path.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC);
  } else {
    serial_device.path_kind = PathKind::stream;
    fd = open(serial_device.path.c_str(),
              O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  }
  if (fd < 0) {
    fail_once(logged_open, "open", errno);
    serial_device.announce_open = true;
    return;
  }
  serial_device.fd = fd;
  serial_device.outputs_applied = false;
  serial_device.break_out = false;
  serial_device.has_modem_lines = false;
  serial_device.inputs = all_modem_lines;
  if (serial_device.path_kind == PathKind::stream && isatty(fd) == 1) {
    serial_device.path_kind = PathKind::tty;
    apply_termios();
    int bits = 0;
    if (ioctl(fd, modem_get, &bits) == 0) {
      serial_device.has_modem_lines = true;
      serial_device.inputs = inputs_from(bits);
    } else if (errno != ENOTTY) {
      fail_once(logged_modem, "read the modem lines", errno);
    }
    // The kernel asserts DTR on open; the card does not until a program
    // writes its command register.
    apply_outputs();
  }
  // Warning level, like the printer's recovery line: the default verbosity
  // shows nothing below it.
  if (serial_device.announce_open) {
    serial_device.announce_open = false;
    serial_device.logged &= ~static_cast<uint32_t>(logged_open);
    Logger::warning("serial port %s opened; the line in slot %d is connected\n",
                    serial_device.path.c_str(), primary_serial_slot);
  }
}

auto lose_device() -> void {
  warn_once(logged_read, "serial port %s: the device went away\n",
            serial_device.path.c_str());
  close(serial_device.fd);
  serial_device.fd = -1;
  serial_device.retry_ticks = 0;
  serial_device.announce_open = true;
}

auto open_pty_master() -> void {
  if (serial_pty.attempted) {
    return;
  }
  serial_pty.attempted = true;
  const int fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    Logger::error("serial port pty: cannot create a pseudo-terminal: %s\n",
                  std::strerror(errno));
    return;
  }
  if (grantpt(fd) != 0 || unlockpt(fd) != 0) {
    Logger::error("serial port pty: cannot unlock the pseudo-terminal: %s\n",
                  std::strerror(errno));
    close(fd);
    return;
  }
  const char* name = ptsname(fd);
  if (name == nullptr) {
    Logger::error("serial port pty: cannot name the pseudo-terminal: %s\n",
                  std::strerror(errno));
    close(fd);
    return;
  }
  serial_pty.fd = fd;
  serial_pty.path = name;
  // Warning level: the default verbosity shows nothing below it, and the
  // path is the one thing a user who chose "pty" needs.
  Logger::warning(
      "Super Serial Card in slot %d: the line is pseudo-terminal %s; connect "
      "a terminal program to it\n",
      primary_serial_slot, name);
}

// False only when the kernel would block; any other failure loses the byte,
// as a cable with nothing at the far end does.
auto write_now(uint8_t byte) -> bool {
  for (;;) {
    const ssize_t written = write(serial_device.fd, &byte, 1);
    if (written == 1) {
      return true;
    }
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written < 0 && errno == EAGAIN) {
      return false;
    }
    fail_once(logged_write, "write", written < 0 ? errno : EIO);
    return true;
  }
}

auto park(uint8_t byte) -> void {
  if (serial_device.transmit.push(byte)) {
    return;
  }
  warn_once(logged_overflow,
            "serial port %s: a byte written while CTS was deasserted is "
            "lost\n",
            serial_device.path.c_str());
}

auto drain_transmit() -> void {
  uint8_t byte = 0;
  if (serial_device.kind == DeviceKind::loopback) {
    while (!serial_device.transmit.empty() && !serial_device.receive.full()) {
      serial_device.transmit.pop(&byte);
      serial_device.receive.push(byte);
    }
    return;
  }
  if (serial_device.fd < 0) {
    return;
  }
  while (!serial_device.transmit.empty()) {
    if (!write_now(serial_device.transmit.front())) {
      return;
    }
    serial_device.transmit.pop(&byte);
  }
}

auto poll_input() -> void {
  if (serial_device.fd < 0 || serial_device.receive.full()) {
    return;
  }
  if (serial_device.kind == DeviceKind::path &&
      serial_device.path_kind == PathKind::file) {
    return;
  }
  pollfd request{};
  request.fd = serial_device.fd;
  request.events = POLLIN;
  const int ready = poll(&request, 1, 0);
  if (ready < 0) {
    if (errno != EINTR) {
      fail_once(logged_read, "poll", errno);
    }
    return;
  }
  if (ready == 0) {
    return;
  }
  if ((request.revents & POLLIN) == 0) {
    // A pseudo-terminal with no peer reports POLLHUP: the line idle, not a
    // fault.
    if (serial_device.kind == DeviceKind::path &&
        (request.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
      lose_device();
    }
    return;
  }
  std::array<uint8_t, read_chunk> chunk{};
  const size_t want = serial_device.receive.room() < chunk.size()
                          ? serial_device.receive.room()
                          : chunk.size();
  for (;;) {
    const ssize_t got = read(serial_device.fd, chunk.data(), want);
    if (got > 0) {
      for (size_t i = 0; i < static_cast<size_t>(got); ++i) {
        serial_device.receive.push(chunk.at(i));
      }
      return;
    }
    if (got == 0) {
      return;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN) {
      return;
    }
    if (errno == EIO && serial_device.kind == DeviceKind::pty) {
      return;
    }
    if (serial_device.kind == DeviceKind::path) {
      lose_device();
    } else {
      fail_once(logged_read, "read", errno);
    }
    return;
  }
}

auto release_device() -> void {
  if (serial_device.kind == DeviceKind::path && serial_device.fd >= 0) {
    drain_transmit();
    close(serial_device.fd);
  }
  serial_device = Device();
}

auto select_device(const SuperSerialFrontendSettings& settings) -> void {
  const std::string& port = settings.port;
  if (port.empty()) {
    return;
  }
  if (port == "pty") {
    serial_device.kind = DeviceKind::pty;
    open_pty_master();
    serial_device.fd = serial_pty.fd;
    serial_device.path = serial_pty.path;
    if (serial_device.fd < 0) {
      return;
    }
    // Raw mode on the master is inherited by every peer of the slave; the
    // default line discipline would echo the card's bytes and expand LF.
    apply_termios();
    if (tcflush(serial_device.fd, TCIOFLUSH) != 0) {
      fail_once(logged_pty, "flush the pseudo-terminal", errno);
    }
    return;
  }
  if (port == "loopback") {
    serial_device.kind = DeviceKind::loopback;
    return;
  }
  serial_device.kind = DeviceKind::path;
  serial_device.path =
      port.front() == '/' ? port : Path::join(settings.base_dir, port);
  if (slot_in_use(primary_serial_slot)) {
    open_path_device();
  }
}

auto is_separator(char c) -> bool {
  return std::isspace(static_cast<unsigned char>(c)) != 0 || c == ',';
}

auto parse_switch_row(const std::string& row, uint8_t* image) -> bool {
  uint8_t bits = 0;
  size_t count = 0;
  size_t pos = 0;
  while (pos < row.size()) {
    if (is_separator(row.at(pos))) {
      ++pos;
      continue;
    }
    size_t end = pos;
    std::string token;
    while (end < row.size() && !is_separator(row.at(end))) {
      token.push_back(static_cast<char>(
          std::toupper(static_cast<unsigned char>(row.at(end)))));
      ++end;
    }
    if (count == switch_count) {
      return false;
    }
    if (token == "ON") {
      bits |= static_cast<uint8_t>(1U << count);
    } else if (token != "OFF") {
      return false;
    }
    ++count;
    pos = end;
  }
  if (count != switch_count) {
    return false;
  }
  *image = bits;
  return true;
}

auto switch_image(const std::string& row, const char* key, uint8_t fallback)
    -> uint8_t {
  if (row.empty()) {
    return fallback;
  }
  uint8_t image = 0;
  if (parse_switch_row(row, &image)) {
    return image;
  }
  Logger::warning(
      "%s = \"%s\" is not seven ON or OFF tokens; the card keeps its "
      "default\n",
      key, row.c_str());
  return fallback;
}

auto sink_open(void* ctx, int slot, PeripheralSinkKind kind) -> void {
  (void)ctx;
  if (!slot_is_valid(slot)) {
    return;
  }
  serial_in_use.at(static_cast<size_t>(slot - 1)) =
      kind == peripheral_sink_serial;
  if (slot == primary_serial_slot && kind == peripheral_sink_serial &&
      serial_device.kind == DeviceKind::path && serial_device.fd < 0) {
    serial_device.retry_ticks = 0;
    open_path_device();
  }
}

auto sink_write(void* ctx, int slot, uint8_t byte) -> void {
  (void)ctx;
  if (!is_primary(slot) || !device_is_open()) {
    return;
  }
  if (!serial_device.transmit.empty()) {
    park(byte);
    return;
  }
  if (serial_device.kind == DeviceKind::loopback) {
    if (!serial_device.receive.push(byte)) {
      park(byte);
    }
    return;
  }
  if (!write_now(byte)) {
    park(byte);
  }
}

auto sink_ready(void* ctx, int slot) -> bool {
  (void)ctx;
  return is_primary(slot) && device_is_open();
}

auto sink_close(void* ctx, int slot) -> void {
  (void)ctx;
  if (!slot_is_valid(slot)) {
    return;
  }
  serial_in_use.at(static_cast<size_t>(slot - 1)) = false;
  // Released here so nothing is left for the next run's cards to open before
  // it is configured again.
  if (slot == primary_serial_slot) {
    release_device();
    primary_serial_slot = 0;
  }
}

auto sink_tick(void* ctx) -> void {
  (void)ctx;
  if (!slot_in_use(primary_serial_slot)) {
    return;
  }
  if (serial_device.kind == DeviceKind::path && serial_device.fd < 0) {
    if (++serial_device.retry_ticks >= open_retry_ticks) {
      serial_device.retry_ticks = 0;
      open_path_device();
    }
    return;
  }
  drain_transmit();
  poll_input();
  refresh_inputs();
  apply_outputs();
}

auto sink_read(void* ctx, int slot, uint8_t* byte) -> bool {
  (void)ctx;
  if (!is_primary(slot) || byte == nullptr) {
    return false;
  }
  return serial_device.receive.pop(byte);
}

auto sink_set_line(void* ctx, int slot, const PeripheralSerialLine* line)
    -> void {
  (void)ctx;
  if (!is_primary(slot) || line == nullptr) {
    return;
  }
  serial_device.line = *line;
  serial_device.line_known = true;
  apply_termios();
  apply_outputs();
}

auto sink_get_lines(void* ctx, int slot, uint8_t* lines) -> bool {
  (void)ctx;
  if (!is_primary(slot) || lines == nullptr ||
      serial_device.kind == DeviceKind::none) {
    return false;
  }
  // CTS deasserted makes the card hold its byte instead of losing it; DSR
  // and DCD asserted keep the firmware's wait for them from parking PR#n.
  if (!device_is_open()) {
    *lines = dsr_line | dcd_line;
    return true;
  }
  uint8_t mask =
      serial_device.has_modem_lines ? serial_device.inputs : all_modem_lines;
  if (serial_device.transmit.full()) {
    mask &= static_cast<uint8_t>(~cts_line);
  }
  *lines = mask;
  return true;
}

const ByteSink serial_sink = {
    .open = sink_open,
    .write = sink_write,
    .ready = sink_ready,
    .close = sink_close,
    .tick = sink_tick,
    .read = sink_read,
    .set_line = sink_set_line,
    .get_lines = sink_get_lines,
};

}  // namespace

auto super_serial_frontend_configure(
    const SuperSerialFrontendSettings& settings) -> void {
  release_device();
  primary_serial_slot = 0;

  SuperSerialSwitches switches{};
  switches.sw1 = switch_image(settings.switches_1, "Serial Switches 1",
                              default_switches_1);
  switches.sw2 = switch_image(settings.switches_2, "Serial Switches 2",
                              default_switches_2);

  // The send doubles as the presence probe: the bridge refuses a slot
  // holding no card of that id.
  bool sent = false;
  for (int slot = 1; slot <= slot_count; ++slot) {
    if (peripheral_command_by_id(slot, "linapple.ssc",
                                 SUPER_SERIAL_CMD_SET_SWITCHES, &switches,
                                 sizeof(switches)) != peripheral_ok) {
      continue;
    }
    sent = true;
    if (primary_serial_slot == 0) {
      primary_serial_slot = slot;
    }
  }
  if (primary_serial_slot != 0) {
    select_device(settings);
  }
  // The queue drains only in think, and the first frame must already see the
  // switches.
  if (sent) {
    peripheral_manager_think(0);
  }
}

auto super_serial_frontend_sink() -> const ByteSink& { return serial_sink; }

auto super_serial_frontend_primary_slot() -> int { return primary_serial_slot; }

auto super_serial_frontend_device_path(int slot) -> std::string {
  if (slot == 0 || slot != primary_serial_slot) {
    return "";
  }
  return serial_device.path;
}
