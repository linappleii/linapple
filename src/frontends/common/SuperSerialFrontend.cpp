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

constexpr int k_slot_count = 7;
constexpr size_t k_switch_count = 7;
// 9600 baud, communications mode, DCD from DB-25 pin 8; 1 stop bit, 8 data
// bits, no parity, no LF after CR, interrupts forwarded (1981 manual pp. 6-8,
// 22-24, Table A-7 p. 54).
constexpr uint8_t k_default_switches_1 = 0x78;
constexpr uint8_t k_default_switches_2 = 0x2F;

constexpr uint8_t k_cts = 0x01;
constexpr uint8_t k_dsr = 0x02;
constexpr uint8_t k_dcd = 0x04;
constexpr uint8_t k_all_lines = k_cts | k_dsr | k_dcd;

// The kernel buffers a few KiB behind this ring, so RTS can drop at the
// high-water mark and a far end that honours it still has room to stop.
constexpr size_t k_receive_ring_size = 4096;
constexpr size_t k_receive_high_water = 3072;
// A byte parks here only when the kernel refuses it. The card sees CTS drop
// once the ring is full and keeps its next byte in its own register, so the
// ring never has to be large.
constexpr size_t k_transmit_ring_size = 64;
// Ticks come once per frame, so sixty is a second of emulated time between
// attempts on a path that could not be opened.
constexpr uint32_t k_open_retry_ticks = 60;
constexpr size_t k_read_chunk = 256;

// <sys/ioctl.h> carries these on every Linux architecture, but the include
// cleaner attributes them to the kernel header behind it, which differs by
// architecture and so is not named here.
// NOLINTBEGIN(misc-include-cleaner)
constexpr unsigned long k_modem_get = TIOCMGET;
constexpr unsigned long k_modem_set = TIOCMBIS;
constexpr unsigned long k_modem_clear = TIOCMBIC;
// NOLINTEND(misc-include-cleaner)

enum class DeviceKind_t : uint8_t { none, pty, loopback, path };
// Decided from stat and isatty at open: a tty gets termios and the modem
// ioctls, a regular file is appended to and never read, anything else is a
// plain read-write stream.
enum class PathKind_t : uint8_t { tty, file, stream };

