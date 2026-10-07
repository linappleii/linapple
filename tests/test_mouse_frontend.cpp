// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/MouseFrontend.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

constexpr int first_slot = 1;
constexpr int last_slot = 7;
}  // namespace

TEST_CASE(
    "Mouse frontend: a machine with no mouse card answers no slot's query") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);

  for (int slot = first_slot; slot <= last_slot; ++slot) {
    uint8_t active = 0;
    size_t size = sizeof(active);
    CHECK(peripheral_query_by_id(slot, "linapple.mouse", mouse_query_is_active,
                                 &active, &size) == peripheral_error);
  }
}

TEST_CASE("Mouse frontend: with no card the probe caches no slot") {
  TestFixtures::ScopedTestConfig_t config(
      TestFixtures::ScopedTestConfig_t::enhanced_2e_only());
  TestFixtures::ScopedCore_t core(config);

  mouse_frontend_initialize();
  CHECK(mouse_frontend_card_slot() == 0);
  CHECK_FALSE(mouse_frontend_card_present());
  mouse_frontend_motion(10, 10, 560, 384);
  mouse_frontend_follow(10, 10, {0, 0, 80, 24});
  mouse_frontend_button(true);
  peripheral_manager_think(0);
}

#if defined(ENABLE_PERIPHERAL_MOUSE)

namespace {

constexpr size_t frame_size = 92;
constexpr size_t frame_position_x = 8;
constexpr size_t frame_position_y = 12;
constexpr size_t frame_min_x = 16;
constexpr size_t frame_max_x = 20;
constexpr size_t frame_min_y = 24;
constexpr size_t frame_max_y = 28;
constexpr size_t frame_parser_out_len = 52;
constexpr size_t frame_orb = 57;
constexpr size_t frame_ddrb = 59;
constexpr size_t frame_port_b_shadow = 73;
constexpr size_t frame_mode = 74;
constexpr size_t frame_status = 76;
constexpr size_t frame_button = 79;

using Frame_t = std::array<uint8_t, frame_size>;

auto read_frame(int slot) -> Frame_t {
  Frame_t frame{};
  size_t size = frame.size();
  peripheral_save_state(slot, frame.data(), &size);
  REQUIRE(size == frame.size());
  return frame;
}

auto word_at(const Frame_t& frame, size_t offset) -> int16_t {
  return static_cast<int16_t>(frame.at(offset) | (frame.at(offset + 1) << 8));
}

// The loader is the one public path that turns tracking on without running
// the firmware.
auto turn_tracking_on(int slot) -> void {
  Frame_t frame{};
  frame.at(0) = MOUSE_STATE_VERSION;
  frame.at(4) = frame_size;
  frame.at(frame_max_x) = 0xFF;
  frame.at(frame_max_x + 1) = 0x03;
  frame.at(frame_max_y) = 0xFF;
  frame.at(frame_max_y + 1) = 0x03;
  frame.at(frame_parser_out_len) = 1;
  // Port B at rest: PB6 answers the lowered PB4.
  frame.at(frame_orb) = 0x40;
  frame.at(frame_ddrb) = 0x3E;
  frame.at(frame_port_b_shadow) = 0x40;
  frame.at(frame_mode) = 1;
  REQUIRE(peripheral_load_state(slot, frame.data(), frame.size()) ==
          peripheral_ok);
}

auto put_word(Frame_t* frame, size_t offset, int16_t value) -> void {
  const auto bits = static_cast<uint16_t>(value);
  frame->at(offset) = static_cast<uint8_t>(bits & 0xFF);
  frame->at(offset + 1) = static_cast<uint8_t>(bits >> 8);
}

struct CardState_t {
  uint8_t mode;
  int16_t x;
  int16_t y;
  int16_t min_x;
  int16_t max_x;
  int16_t min_y;
  int16_t max_y;
};

// The mode, the counters and the clamps set over the tracking frame; like
// POSMOUSE, a position outside the window is taken as given.
auto load_card(int slot, const CardState_t& state) -> void {
  turn_tracking_on(slot);
  Frame_t frame = read_frame(slot);
  put_word(&frame, frame_position_x, state.x);
  put_word(&frame, frame_position_y, state.y);
  put_word(&frame, frame_min_x, state.min_x);
  put_word(&frame, frame_max_x, state.max_x);
  put_word(&frame, frame_min_y, state.min_y);
  put_word(&frame, frame_max_y, state.max_y);
  frame.at(frame_mode) = state.mode;
  REQUIRE(peripheral_load_state(slot, frame.data(), frame.size()) ==
          peripheral_ok);
}

auto query_position(int slot) -> MousePositionReport_t {
  MousePositionReport_t report{};
  size_t size = sizeof(report);
  REQUIRE(peripheral_query_by_id(slot, "linapple.mouse", mouse_query_position,
                                 &report, &size) == peripheral_ok);
  REQUIRE(size == sizeof(report));
  return report;
}

auto position(int slot) -> std::array<int16_t, 2> {
  peripheral_manager_think(0);
  const Frame_t frame = read_frame(slot);
  return {word_at(frame, frame_position_x), word_at(frame, frame_position_y)};
}

}  // namespace

