// SPDX-License-Identifier: GPL-2.0-only
#include <fcntl.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/MouseFrontend.h"
#include "frontends/tui/TuiInput.h"
#include "frontends/tui/TuiTerminal.h"
#include "frontends/tui/TuiVideo.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

auto make_non_blocking(int fd) -> void {
  const int flags = fcntl(fd, F_GETFL);
  REQUIRE(flags >= 0);
  REQUIRE(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
}

auto read_all_available(int fd) -> std::string {
  std::string out;
  std::array<char, 256> chunk{};
  while (true) {
    const ssize_t n = read(fd, chunk.data(), chunk.size());
    if (n <= 0) {
      break;
    }
    out.append(chunk.data(), static_cast<size_t>(n));
  }
  return out;
}

// Puts a pipe behind stdout for the span of the guard and hands back what was
// written so far. stdout is flushed on the way in and out so doctest's own
// report stays on the real descriptor.
class ScopedStdoutPipe_t {
 public:
  ScopedStdoutPipe_t() {
    fflush(stdout);
    REQUIRE(pipe(fds_.data()) == 0);
    make_non_blocking(fds_[0]);
    saved_ = dup(STDOUT_FILENO);
    REQUIRE(saved_ >= 0);
    REQUIRE(dup2(fds_[1], STDOUT_FILENO) >= 0);
  }

  ~ScopedStdoutPipe_t() {
    fflush(stdout);
    dup2(saved_, STDOUT_FILENO);
    close(saved_);
    close(fds_[0]);
    close(fds_[1]);
  }

  ScopedStdoutPipe_t(const ScopedStdoutPipe_t&) = delete;
  auto operator=(const ScopedStdoutPipe_t&) -> ScopedStdoutPipe_t& = delete;
  ScopedStdoutPipe_t(ScopedStdoutPipe_t&&) = delete;
  auto operator=(ScopedStdoutPipe_t&&) -> ScopedStdoutPipe_t& = delete;

  auto take() -> std::string {
    fflush(stdout);
    return read_all_available(fds_[0]);
  }

 private:
  std::array<int, 2> fds_{{-1, -1}};
  int saved_ = -1;
};

// Puts a pipe behind stdin so the terminal's reports and replies can be fed
// to the poll; the read end is non-blocking because a terminal in raw mode
// with VMIN 0 never blocks either.
class ScopedStdinPipe_t {
 public:
  ScopedStdinPipe_t() {
    REQUIRE(pipe(fds_.data()) == 0);
    make_non_blocking(fds_[0]);
    saved_ = dup(STDIN_FILENO);
    REQUIRE(saved_ >= 0);
    REQUIRE(dup2(fds_[0], STDIN_FILENO) >= 0);
  }

  ~ScopedStdinPipe_t() {
    dup2(saved_, STDIN_FILENO);
    close(saved_);
    close(fds_[0]);
    close(fds_[1]);
  }

  ScopedStdinPipe_t(const ScopedStdinPipe_t&) = delete;
  auto operator=(const ScopedStdinPipe_t&) -> ScopedStdinPipe_t& = delete;
  ScopedStdinPipe_t(ScopedStdinPipe_t&&) = delete;
  auto operator=(ScopedStdinPipe_t&&) -> ScopedStdinPipe_t& = delete;

  auto feed(const std::string& bytes) -> void {
    REQUIRE(write(fds_[1], bytes.data(), bytes.size()) ==
            static_cast<ssize_t>(bytes.size()));
    tui_input_poll();
  }

 private:
  std::array<int, 2> fds_{{-1, -1}};
  int saved_ = -1;
};

struct ModeSequence_t {
  std::string mode;
  bool enable;
};

// CSI ? Pm h and CSI ? Pm l, xterm's private mode set and reset; anything else
// in the stream is a failure the caller reports.
auto parse_mode_sequences(const std::string& text,
                          std::vector<ModeSequence_t>* out) -> bool {
  size_t i = 0;
  while (i < text.size()) {
    if (text.compare(i, 3, "\x1b[?") != 0) {
      return false;
    }
    i += 3;
    const size_t digits = i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
      ++i;
    }
    if (i == digits || i >= text.size() || (text[i] != 'h' && text[i] != 'l')) {
      return false;
    }
    out->push_back({text.substr(digits, i - digits), text[i] == 'h'});
    ++i;
  }
  return true;
}

constexpr const char* k_tracking_request =
    "\x1b[?1003h\x1b[?1006h\x1b[?1016$p\x1b[16t";