// Each kind of failure is logged once, so a port that is wrong at every tick
// does not write a line per frame.
enum LoggedFailure_t : uint32_t {
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
struct Ring_t {
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

struct Device_t {
  DeviceKind_t kind = DeviceKind_t::none;
  PathKind_t path_kind = PathKind_t::stream;
  // The pseudo-terminal's peer path, or the resolved device path.
  std::string path;
  int fd = -1;
  // A tty that answers TIOCMGET. A pseudo-terminal slave given as a path
  // answers ENOTTY and is driven as a three-wire line instead.
  bool has_modem_lines = false;
  bool outputs_applied = false;
  bool dtr_out = false;
  bool rts_out = false;
  bool break_out = false;
  uint8_t inputs = k_all_lines;
  bool line_known = false;
  PeripheralSerialLine_t line{};
  uint32_t retry_ticks = 0;
  uint32_t logged = 0;
  bool announce_open = false;
  Ring_t<k_receive_ring_size> receive;
  Ring_t<k_transmit_ring_size> transmit;
};

// The master is opened once per process: reopening would mint another
// /dev/pts/N and strand the path that was logged.
struct PtyMaster_t {
  bool attempted = false;
  int fd = -1;
  std::string path;
};

struct Speed_t {
  uint32_t baud;
  speed_t constant;
};

// 109.92 and 134.58 baud arrive rounded from the card; 3600 and 7200 have no
// POSIX constant and leave the speed as it was.
constexpr std::array<Speed_t, 13> k_speeds = {{{50, B50},
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
                                               {19200, B19200}}};

std::array<bool, k_slot_count> g_in_use{};
int g_primary_slot = 0;
Device_t g_device;
PtyMaster_t g_pty;

auto slot_is_valid(int slot) -> bool {
  return slot >= 1 && slot <= k_slot_count;
}

auto slot_in_use(int slot) -> bool {
  return slot_is_valid(slot) && g_in_use.at(static_cast<size_t>(slot - 1));
}

auto is_primary(int slot) -> bool {
  return slot != 0 && slot == g_primary_slot && slot_in_use(slot);
}

auto device_is_open() -> bool {
  switch (g_device.kind) {
    case DeviceKind_t::loopback:
      return true;
    case DeviceKind_t::pty:
    case DeviceKind_t::path:
      return g_device.fd >= 0;
    default:
      return false;
  }
}

auto uses_termios() -> bool {
  if (g_device.fd < 0) {
    return false;
  }
  return g_device.kind == DeviceKind_t::pty ||
         (g_device.kind == DeviceKind_t::path &&
          g_device.path_kind == PathKind_t::tty);
}

auto uses_modem_ioctls() -> bool {
  return g_device.kind == DeviceKind_t::path &&
         g_device.path_kind == PathKind_t::tty && g_device.fd >= 0 &&
         g_device.has_modem_lines;
}

[[gnu::format(printf, 2, 3)]] auto warn_once(uint32_t kind, const char* format,
                                             ...) -> void {
  if ((g_device.logged & kind) != 0) {
    return;
  }
  g_device.logged |= kind;
  va_list args;
  va_start(args, format);
  Logger::log_message_v(LogLevel_t::warning, format, args);
  va_end(args);
}

auto fail_once(uint32_t kind, const char* action, int error) -> void {
  warn_once(kind, "serial port %s: cannot %s: %s\n", g_device.path.c_str(),
            action, std::strerror(error));
}

auto inputs_from(int modem_bits) -> uint8_t {
  uint8_t mask = 0;
  if ((modem_bits & TIOCM_CTS) != 0) {
    mask |= k_cts;
  }
  if ((modem_bits & TIOCM_DSR) != 0) {
    mask |= k_dsr;
  }
  if ((modem_bits & TIOCM_CAR) != 0) {
    mask |= k_dcd;
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
                g_device.path.c_str());
      return 0;
#endif
    default:
      return 0;
  }
}

auto apply_format(termios* settings) -> void {
  const PeripheralSerialLine_t& line = g_device.line;
  tcflag_t clear = PARENB | PARODD | CSTOPB;
#ifdef CMSPAR
  clear |= CMSPAR;
#endif
  tcflag_t set = parity_flags(line.parity);
  // A pseudo-terminal has no word length or parity: the kernel's driver
  // forces CS8 and clears PARENB on every change, and glibc's tcsetattr
  // reports EINVAL when a value it asked for did not take. So those two are
  // not asked of a pseudo-terminal; PARODD and CMSPAR are set all the same,
  // so a peer reading the slave's settings still learns the card's parity.
  if (g_device.kind == DeviceKind_t::pty) {
    set &= ~static_cast<tcflag_t>(PARENB);
  } else {
    clear |= CSIZE;
    set |= word_length_flag(line.data_bits);
  }
  settings->c_cflag &= ~clear;
  settings->c_cflag |= set;

  // POSIX has no 1.5-stop-bit flag. The 6551 produces 1.5 only with a 5-bit
  // word (SY6551 Fig. 6), and with a 5-bit word CSTOPB is 1.5 stop bits on
  // UARTs that follow the 8250's convention.
  if (line.stop_half_bits > 2) {
    settings->c_cflag |= CSTOPB;
  }

  // Baud 0 is the card selecting no clock, not a request to hang up, so the
  // speed is left alone.
  if (line.baud == 0) {
    return;
  }
  for (const Speed_t& speed : k_speeds) {
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
            g_device.path.c_str(), static_cast<unsigned>(line.baud));
}

auto apply_termios() -> void {
  if (!uses_termios()) {
    return;
  }
  termios settings{};
  if (tcgetattr(g_device.fd, &settings) != 0) {
    fail_once(logged_termios, "read the line settings", errno);
    return;
  }
  cfmakeraw(&settings);
  // The modem lines are the card's to read through TIOCMGET, not the
  // kernel's to act on, and closing the port at shutdown must not hang up a
  // modem the user means to keep.
  settings.c_cflag |= CLOCAL | CREAD;
  settings.c_cflag &= ~static_cast<tcflag_t>(HUPCL);
  if (g_device.line_known) {
    apply_format(&settings);
  }
  if (tcsetattr(g_device.fd, TCSANOW, &settings) != 0) {
    fail_once(logged_termios, "set the line settings", errno);
  }
}

auto set_modem_bits(unsigned long request, int bits, const char* action)
    -> void {
  int value = bits;
  if (ioctl(g_device.fd, request, &value) != 0) {
    fail_once(logged_modem, action, errno);
  }
}

auto apply_outputs() -> void {
  if (!uses_modem_ioctls()) {
    return;
  }
  const bool dtr = g_device.line_known && g_device.line.dtr != 0;
  // RTS also tells the far end to stop when the ring is nearly full: a far
  // end that ignores it overflows the kernel's buffer with no overrun
  // anywhere in the machine.
  const bool rts = g_device.line_known && g_device.line.rts != 0 &&
                   g_device.receive.count < k_receive_high_water;
  int set = 0;
  int clear = 0;
  if (!g_device.outputs_applied || dtr != g_device.dtr_out) {
    (dtr ? set : clear) |= TIOCM_DTR;
  }
  if (!g_device.outputs_applied || rts != g_device.rts_out) {
    (rts ? set : clear) |= TIOCM_RTS;
  }
  g_device.outputs_applied = true;
  g_device.dtr_out = dtr;
  g_device.rts_out = rts;
  if (set != 0) {
    set_modem_bits(k_modem_set, set, "assert DTR or RTS");
  }
  if (clear != 0) {
    set_modem_bits(k_modem_clear, clear, "deassert DTR or RTS");
  }

  const bool brk = g_device.line_known && g_device.line.brk != 0;
  if (brk == g_device.break_out) {
    return;
  }
  g_device.break_out = brk;
  // The break lasts exactly as long as the card holds its command bits at
  // break, as on hardware; tcsendbreak would instead block the emulation
  // thread for a fixed fraction of a second.
#if defined(TIOCSBRK) && defined(TIOCCBRK)
  if (ioctl(g_device.fd, brk ? TIOCSBRK : TIOCCBRK) != 0) {
    fail_once(logged_break, brk ? "start a break" : "end a break", errno);
  }
#else
  warn_once(logged_break, "serial port %s: the host cannot send a break\n",
            g_device.path.c_str());
#endif
}

auto refresh_inputs() -> void {
  if (!uses_modem_ioctls()) {
    return;
  }
  int bits = 0;
  if (ioctl(g_device.fd, k_modem_get, &bits) != 0) {
    fail_once(logged_modem, "read the modem lines", errno);
    g_device.has_modem_lines = false;
    g_device.inputs = k_all_lines;
    return;
  }
  g_device.inputs = inputs_from(bits);
}

auto open_path_device() -> void {
  struct stat info{};
  if (stat(g_device.path.c_str(), &info) != 0) {
    fail_once(logged_open, "open", errno);
    g_device.announce_open = true;
    return;
  }
  int fd = -1;
  if (S_ISREG(info.st_mode)) {
    g_device.path_kind = PathKind_t::file;
    fd = open(g_device.path.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC);
  } else {
    g_device.path_kind = PathKind_t::stream;
    fd =
        open(g_device.path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  }
  if (fd < 0) {
    fail_once(logged_open, "open", errno);
    g_device.announce_open = true;
    return;
  }
  g_device.fd = fd;
  g_device.outputs_applied = false;
  g_device.break_out = false;
  g_device.has_modem_lines = false;
  g_device.inputs = k_all_lines;
  if (g_device.path_kind == PathKind_t::stream && isatty(fd) == 1) {
    g_device.path_kind = PathKind_t::tty;
    apply_termios();
    int bits = 0;
    if (ioctl(fd, k_modem_get, &bits) == 0) {
      g_device.has_modem_lines = true;
      g_device.inputs = inputs_from(bits);
    } else if (errno != ENOTTY) {
      fail_once(logged_modem, "read the modem lines", errno);
    }
    // The kernel asserts DTR on open; the card does not until a program
    // writes its command register, so the line starts where the card has it.
    apply_outputs();
  }
  // At warning level, as the printer's recovery line is: the frontend's
  // default verbosity shows nothing below it, and a port that failed and
  // then opened is something the user asked about.
  if (g_device.announce_open) {
    g_device.announce_open = false;
    g_device.logged &= ~static_cast<uint32_t>(logged_open);
    Logger::warning("serial port %s opened; the line in slot %d is connected\n",
                    g_device.path.c_str(), g_primary_slot);
  }
}

// A path device whose descriptor reports an error has gone away; the retry
// that serves a path that could not be opened reopens it.
auto lose_device() -> void {
  warn_once(logged_read, "serial port %s: the device went away\n",
            g_device.path.c_str());
  close(g_device.fd);
  g_device.fd = -1;
  g_device.retry_ticks = 0;
  g_device.announce_open = true;
}

auto open_pty_master() -> void {
  if (g_pty.attempted) {
    return;
  }
  g_pty.attempted = true;
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
  g_pty.fd = fd;
  g_pty.path = name;
  // The path is the one thing a user who chose "pty" needs, and the
  // frontend's default verbosity shows nothing below warning.
  Logger::warning(
      "Super Serial Card in slot %d: the line is pseudo-terminal %s; connect "
      "a terminal program to it\n",
      g_primary_slot, name);
}

// Returns false when the kernel would block. Any other failure is logged
// once and the byte is lost, as it is on a cable with nothing at the far end.
auto write_now(uint8_t byte) -> bool {
  for (;;) {
    const ssize_t written = write(g_device.fd, &byte, 1);
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
  if (g_device.transmit.push(byte)) {
    return;
  }
  warn_once(logged_overflow,
            "serial port %s: a byte written while CTS was deasserted is "
            "lost\n",
            g_device.path.c_str());
}

auto drain_transmit() -> void {
  uint8_t byte = 0;
  if (g_device.kind == DeviceKind_t::loopback) {
    while (!g_device.transmit.empty() && !g_device.receive.full()) {
      g_device.transmit.pop(&byte);
      g_device.receive.push(byte);
    }
    return;
  }
  if (g_device.fd < 0) {
    return;
  }
  while (!g_device.transmit.empty()) {
    if (!write_now(g_device.transmit.front())) {
      return;
    }
    g_device.transmit.pop(&byte);
  }
}

auto poll_input() -> void {
  if (g_device.fd < 0 || g_device.receive.full()) {
    return;
  }
  if (g_device.kind == DeviceKind_t::path &&
      g_device.path_kind == PathKind_t::file) {
    return;
  }
  pollfd request{};
  request.fd = g_device.fd;
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
    // A pseudo-terminal with no peer reports POLLHUP until one opens; that
    // is the line idle, not a fault.
    if (g_device.kind == DeviceKind_t::path &&
        (request.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
      lose_device();
    }
    return;
  }
  std::array<uint8_t, k_read_chunk> chunk{};
  const size_t want = g_device.receive.room() < chunk.size()
                          ? g_device.receive.room()
                          : chunk.size();
  for (;;) {
    const ssize_t got = read(g_device.fd, chunk.data(), want);
    if (got > 0) {
      for (size_t i = 0; i < static_cast<size_t>(got); ++i) {
        g_device.receive.push(chunk.at(i));
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
    if (errno == EIO && g_device.kind == DeviceKind_t::pty) {
      return;
    }
    if (g_device.kind == DeviceKind_t::path) {
      lose_device();
    } else {
      fail_once(logged_read, "read", errno);
    }
    return;
  }
}

auto release_device() -> void {
  if (g_device.kind == DeviceKind_t::path && g_device.fd >= 0) {
    drain_transmit();
    close(g_device.fd);
  }
  g_device = Device_t();
}

auto select_device(const SuperSerialFrontendSettings_t& settings) -> void {
  const std::string& port = settings.port;
  if (port.empty()) {
    return;
  }
  if (port == "pty") {
    g_device.kind = DeviceKind_t::pty;
    open_pty_master();
    g_device.fd = g_pty.fd;
    g_device.path = g_pty.path;
    if (g_device.fd < 0) {
      return;
    }
    // Raw mode is set on the master and so inherited by every peer that
    // opens the slave: the kernel's default line discipline would echo the
    // card's bytes back to it and turn its LF into CR LF.
    apply_termios();
    if (tcflush(g_device.fd, TCIOFLUSH) != 0) {
      fail_once(logged_pty, "flush the pseudo-terminal", errno);
    }
    return;
  }
  if (port == "loopback") {
    g_device.kind = DeviceKind_t::loopback;
    return;
  }
  g_device.kind = DeviceKind_t::path;
  g_device.path =
      port.front() == '/' ? port : Path::join(settings.base_dir, port);
  if (slot_in_use(g_primary_slot)) {
    open_path_device();
  }
}

auto is_separator(char c) -> bool {
  return std::isspace(static_cast<unsigned char>(c)) != 0 || c == ',';
}

// Seven ON or OFF tokens in the manual's order, switch 1 first; bit k of the
// image is switch k + 1 and 1 means ON.
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
    if (count == k_switch_count) {
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
  if (count != k_switch_count) {
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

auto sink_open(void* ctx, int slot, PeripheralSinkKind_t kind) -> void {
  (void)ctx;
  if (!slot_is_valid(slot)) {
    return;
  }
  g_in_use.at(static_cast<size_t>(slot - 1)) = kind == peripheral_sink_serial;
  if (slot == g_primary_slot && kind == peripheral_sink_serial &&
      g_device.kind == DeviceKind_t::path && g_device.fd < 0) {
    g_device.retry_ticks = 0;
    open_path_device();
  }
}

auto sink_write(void* ctx, int slot, uint8_t byte) -> void {
  (void)ctx;
  if (!is_primary(slot) || !device_is_open()) {
    return;
  }
  if (!g_device.transmit.empty()) {
    park(byte);
    return;
  }
  if (g_device.kind == DeviceKind_t::loopback) {
    if (!g_device.receive.push(byte)) {
      park(byte);
    }
    return;
  }
  if (!write_now(byte)) {
    park(byte);
  }
}

// On a serial line "ready" is "the device is open".
auto sink_ready(void* ctx, int slot) -> bool {
  (void)ctx;
  return is_primary(slot) && device_is_open();
}

auto sink_close(void* ctx, int slot) -> void {
  (void)ctx;
  if (!slot_is_valid(slot)) {
    return;
  }
  g_in_use.at(static_cast<size_t>(slot - 1)) = false;
  // The device sits behind the primary slot's token. The token is closed at
  // the card's shutdown and when the bridge's sink is installed anew, and in
  // both cases the next run configures the port again, so nothing is left
  // behind for the next run's cards to open before it does.
  if (slot == g_primary_slot) {
    release_device();
    g_primary_slot = 0;
  }
}

auto sink_tick(void* ctx) -> void {
  (void)ctx;
  if (!slot_in_use(g_primary_slot)) {
    return;
  }
  if (g_device.kind == DeviceKind_t::path && g_device.fd < 0) {
    if (++g_device.retry_ticks >= k_open_retry_ticks) {
      g_device.retry_ticks = 0;
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
  return g_device.receive.pop(byte);
}

auto sink_set_line(void* ctx, int slot, const PeripheralSerialLine_t* line)
    -> void {
  (void)ctx;
  if (!is_primary(slot) || line == nullptr) {
    return;
  }
  g_device.line = *line;
  g_device.line_known = true;
  apply_termios();
  apply_outputs();
}

auto sink_get_lines(void* ctx, int slot, uint8_t* lines) -> bool {
  (void)ctx;
  if (!is_primary(slot) || lines == nullptr ||
      g_device.kind == DeviceKind_t::none) {
    return false;
  }
  // A device that is not open yet reads CTS deasserted, so the card holds
  // its byte in the transmit register instead of losing it; DSR and DCD stay
  // asserted so the firmware's wait for them does not park PR#2 for good.
  if (!device_is_open()) {
    *lines = k_dsr | k_dcd;
    return true;
  }
  uint8_t mask = g_device.has_modem_lines ? g_device.inputs : k_all_lines;
  if (g_device.transmit.full()) {
    mask &= static_cast<uint8_t>(~k_cts);
  }
  *lines = mask;
  return true;
}

const ByteSink_t g_serial_sink = {.open = sink_open,
                                  .write = sink_write,
                                  .ready = sink_ready,
                                  .close = sink_close,
                                  .tick = sink_tick,
                                  .read = sink_read,
                                  .set_line = sink_set_line,
                                  .get_lines = sink_get_lines};

}  // namespace

auto super_serial_frontend_configure(
    const SuperSerialFrontendSettings_t& settings) -> void {
  release_device();
  g_primary_slot = 0;

  SuperSerialSwitches_t switches{};
  switches.sw1 = switch_image(settings.switches_1, "Serial Switches 1",
                              k_default_switches_1);
  switches.sw2 = switch_image(settings.switches_2, "Serial Switches 2",
                              k_default_switches_2);

  // One call is both the presence probe and the send: the bridge refuses at
  // once for a slot holding no card of that id and queues otherwise.
  bool sent = false;
  for (int slot = 1; slot <= k_slot_count; ++slot) {
    if (peripheral_command_by_id(slot, "linapple.ssc",
                                 SUPER_SERIAL_CMD_SET_SWITCHES, &switches,
                                 sizeof(switches)) != peripheral_ok) {
      continue;
    }
    sent = true;
    if (g_primary_slot == 0) {
      g_primary_slot = slot;
    }
  }
  if (g_primary_slot != 0) {
    select_device(settings);
  }
  // The queue drains only in think, and a program that touches the card in
  // its first frame must already see the configured switches.
  if (sent) {
    peripheral_manager_think(0);
  }
}

auto super_serial_frontend_sink() -> const ByteSink_t& { return g_serial_sink; }

auto super_serial_frontend_primary_slot() -> int { return g_primary_slot; }

auto super_serial_frontend_device_path(int slot) -> std::string {
  if (slot == 0 || slot != g_primary_slot) {
    return "";
  }
  return g_device.path;
}