TEST_CASE(
    "Mouse frontend: the probe finds the card in slot 5 and a button reaches "
    "it") {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[4] = "Mouse Interface";
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);

  mouse_frontend_initialize();
  CHECK(mouse_frontend_card_slot() == 5);
  CHECK(mouse_frontend_card_present());

  mouse_frontend_button(true);
  peripheral_manager_think(0);
  CHECK(read_frame(5).at(frame_button) == 1);
}

TEST_CASE(
    "Mouse frontend: host motion reaches the card as one count per hires "
    "pixel of the picture, the remainder carried with its sign") {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[3] = "Mouse Interface";
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);
  mouse_frontend_initialize();
  REQUIRE(mouse_frontend_card_slot() == 4);
  turn_tracking_on(4);
  REQUIRE(position(4) == std::array<int16_t, 2>{0, 0});

  SUBCASE("a 560 x 384 picture: two pixels are one count, one is half") {
    mouse_frontend_motion(2, 0, 560, 384);
    CHECK(position(4) == std::array<int16_t, 2>{1, 0});
    mouse_frontend_motion(1, 0, 560, 384);
    CHECK(position(4) == std::array<int16_t, 2>{1, 0});
    mouse_frontend_motion(1, 0, 560, 384);
    CHECK(position(4) == std::array<int16_t, 2>{2, 0});
    mouse_frontend_motion(0, 2, 560, 384);
    CHECK(position(4) == std::array<int16_t, 2>{2, 1});
    mouse_frontend_motion(0, 1, 560, 384);
    CHECK(position(4) == std::array<int16_t, 2>{2, 1});
    mouse_frontend_motion(0, 1, 560, 384);
    CHECK(position(4) == std::array<int16_t, 2>{2, 2});
  }

  SUBCASE("a 1120 x 768 picture: four pixels are one count") {
    mouse_frontend_motion(4, 4, 1120, 768);
    CHECK(position(4) == std::array<int16_t, 2>{1, 1});
    mouse_frontend_motion(3, 3, 1120, 768);
    CHECK(position(4) == std::array<int16_t, 2>{1, 1});
    mouse_frontend_motion(1, 1, 1120, 768);
    CHECK(position(4) == std::array<int16_t, 2>{2, 2});
  }

  SUBCASE("a negative remainder keeps its sign") {
    mouse_frontend_motion(100, 0, 560, 384);
    CHECK(position(4) == std::array<int16_t, 2>{50, 0});
    // -3 pixels are -1.5 counts: -1 sent, one pixel's worth left over, so
    // the next two pixels only cancel it and the third completes a count.
    mouse_frontend_motion(-3, 0, 560, 384);
    CHECK(position(4) == std::array<int16_t, 2>{49, 0});
    mouse_frontend_motion(1, 0, 560, 384);
    CHECK(position(4) == std::array<int16_t, 2>{49, 0});
    mouse_frontend_motion(1, 0, 560, 384);
    CHECK(position(4) == std::array<int16_t, 2>{49, 0});
    mouse_frontend_motion(1, 0, 560, 384);
    CHECK(position(4) == std::array<int16_t, 2>{50, 0});
  }

  SUBCASE("a picture of no size sends nothing") {
    mouse_frontend_motion(100, 100, 0, 0);
    CHECK(position(4) == std::array<int16_t, 2>{0, 0});
  }
}