constexpr const char* k_tracking_release = "\x1b[?1016l\x1b[?1006l\x1b[?1003l";
constexpr const char* k_pixel_mode_set = "\x1b[?1016h";
constexpr const char* k_pixel_mode_reset_reply = "\x1b[?1016;2$y";
constexpr const char* k_pixel_mode_set_reply = "\x1b[?1016;1$y";
constexpr const char* k_cell_size_reply = "\x1b[6;16;8t";

// An Enhanced //e built and reset as the frontend builds it, with the mouse
// probe run as the TUI's start-up runs it.
struct TuiMachine_t {
  TestFixtures::ScopedTestConfig_t config;
  TestFixtures::ScopedCore_t core;

  explicit TuiMachine_t(
      const TestFixtures::ScopedTestConfig_t::Description_t& description)
      : config(description), core(config) {
    peripheral_manager_init();
    linapple_register_peripherals();
    linapple_reset_hard();
    mouse_frontend_initialize();
  }

  TuiMachine_t(const TuiMachine_t&) = delete;
  auto operator=(const TuiMachine_t&) -> TuiMachine_t& = delete;
  TuiMachine_t(TuiMachine_t&&) = delete;
  auto operator=(TuiMachine_t&&) -> TuiMachine_t& = delete;

  // The renderer records the box it drew; on a pipe the terminal size falls
  // back to 80 x 24 and nothing is written.
  static auto render_frame() -> void {
    static std::vector<uint32_t> pixels(560 * 384, 0);
    tui_video_render_frame(pixels.data(), 560, 384, 560);
  }

  static auto show_text_80() -> void {
    io_map_dispatch(0, 0xC051, 0, 0, 0);
    io_map_dispatch(0, 0xC00D, 1, 0, 0);
    render_frame();
  }

  static auto show_graphics() -> void {
    io_map_dispatch(0, 0xC050, 0, 0, 0);
    io_map_dispatch(0, 0xC052, 0, 0, 0);
    render_frame();
  }
};

// Shuts the TUI down while the pipes still stand in for the terminal, so
// the disables land there and not in doctest's report. Declared after the
// pipes, it goes first.
struct ScopedTuiSession_t {
  ScopedTuiSession_t() = default;
  ~ScopedTuiSession_t() {
    tui_input_shutdown();
    tui_video_shutdown();
  }
  ScopedTuiSession_t(const ScopedTuiSession_t&) = delete;
  auto operator=(const ScopedTuiSession_t&) -> ScopedTuiSession_t& = delete;
  ScopedTuiSession_t(ScopedTuiSession_t&&) = delete;
  auto operator=(ScopedTuiSession_t&&) -> ScopedTuiSession_t& = delete;
};

}  // namespace

