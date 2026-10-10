// SPDX-License-Identifier: GPL-2.0-only
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/KeyboardTranslator.h"
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

// stdout is flushed on the way in and out so doctest's own report stays on the
// real descriptor.
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

// The read end is non-blocking, as a raw-mode terminal with VMIN 0 is.
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

  ~TuiMachine_t() = default;
  TuiMachine_t(const TuiMachine_t&) = delete;
  auto operator=(const TuiMachine_t&) -> TuiMachine_t& = delete;
  TuiMachine_t(TuiMachine_t&&) = delete;
  auto operator=(TuiMachine_t&&) -> TuiMachine_t& = delete;

  // On a pipe the terminal size falls back to 80 x 24 and nothing is written.
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

// Declared after the pipes so it goes first: the disables then land in the
// pipes and not in doctest's report.
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

#ifdef ENABLE_PERIPHERAL_MOUSE

namespace {

constexpr int mouse_slot = 4;
constexpr uint16_t program_start = 0x0300;
constexpr uint16_t indirect_jump = 0x0320;
constexpr uint16_t entry_table = 0x12;
constexpr int entry_set_mouse = 0;
constexpr int entry_read_mouse = 2;
constexpr int entry_pos_mouse = 4;
constexpr int entry_clamp_mouse = 5;
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

auto poke_byte(uint16_t at, uint8_t value) -> void {
  const std::array<uint8_t, 1> byte = {value};
  TestFixtures::ScopedCore_t::poke(at, byte);
}

// CLAMPMOUSE takes low minimum, low maximum, high minimum, high maximum from
// the slot-0 holes (manual p. 48); here the hires screen, 0..279 by 0..191.
auto clamp_to_hires(int slot) -> void {
  poke_byte(0x478, 0x00);
  poke_byte(0x4F8, 0x17);
  poke_byte(0x578, 0x00);
  poke_byte(0x5F8, 0x01);
  call_firmware(slot, entry_clamp_mouse, 0);
  poke_byte(0x4F8, 0xBF);
  poke_byte(0x5F8, 0x00);
  call_firmware(slot, entry_clamp_mouse, 1);
}

// POSMOUSE takes the position from the slot's holes (manual p. 47).
auto pos_mouse(int slot, int16_t x, int16_t y) -> void {
  const auto n = static_cast<uint16_t>(slot);
  poke_byte(0x478 + n, static_cast<uint8_t>(x & 0xFF));
  poke_byte(0x578 + n, static_cast<uint8_t>(static_cast<uint16_t>(x) >> 8));
  poke_byte(0x4F8 + n, static_cast<uint8_t>(y & 0xFF));
  poke_byte(0x5F8 + n, static_cast<uint8_t>(static_cast<uint16_t>(y) >> 8));
  call_firmware(slot, entry_pos_mouse, 0);
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

// The 80-column text screen fills the 80 by 24 fallback terminal exactly, so a
// one-based report column c is offset c - 1 of the 79 steps across the box and
// row r is offset r - 1 of 23 down it.
static auto require_text_box_fills_terminal() -> void {
  const MousePictureRect box = tui_video_picture_box();
  REQUIRE(box.x == 0);
  REQUIRE(box.y == 0);
  REQUIRE(box.w == 80);
  REQUIRE(box.h == 24);
}

TEST_CASE(
    "TUI input: a motion report puts the card's pointer where the host's is "
    "within the box the renderer drew, in cells") {
  TuiMachine_t machine(mouse_in_slot_4());
  ScopedStdoutPipe_t out;
  ScopedStdinPipe_t in;
  ScopedTuiSession_t tui;
  tui_video_initialize();
  tui_input_initialize();
  call_firmware(mouse_slot, entry_set_mouse, 0x01);
  TuiMachine_t::show_text_80();
  require_text_box_fills_terminal();

  // Under the power-on 0..1023 window the box's last cell is the far corner
  // and its first the near one.
  in.feed(sgr(35, 80, 24, 'M'));
  Reading_t reading = read_mouse(mouse_slot);
  CHECK(reading.x == 1023);
  CHECK(reading.y == 1023);
  in.feed(sgr(35, 1, 1, 'M'));
  reading = read_mouse(mouse_slot);
  CHECK(reading.x == 0);
  CHECK(reading.y == 0);
  // Column 41 is offset 40: 40 * 1023 / 79 = 518.0, so 518; row 13 is offset
  // 12: 12 * 1023 / 23 = 533.7, so 534.
  in.feed(sgr(35, 41, 13, 'M'));
  reading = read_mouse(mouse_slot);
  CHECK(reading.x == 518);
  CHECK(reading.y == 534);

  // Column 40 is offset 39: 39 * 279 / 79 = 137.7, so 138; row 12 is offset
  // 11: 11 * 191 / 23 = 91.3, so 91.
  clamp_to_hires(mouse_slot);
  in.feed(sgr(35, 40, 12, 'M'));
  reading = read_mouse(mouse_slot);
  CHECK(reading.x == 138);
  CHECK(reading.y == 91);

  // A report past the box's edge holds the pointer at the window's edge.
  in.feed(sgr(35, 82, 12, 'M'));
  reading = read_mouse(mouse_slot);
  CHECK(reading.x == 279);
  CHECK(reading.y == 91);
}

TEST_CASE(
    "TUI input: pixel reports place the pointer within a cell, and a graphics "
    "frame changes the box") {
  TuiMachine_t machine(mouse_in_slot_4());
  ScopedStdoutPipe_t out;
  ScopedStdinPipe_t in;
  ScopedTuiSession_t tui;
  tui_video_initialize();
  tui_input_initialize();
  call_firmware(mouse_slot, entry_set_mouse, 0x01);
  clamp_to_hires(mouse_slot);
  TuiMachine_t::show_text_80();
  require_text_box_fills_terminal();

  in.feed(k_pixel_mode_reset_reply);
  in.feed(k_cell_size_reply);
  REQUIRE(out.take().find(k_pixel_mode_set) != std::string::npos);

  // In 8 by 16 pixel cells the box is 640 by 384. One cell is 3.5 counts;
  // pixel 3 is offset 2 of 639: 2 * 279 / 639 = 0.87, so 1; pixel 5 is 1.75,
  // so 2; pixel 8 is 3.06, so 3.
  in.feed(sgr(35, 3, 1, 'M'));
  CHECK(read_mouse(mouse_slot).x == 1);
  in.feed(sgr(35, 5, 1, 'M'));
  CHECK(read_mouse(mouse_slot).x == 2);
  in.feed(sgr(35, 8, 1, 'M'));
  CHECK(read_mouse(mouse_slot).x == 3);
  in.feed(sgr(35, 640, 384, 'M'));
  Reading_t reading = read_mouse(mouse_slot);
  CHECK(reading.x == 279);
  CHECK(reading.y == 191);

  // Pixel 100 is offset 99 of the text box: 99 * 279 / 639 = 43.2, so 43.
  in.feed(sgr(35, 100, 1, 'M'));
  CHECK(read_mouse(mouse_slot).x == 43);

  // The graphics box is 64 cells from cell 8, 512 pixels from pixel 64, so
  // the same host pixel is offset 35 of 511: 35 * 279 / 511 = 19.1, so 19.
  TuiMachine_t::show_graphics();
  REQUIRE(tui_video_picture_box().x == 8);
  REQUIRE(tui_video_picture_box().w == 64);
  in.feed(sgr(35, 100, 1, 'M'));
  CHECK(read_mouse(mouse_slot).x == 19);
}

TEST_CASE(
    "TUI input: a program's POSMOUSE holds until the next report puts the "
    "pointer back under the host's") {
  TuiMachine_t machine(mouse_in_slot_4());
  ScopedStdoutPipe_t out;
  ScopedStdinPipe_t in;
  ScopedTuiSession_t tui;
  tui_video_initialize();
  tui_input_initialize();
  call_firmware(mouse_slot, entry_set_mouse, 0x01);
  clamp_to_hires(mouse_slot);
  TuiMachine_t::show_text_80();
  require_text_box_fills_terminal();

  in.feed(sgr(35, 40, 12, 'M'));
  Reading_t reading = read_mouse(mouse_slot);
  REQUIRE(reading.x == 138);
  REQUIRE(reading.y == 91);

  pos_mouse(mouse_slot, 20, 30);
  reading = read_mouse(mouse_slot);
  CHECK(reading.x == 20);
  CHECK(reading.y == 30);

  // The host pointer has not moved, so the same report is repeated.
  in.feed(sgr(35, 40, 12, 'M'));
  reading = read_mouse(mouse_slot);
  CHECK(reading.x == 138);
  CHECK(reading.y == 91);
}

TEST_CASE("TUI input: with the mouse off a motion report moves nothing") {
  TuiMachine_t machine(mouse_in_slot_4());
  ScopedStdoutPipe_t out;
  ScopedStdinPipe_t in;
  ScopedTuiSession_t tui;
  tui_video_initialize();
  tui_input_initialize();
  TuiMachine_t::show_text_80();
  require_text_box_fills_terminal();

  in.feed(sgr(35, 80, 24, 'M'));
  Reading_t reading = read_mouse(mouse_slot);
  CHECK(reading.x == 0);
  CHECK(reading.y == 0);
  CHECK(reading.status == 0x00);

  call_firmware(mouse_slot, entry_set_mouse, 0x01);
  in.feed(sgr(35, 80, 24, 'M'));
  reading = read_mouse(mouse_slot);
  CHECK(reading.x == 1023);
  CHECK(reading.y == 1023);
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
  require_text_box_fills_terminal();

  // Column 10 is offset 9: 9 * 1023 / 79 = 116.5, so 117. The read clears the
  // movement bit so the press is read alone.
  in.feed(sgr(35, 10, 5, 'M'));
  Reading_t reading = read_mouse(mouse_slot);
  REQUIRE(reading.x == 117);
  in.feed(sgr(0, 10, 5, 'M'));
  reading = read_mouse(mouse_slot);
  CHECK(reading.status == 0x80);
  CHECK(reading.x == 117);

  // Column 12 is offset 11: 11 * 1023 / 79 = 142.4, so 142.
  in.feed(sgr(32, 12, 5, 'M'));
  reading = read_mouse(mouse_slot);
  CHECK(reading.status == 0xE0);
  CHECK(reading.x == 142);

  in.feed(sgr(0, 12, 5, 'm'));
  reading = read_mouse(mouse_slot);
  CHECK(reading.status == 0x40);
  CHECK(reading.x == 142);

  in.feed(sgr(1, 12, 5, 'M'));
  in.feed(sgr(2, 12, 5, 'M'));
  in.feed(sgr(64, 12, 5, 'M'));
  reading = read_mouse(mouse_slot);
  CHECK(reading.status == 0x00);
  CHECK(reading.x == 142);
  CHECK(out.take().empty());
}
#endif

#ifdef ENABLE_PERIPHERAL_KEYBOARD

namespace {

constexpr uint16_t addr_keyboard_data = 0xC000;
constexpr uint16_t addr_keyboard_strobe = 0xC010;
constexpr uint16_t addr_pushbutton0 = 0xC061;
constexpr uint8_t bit7 = 0x80;
constexpr uint8_t ascii_esc = 0x1B;
constexpr uint8_t last_control_byte = 0x1F;

// The model is process-wide and a harness-built machine leaves it behind.
struct Model_t {
  Apple2Type saved = current_apple2_type;
  explicit Model_t(Apple2Type type) { current_apple2_type = type; }
  ~Model_t() { current_apple2_type = saved; }
  Model_t(const Model_t&) = delete;
  auto operator=(const Model_t&) -> Model_t& = delete;
  Model_t(Model_t&&) = delete;
  auto operator=(Model_t&&) -> Model_t& = delete;
};

// A terminal reports no caps state, so its caps is the emulated kind, down to
// start with; whatever the session writes to the terminal lands in a pipe.
struct TuiKeyboard_t {
  Model_t model;
  TuiMachine_t machine;
  ScopedStdoutPipe_t out;
  ScopedStdinPipe_t in;
  ScopedTuiSession_t tui;

  explicit TuiKeyboard_t(
      const TestFixtures::ScopedTestConfig_t::Description_t& description,
      Apple2Type type = A2TYPE_APPLE2EENHANCED)
      : model(type), machine(description) {
    keyboard_set_caps(true);
    keyboard_set_caps_mode(caps_mode_emulated);
    keyboard_set_mapping_mode(kbd_mode_symbolic);
    keyboard_set_layout(0);
    linapple_set_rocker_switch(false);
    frontend_update_keyboard_mapping();
    tui_input_initialize();
    settle();
  }

  ~TuiKeyboard_t() {
    linapple_set_key_release_all();
    settle();
    keyboard_set_caps(true);
    keyboard_set_caps_mode(caps_mode_host);
  }

  TuiKeyboard_t(const TuiKeyboard_t&) = delete;
  auto operator=(const TuiKeyboard_t&) -> TuiKeyboard_t& = delete;
  TuiKeyboard_t(TuiKeyboard_t&&) = delete;
  auto operator=(TuiKeyboard_t&&) -> TuiKeyboard_t& = delete;

  // A think drains the command queue, as a running machine does once a frame.
  static auto settle() -> void { peripheral_manager_think(0); }

  auto type(const std::string& bytes) -> void {
    in.feed(bytes);
    settle();
  }

  // A poll with nothing to read is the next frame's poll.
  static auto next_poll() -> void {
    tui_input_poll();
    settle();
  }

  static auto latch() -> uint8_t {
    return io_map_dispatch(0, addr_keyboard_data, 0, 0, 0);
  }

  static auto any_key_down() -> bool {
    return (io_map_dispatch(0, addr_keyboard_strobe, 0, 0, 0) & bit7) != 0;
  }

  static auto clear_strobe() -> void {
    (void)io_map_dispatch(0, addr_keyboard_strobe, 0, 0, 0);
  }

  static auto pushbutton(uint8_t line) -> int {
    const auto addr = static_cast<uint16_t>(addr_pushbutton0 + line);
    return (io_map_dispatch(0, addr, 0, 0, 0) & bit7) != 0 ? 1 : 0;
  }
};

volatile sig_atomic_t g_sigint_count = 0;

auto count_sigint(int signal) -> void {
  (void)signal;
  ++g_sigint_count;
}

// doctest installs signal handlers of its own, so SIGINT is taken over for
// the case and handed back.
struct ScopedSigintCounter_t {
  struct sigaction saved{};

  ScopedSigintCounter_t() {
    g_sigint_count = 0;
    struct sigaction action{};
    action.sa_handler = count_sigint;
    sigemptyset(&action.sa_mask);
    REQUIRE(sigaction(SIGINT, &action, &saved) == 0);
  }

  ~ScopedSigintCounter_t() { sigaction(SIGINT, &saved, nullptr); }

  ScopedSigintCounter_t(const ScopedSigintCounter_t&) = delete;
  auto operator=(const ScopedSigintCounter_t&)
      -> ScopedSigintCounter_t& = delete;
  ScopedSigintCounter_t(ScopedSigintCounter_t&&) = delete;
  auto operator=(ScopedSigintCounter_t&&) -> ScopedSigintCounter_t& = delete;
};

auto ii_plus() -> TestFixtures::ScopedTestConfig_t::Description_t {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.machine_type =
      TestFixtures::ScopedTestConfig_t::machine_apple2_plus;
  return description;
}

}  // namespace

TEST_CASE(
    "TUI keys: a byte types its key in upper case with the emulated caps down "
    "and in lower case with it up, held through the poll that read it and let "
    "go at the next") {
  TuiKeyboard_t terminal(TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  REQUIRE(keyboard_get_caps());
  REQUIRE_FALSE(TuiKeyboard_t::any_key_down());

  terminal.type("a");
  CHECK(TuiKeyboard_t::latch() == 0xC1);
  CHECK(TuiKeyboard_t::any_key_down());
  TuiKeyboard_t::next_poll();
  CHECK_FALSE(TuiKeyboard_t::any_key_down());
  TuiKeyboard_t::clear_strobe();
  CHECK(TuiKeyboard_t::latch() == 0x41);

  keyboard_set_caps(false);
  terminal.type("a");
  CHECK(TuiKeyboard_t::latch() == 0xE1);
  TuiKeyboard_t::next_poll();
  TuiKeyboard_t::clear_strobe();
  terminal.type("A");
  CHECK(TuiKeyboard_t::latch() == 0xC1);
  TuiKeyboard_t::next_poll();
}

TEST_CASE(
    "TUI keys: every control byte but ESC reaches the Apple as itself, Ctrl-C "
    "included and raising no SIGINT, Return is $0D, the Backspace key's 0x7F "
    "is the left arrow and the Delete key's CSI 3 ~ is $7F") {
  TuiKeyboard_t terminal(TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  ScopedSigintCounter_t sigint;

  terminal.type("\x03");
  CHECK(TuiKeyboard_t::latch() == 0x83);
  CHECK(g_sigint_count == 0);
  TuiKeyboard_t::next_poll();
  TuiKeyboard_t::clear_strobe();

  for (int byte = 0x01; byte <= last_control_byte; ++byte) {
    if (byte == ascii_esc) {
      continue;
    }
    CAPTURE(byte);
    terminal.type(std::string(1, static_cast<char>(byte)));
    CHECK(TuiKeyboard_t::latch() == (bit7 | byte));
    CHECK(TuiKeyboard_t::any_key_down());
    TuiKeyboard_t::next_poll();
    CHECK_FALSE(TuiKeyboard_t::any_key_down());
    TuiKeyboard_t::clear_strobe();
  }
  CHECK(g_sigint_count == 0);

  terminal.type("\r");
  CHECK(TuiKeyboard_t::latch() == 0x8D);
  TuiKeyboard_t::next_poll();
  TuiKeyboard_t::clear_strobe();
  terminal.type("\x7f");
  CHECK(TuiKeyboard_t::latch() == 0x88);
  TuiKeyboard_t::next_poll();
  TuiKeyboard_t::clear_strobe();
  terminal.type("\x1b[3~");
  CHECK(TuiKeyboard_t::latch() == 0xFF);
  TuiKeyboard_t::next_poll();
}

TEST_CASE(
    "TUI keys: a lone ESC types $1B, and ESC before a key or a key with its "
    "eighth bit set is Open Apple held around that key, both let go at the "
    "next poll") {
  TuiKeyboard_t terminal(TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  REQUIRE(TuiKeyboard_t::pushbutton(0) == 0);

  terminal.type("\x1b");
  CHECK(TuiKeyboard_t::latch() == 0x9B);
  CHECK(TuiKeyboard_t::pushbutton(0) == 0);
  TuiKeyboard_t::next_poll();
  TuiKeyboard_t::clear_strobe();

  terminal.type(
      "\x1b"
      "a");
  CHECK(TuiKeyboard_t::latch() == 0xC1);
  CHECK(TuiKeyboard_t::any_key_down());
  CHECK(TuiKeyboard_t::pushbutton(0) == 1);
  CHECK(TuiKeyboard_t::pushbutton(1) == 0);
  TuiKeyboard_t::next_poll();
  CHECK_FALSE(TuiKeyboard_t::any_key_down());
  CHECK(TuiKeyboard_t::pushbutton(0) == 0);
  TuiKeyboard_t::clear_strobe();

  terminal.type("\xe1");
  CHECK(TuiKeyboard_t::latch() == 0xC1);
  CHECK(TuiKeyboard_t::pushbutton(0) == 1);
  TuiKeyboard_t::next_poll();
  CHECK(TuiKeyboard_t::pushbutton(0) == 0);
  TuiKeyboard_t::clear_strobe();

  // Alt+Backspace is Open Apple with the left arrow.
  terminal.type("\x1b\x7f");
  CHECK(TuiKeyboard_t::latch() == 0x88);
  CHECK(TuiKeyboard_t::pushbutton(0) == 1);
  TuiKeyboard_t::next_poll();
  CHECK(TuiKeyboard_t::pushbutton(0) == 0);
  TuiKeyboard_t::clear_strobe();

  // A UTF-8 terminal sends Alt as the ESC prefix, so a valid UTF-8 sequence is
  // a character the Apple cannot type and reaches nothing.
  terminal.type("\xc3\xa9");
  CHECK(TuiKeyboard_t::latch() == 0x08);
  CHECK_FALSE(TuiKeyboard_t::any_key_down());
  CHECK(TuiKeyboard_t::pushbutton(0) == 0);
  TuiKeyboard_t::next_poll();
  CHECK(TuiKeyboard_t::pushbutton(0) == 0);

  // A high byte that begins no UTF-8 sequence is the eighth-bit Alt form:
  // $E1 before an ASCII byte is Open Apple with a, then that byte.
  terminal.type(
      "\xe1"
      "b");
  CHECK(TuiKeyboard_t::latch() == 0xC2);
  CHECK(TuiKeyboard_t::any_key_down());
  CHECK(TuiKeyboard_t::pushbutton(0) == 1);
  TuiKeyboard_t::next_poll();
  CHECK_FALSE(TuiKeyboard_t::any_key_down());
  CHECK(TuiKeyboard_t::pushbutton(0) == 0);
}

TEST_CASE(
    "TUI keys: Shift+F6 toggles the rocker switch on a //e and leaves it "
    "alone on a II Plus") {
  SUBCASE("Enhanced //e") {
    TuiKeyboard_t terminal(
        TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
    REQUIRE_FALSE(linapple_get_rocker_switch());
    terminal.type("\x1b[17;2~");
    CHECK(linapple_get_rocker_switch());
    terminal.type("\x1b[17;2~");
    CHECK_FALSE(linapple_get_rocker_switch());
    CHECK_FALSE(TuiKeyboard_t::any_key_down());
  }

  SUBCASE("II Plus") {
    TuiKeyboard_t terminal(ii_plus(), A2TYPE_APPLE2PLUS);
    REQUIRE_FALSE(linapple_get_rocker_switch());
    terminal.type("\x1b[17;2~");
    CHECK_FALSE(linapple_get_rocker_switch());
  }
}

#endif