TEST_CASE(
    "Mouse frontend: the position query answers the counters, the clamp "
    "window and whether motion is on") {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[3] = "Mouse Interface";
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);
  mouse_frontend_initialize();
  REQUIRE(mouse_frontend_card_slot() == 4);

  load_card(4, {1, 50, -60, 10, 270, -20, 180});
  MousePositionReport_t report = query_position(4);
  CHECK(report.x == 50);
  CHECK(report.y == -60);
  CHECK(report.min_x == 10);
  CHECK(report.max_x == 270);
  CHECK(report.min_y == -20);
  CHECK(report.max_y == 180);
  CHECK(report.tracking == 1);

  load_card(4, {0, 0, 0, 0, 1023, 0, 1023});
  report = query_position(4);
  CHECK(report.x == 0);
  CHECK(report.max_x == 1023);
  CHECK(report.tracking == 0);
}

TEST_CASE(
    "Mouse frontend: the host pointer's place in the picture puts the card's "
    "pointer at the same place in its clamp window") {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[3] = "Mouse Interface";
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);
  mouse_frontend_initialize();
  REQUIRE(mouse_frontend_card_slot() == 4);
  constexpr MousePictureRect_t text_box{0, 0, 80, 24};

  SUBCASE("a 0..279 by 0..191 window across an 80 by 24 box") {
    load_card(4, {1, 0, 0, 0, 279, 0, 191});
    // Cell column 40 is offset 39 of the 79 steps from the first cell to the
    // last: 39 * 279 / 79 = 137.7, so 138. Row 12 is offset 11 of 23:
    // 11 * 191 / 23 = 91.3, so 91.
    mouse_frontend_follow(39, 11, text_box);
    CHECK(position(4) == std::array<int16_t, 2>{138, 91});
    // The first cell is the window's low edge and the last its high edge.
    mouse_frontend_follow(0, 0, text_box);
    CHECK(position(4) == std::array<int16_t, 2>{0, 0});
    mouse_frontend_follow(79, 23, text_box);
    CHECK(position(4) == std::array<int16_t, 2>{279, 191});
  }

  SUBCASE("the power-on 0..1023 window: the box's far edge is 1023") {
    turn_tracking_on(4);
    mouse_frontend_follow(79, 23, text_box);
    CHECK(position(4) == std::array<int16_t, 2>{1023, 1023});
    // Offset 40 of 79: 40 * 1023 / 79 = 518.0, so 518; offset 12 of 23:
    // 12 * 1023 / 23 = 533.7, so 534.
    mouse_frontend_follow(40, 12, text_box);
    CHECK(position(4) == std::array<int16_t, 2>{518, 534});
  }

  SUBCASE("pixels place the pointer within a cell") {
    load_card(4, {1, 0, 0, 0, 279, 0, 191});
    // The same box in 8 by 16 pixel cells. One cell is 3.5 counts; pixel
    // offset 2 is 2 * 279 / 639 = 0.87, so 1, offset 4 is 1.75, so 2, and
    // offset 7 is 3.06, so 3.
    constexpr MousePictureRect_t pixel_box{0, 0, 640, 384};
    mouse_frontend_follow(2, 0, pixel_box);
    CHECK(position(4) == std::array<int16_t, 2>{1, 0});
    mouse_frontend_follow(4, 0, pixel_box);
    CHECK(position(4) == std::array<int16_t, 2>{2, 0});
    mouse_frontend_follow(7, 0, pixel_box);
    CHECK(position(4) == std::array<int16_t, 2>{3, 0});
    mouse_frontend_follow(639, 383, pixel_box);
    CHECK(position(4) == std::array<int16_t, 2>{279, 191});
  }

  SUBCASE("a pointer outside the box is held at the window's edge") {
    load_card(4, {1, 0, 0, 100, 200, 50, 150});
    constexpr MousePictureRect_t offset_box{10, 2, 80, 24};
    mouse_frontend_follow(5, 0, offset_box);
    CHECK(position(4) == std::array<int16_t, 2>{100, 50});
    mouse_frontend_follow(300, 100, offset_box);
    CHECK(position(4) == std::array<int16_t, 2>{200, 150});
    // Offset 40 of 79 across 100..200: 100 + 40 * 100 / 79 = 150.6, so 151;
    // offset 12 of 23 across 50..150: 50 + 12 * 100 / 23 = 102.2, so 102.
    mouse_frontend_follow(50, 14, offset_box);
    CHECK(position(4) == std::array<int16_t, 2>{151, 102});
  }

  SUBCASE("a program's POSMOUSE holds only until the next host position") {
    load_card(4, {1, 250, 10, 0, 279, 0, 191});
    mouse_frontend_follow(39, 11, text_box);
    CHECK(position(4) == std::array<int16_t, 2>{138, 91});
    // A position outside the window, as POSMOUSE may leave one.
    load_card(4, {1, 300, -5, 0, 279, 0, 191});
    mouse_frontend_follow(39, 11, text_box);
    CHECK(position(4) == std::array<int16_t, 2>{138, 91});
  }

  SUBCASE("with motion off nothing is sent") {
    load_card(4, {0, 0, 0, 0, 279, 0, 191});
    mouse_frontend_follow(79, 23, text_box);
    CHECK(position(4) == std::array<int16_t, 2>{0, 0});
    CHECK(read_frame(4).at(frame_status) == 0);
  }
}
#endif