TEST_CASE(
    "TUI input: with no mouse card nothing is asked of the terminal and "
    "shutdown writes nothing") {
  TuiMachine_t machine(TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  REQUIRE_FALSE(mouse_frontend_card_present());
  ScopedStdoutPipe_t out;
  ScopedTuiSession_t tui;
  tui_input_initialize();
  CHECK(out.take().empty());
  tui_input_shutdown();
  CHECK(out.take().empty());
}

TEST_CASE(
    "TUI input: initialize and shutdown write only terminal mode sequences, "
    "and every mode they set is reset") {
  TuiMachine_t machine(TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  std::string written;
  {
    ScopedStdoutPipe_t out;
    ScopedTuiSession_t tui;
    tui_input_initialize();
    tui_input_shutdown();
    written = out.take();
  }

  std::vector<ModeSequence_t> sequences;
  REQUIRE_MESSAGE(parse_mode_sequences(written, &sequences), written);
  for (size_t i = 0; i < sequences.size(); ++i) {
    if (!sequences[i].enable) {
      continue;
    }
    bool reset_later = false;
    for (size_t j = i + 1; j < sequences.size(); ++j) {
      if (!sequences[j].enable && sequences[j].mode == sequences[i].mode) {
        reset_later = true;
      }
    }
    CHECK_MESSAGE(reset_later, "mode " << sequences[i].mode << " left set");
  }
}

TEST_CASE(
    "TUI terminal: the restore string turns mouse tracking off with no "
    "enable having run, on a pseudo-terminal") {
  const int master = posix_openpt(O_RDWR | O_NOCTTY);
  REQUIRE_MESSAGE(master >= 0, strerror(errno));
  REQUIRE_MESSAGE(grantpt(master) == 0, strerror(errno));
  REQUIRE_MESSAGE(unlockpt(master) == 0, strerror(errno));
  const char* slave_name = ptsname(master);
  REQUIRE(slave_name != nullptr);
  const int slave = open(slave_name, O_RDWR | O_NOCTTY);
  REQUIRE_MESSAGE(slave >= 0, strerror(errno));

  fflush(stdout);
  const int saved_in = dup(STDIN_FILENO);
  const int saved_out = dup(STDOUT_FILENO);
  REQUIRE(saved_in >= 0);
  REQUIRE(saved_out >= 0);
  REQUIRE(dup2(slave, STDIN_FILENO) >= 0);
  REQUIRE(dup2(slave, STDOUT_FILENO) >= 0);

  const int initialized = tui_terminal_initialize();
  tui_terminal_shutdown();
  fflush(stdout);

  dup2(saved_in, STDIN_FILENO);
  dup2(saved_out, STDOUT_FILENO);
  close(saved_in);
  close(saved_out);
  close(slave);

  std::string written;
  while (true) {
    struct pollfd pfd{};
    pfd.fd = master;
    pfd.events = POLLIN;
    constexpr int wait_ms = 200;
    if (poll(&pfd, 1, wait_ms) <= 0 || (pfd.revents & POLLIN) == 0) {
      break;
    }
    std::array<char, 256> chunk{};
    const ssize_t n = read(master, chunk.data(), chunk.size());
    if (n <= 0) {
      break;
    }
    written.append(chunk.data(), static_cast<size_t>(n));
  }
  close(master);

  REQUIRE(initialized == 0);
  CHECK(written.find("\x1b[?1049h\x1b[?25l") != std::string::npos);
  CHECK(written.find("\x1b[?1016l\x1b[?1006l\x1b[?1003l\x1b[?25h\x1b[?1049l") !=
        std::string::npos);
}

#if defined(ENABLE_PERIPHERAL_MOUSE)

namespace {

constexpr int mouse_slot = 4;
constexpr uint16_t program_start = 0x0300;
constexpr uint16_t indirect_jump = 0x0320;
constexpr uint16_t entry_table = 0x12;
constexpr int entry_set_mouse = 0;
constexpr int entry_read_mouse = 2;
constexpr uint32_t firmware_cycle_cap = 200000;

// The table at $Cn12 holds the low bytes of the entries, so every call goes
// through it indirectly: LDA $Cn12+k / STA $07 / LDA #$Cn / STA $08 / LDA #a
// / LDX #$Cn / LDY #$n0 / JSR $0320, with JMP ($0007) at $0320, then a spin.
auto call_firmware(int slot, int entry, uint8_t a) -> void {
  const auto page = static_cast<uint8_t>(0xC0 + slot);
  const auto table = static_cast<uint16_t>((page << 8) + entry_table + entry);
  std::vector<uint8_t> program = {0xAD,
                                  static_cast<uint8_t>(table & 0xFF),
                                  static_cast<uint8_t>(table >> 8),
                                  0x85,
                                  0x07,
                                  0xA9,
                                  page,
                                  0x85,
                                  0x08,
                                  0xA9,
                                  a,
                                  0xA2,
                                  page,
                                  0xA0,
                                  static_cast<uint8_t>(slot << 4),
                                  0x20,
                                  static_cast<uint8_t>(indirect_jump & 0xFF),
                                  static_cast<uint8_t>(indirect_jump >> 8)};
  const auto spin = static_cast<uint16_t>(program_start + program.size());
  program.push_back(0x4C);
  program.push_back(static_cast<uint8_t>(spin & 0xFF));
  program.push_back(static_cast<uint8_t>(spin >> 8));
  TestFixtures::ScopedCore_t::poke(program_start, program.data(),
                                   program.size());
  const std::array<uint8_t, 3> jump = {0x6C, 0x07, 0x00};
  TestFixtures::ScopedCore_t::poke(indirect_jump, jump);
  TestFixtures::enter_at({program_start, 0, 0, 0});
  TestFixtures::step_until_pc(spin, firmware_cycle_cap);
  REQUIRE(cpu_get_registers()->pc == spin);
}

struct Reading_t {
  int16_t x;
  int16_t y;
  uint8_t status;
};

// READMOUSE through the table, then the slot's holes (manual p. 44).
auto read_mouse(int slot) -> Reading_t {
  peripheral_manager_think(0);
  call_firmware(slot, entry_read_mouse, 0);
  const auto n = static_cast<uint16_t>(slot);
  Reading_t reading{};
  reading.x = static_cast<int16_t>(mem[0x478 + n] | (mem[0x578 + n] << 8));
  reading.y = static_cast<int16_t>(mem[0x4F8 + n] | (mem[0x5F8 + n] << 8));
  reading.status = mem[0x778 + n];
  return reading;
}

auto mouse_in_slot_4() -> TestFixtures::ScopedTestConfig_t::Description_t {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[mouse_slot - 1] = "Mouse Interface";
  return description;
}

auto sgr(int cb, int x, int y, char final) -> std::string {
  return "\x1b[<" + std::to_string(cb) + ";" + std::to_string(x) + ";" +
         std::to_string(y) + final;
}

}  // namespace

TEST_CASE(
    "TUI input: with a mouse card the terminal is asked for any-event SGR "
    "tracking and queried for pixel reporting and the cell size, and "
    "shutdown turns all three modes off") {
  TuiMachine_t machine(mouse_in_slot_4());
  REQUIRE(mouse_frontend_card_slot() == mouse_slot);
  ScopedStdoutPipe_t out;
  ScopedTuiSession_t tui;
  tui_input_initialize();
  CHECK(out.take() == k_tracking_request);
  tui_input_shutdown();
  CHECK(out.take() == k_tracking_release);
}

TEST_CASE(
    "TUI input: Mouse Capture = 0 asks nothing of the terminal even with a "
    "card present") {
  TestFixtures::ScopedTestConfig_t::Description_t description =
      mouse_in_slot_4();
  description.extras.push_back({"Configuration", "Mouse Capture", "0"});
  TuiMachine_t machine(description);
  REQUIRE(mouse_frontend_card_present());
  REQUIRE_FALSE(mouse_frontend_capture_enabled());
  ScopedStdoutPipe_t out;
  ScopedTuiSession_t tui;
  tui_input_initialize();
  CHECK(out.take().empty());
  tui_input_shutdown();
  CHECK(out.take().empty());
}

TEST_CASE(
    "TUI input: pixel reporting is set only after the terminal reports the "
    "mode reset and a cell size is known") {
  TuiMachine_t machine(mouse_in_slot_4());
  ScopedStdoutPipe_t out;
  ScopedStdinPipe_t in;
  ScopedTuiSession_t tui;
  tui_input_initialize();
  REQUIRE(out.take() == k_tracking_request);

  SUBCASE("a reply of 2 with the cell size sets the mode") {
    in.feed(k_pixel_mode_reset_reply);
    CHECK(out.take().empty());
    in.feed(k_cell_size_reply);
    CHECK(out.take() == k_pixel_mode_set);
  }

  SUBCASE("the cell size first, then the reply of 2, sets the mode") {
    in.feed(k_cell_size_reply);
    CHECK(out.take().empty());
    in.feed(k_pixel_mode_reset_reply);
    CHECK(out.take() == k_pixel_mode_set);
  }

  SUBCASE("a reply of 0, an unknown mode, leaves cells") {
    in.feed("\x1b[?1016;0$y");
    in.feed(k_cell_size_reply);
    CHECK(out.take().empty());
  }

  SUBCASE("a reply of 4, permanently reset, leaves cells") {
    in.feed("\x1b[?1016;4$y");
    in.feed(k_cell_size_reply);
    CHECK(out.take().empty());
  }

  SUBCASE("no reply leaves cells") {
    in.feed(k_cell_size_reply);
    CHECK(out.take().empty());
  }

  SUBCASE("a reply of 1 with no cell size turns the mode off") {
    in.feed(k_pixel_mode_set_reply);
    CHECK(out.take() == "\x1b[?1016l");
  }

  SUBCASE("a reply of 1 with a cell size writes nothing and takes pixels") {
    in.feed(k_cell_size_reply);
    in.feed(k_pixel_mode_set_reply);
    CHECK(out.take().empty());
  }
}

TEST_CASE(
    "TUI input: a motion report is differenced against the previous one in "
    "cells and scaled to the box the renderer drew") {
  TuiMachine_t machine(mouse_in_slot_4());
  ScopedStdoutPipe_t out;
  ScopedStdinPipe_t in;
  ScopedTuiSession_t tui;
  tui_video_initialize();
  tui_input_initialize();
  call_firmware(mouse_slot, entry_set_mouse, 0x01);
  TuiMachine_t::show_text_80();
  REQUIRE(tui_video_picture_box().w == 80);
  REQUIRE(tui_video_picture_box().h == 24);

  // The first report has nothing to differ from.
  in.feed(sgr(35, 10, 5, 'M'));
  Reading_t reading = read_mouse(mouse_slot);
  CHECK(reading.x == 0);
  CHECK(reading.y == 0);

  // Two cells of an 80-cell box are 7 of 280 counts; one row of 24 is 8 of
  // 192.
  in.feed(sgr(35, 12, 5, 'M'));
  reading = read_mouse(mouse_slot);
  CHECK(reading.x == 7);
  CHECK(reading.y == 0);
  in.feed(sgr(35, 12, 6, 'M'));
  reading = read_mouse(mouse_slot);
  CHECK(reading.x == 7);
  CHECK(reading.y == 8);

  // A report outside the box still moves the pointer; the box is the scale,
  // not a fence.
  in.feed(sgr(35, 82, 6, 'M'));
  CHECK(read_mouse(mouse_slot).x == 252);
}

TEST_CASE(
    "TUI input: pixel reports are scaled by the cell size, the first report "
    "after the switch sends nothing, and a graphics frame changes the box") {
  TuiMachine_t machine(mouse_in_slot_4());
  ScopedStdoutPipe_t out;
  ScopedStdinPipe_t in;
  ScopedTuiSession_t tui;
  tui_video_initialize();
  tui_input_initialize();
  call_firmware(mouse_slot, entry_set_mouse, 0x01);
  TuiMachine_t::show_text_80();

  in.feed(sgr(35, 10, 5, 'M'));
  in.feed(sgr(35, 11, 5, 'M'));
  REQUIRE(read_mouse(mouse_slot).x == 3);

  in.feed(k_pixel_mode_reset_reply);
  in.feed(k_cell_size_reply);
  REQUIRE(out.take().find(k_pixel_mode_set) != std::string::npos);

  // The last cell position must not be differenced against a pixel one.
  in.feed(sgr(35, 400, 80, 'M'));
  CHECK(read_mouse(mouse_slot).x == 3);

  // 16 pixels of a 640-pixel box are 7 counts.
  in.feed(sgr(35, 416, 80, 'M'));
  CHECK(read_mouse(mouse_slot).x == 10);

  // In graphics mode the box is 64 cells, 512 pixels, wide: 16 pixels are
  // 8.75 counts, so 8 then 9.
  TuiMachine_t::show_graphics();
  REQUIRE(tui_video_picture_box().w == 64);
  in.feed(sgr(35, 432, 80, 'M'));
  CHECK(read_mouse(mouse_slot).x == 18);
  in.feed(sgr(35, 448, 80, 'M'));
  CHECK(read_mouse(mouse_slot).x == 27);
}

TEST_CASE(
    "TUI input: a left press and release are the card's button, a drag is "
    "motion with the button held and no new edge, and the other buttons do "
    "nothing") {
  TuiMachine_t machine(mouse_in_slot_4());
  ScopedStdoutPipe_t out;
  ScopedStdinPipe_t in;
  ScopedTuiSession_t tui;
  tui_video_initialize();
  (void)out.take();
  tui_input_initialize();
  REQUIRE(out.take() == k_tracking_request);
  call_firmware(mouse_slot, entry_set_mouse, 0x01);
  TuiMachine_t::show_text_80();

  in.feed(sgr(35, 10, 5, 'M'));
  in.feed(sgr(0, 10, 5, 'M'));
  Reading_t reading = read_mouse(mouse_slot);
  CHECK(reading.status == 0x80);
  CHECK(reading.x == 0);

  in.feed(sgr(32, 12, 5, 'M'));
  reading = read_mouse(mouse_slot);
  CHECK(reading.status == 0xE0);
  CHECK(reading.x == 7);

  in.feed(sgr(0, 12, 5, 'm'));
  reading = read_mouse(mouse_slot);
  CHECK(reading.status == 0x40);
  CHECK(reading.x == 7);

  in.feed(sgr(1, 12, 5, 'M'));
  in.feed(sgr(2, 12, 5, 'M'));
  in.feed(sgr(64, 12, 5, 'M'));
  reading = read_mouse(mouse_slot);
  CHECK(reading.status == 0x00);
  CHECK(reading.x == 7);
  CHECK(out.take().empty());
}
#endif