namespace {

auto decode(const char* text, MouseSgrEvent_t* out) -> bool {
  return mouse_frontend_sgr_decode(reinterpret_cast<const uint8_t*>(text),
                                   strlen(text), out);
}

}  // namespace

TEST_CASE(
    "Mouse frontend: the SGR decoder tells a press, a release, a motion and "
    "a drag apart and refuses a short report") {
  MouseSgrEvent_t event{};

  REQUIRE(decode("\x1b[<0;10;5M", &event));
  CHECK(event.button == 0);
  CHECK(event.pressed);
  CHECK_FALSE(event.released);
  CHECK_FALSE(event.motion);
  CHECK_FALSE(event.wheel);
  CHECK(event.x == 10);
  CHECK(event.y == 5);

  REQUIRE(decode("\x1b[<0;10;5m", &event));
  CHECK(event.button == 0);
  CHECK_FALSE(event.pressed);
  CHECK(event.released);
  CHECK_FALSE(event.motion);

  REQUIRE(decode("\x1b[<35;12;5M", &event));
  CHECK(event.button == 3);
  CHECK_FALSE(event.pressed);
  CHECK(event.motion);
  CHECK(event.x == 12);
  CHECK(event.y == 5);

  // Under any-event tracking every report of a drag carries the held button
  // with the motion bit; a press here would re-press the button every cell.
  REQUIRE(decode("\x1b[<32;12;5M", &event));
  CHECK(event.button == 0);
  CHECK(event.motion);
  CHECK_FALSE(event.pressed);
  CHECK_FALSE(event.released);

  REQUIRE(decode("\x1b[<64;12;5M", &event));
  CHECK(event.wheel);
  CHECK_FALSE(event.pressed);

  CHECK_FALSE(decode("\x1b[<0;10M", &event));
  CHECK_FALSE(decode("\x1b[<0;10;5;7M", &event));
  CHECK_FALSE(decode("\x1b[0;10;5M", &event));
  CHECK_FALSE(decode("\x1b[<0;10;5", &event));
  CHECK_FALSE(decode("\x1b[<0;;5M", &event));
  CHECK_FALSE(decode("\x1b[<0;1234567;5M", &event));
}

TEST_CASE(
    "Mouse frontend: the DECRPM and cell-size decoders read xterm's replies "
    "and nothing else") {
  int mode = 0;
  int setting = 0;
  const char* reset = "\x1b[?1016;2$y";
  REQUIRE(mouse_frontend_decode_mode_report(
      reinterpret_cast<const uint8_t*>(reset), strlen(reset), &mode, &setting));
  CHECK(mode == 1016);
  CHECK(setting == 2);

  const char* no_marker = "\x1b[1016;2$y";
  CHECK_FALSE(mouse_frontend_decode_mode_report(
      reinterpret_cast<const uint8_t*>(no_marker), strlen(no_marker), &mode,
      &setting));
  const char* sgr_report = "\x1b[<0;10;5M";
  CHECK_FALSE(mouse_frontend_decode_mode_report(
      reinterpret_cast<const uint8_t*>(sgr_report), strlen(sgr_report), &mode,
      &setting));

  int width = 0;
  int height = 0;
  const char* cell = "\x1b[6;16;8t";
  REQUIRE(mouse_frontend_decode_cell_size_report(
      reinterpret_cast<const uint8_t*>(cell), strlen(cell), &width, &height));
  CHECK(width == 8);
  CHECK(height == 16);

  const char* window_size = "\x1b[8;24;80t";
  CHECK_FALSE(mouse_frontend_decode_cell_size_report(
      reinterpret_cast<const uint8_t*>(window_size), strlen(window_size),
      &width, &height));
  CHECK_FALSE(mouse_frontend_decode_cell_size_report(
      reinterpret_cast<const uint8_t*>(reset), strlen(reset), &width, &height));
}
