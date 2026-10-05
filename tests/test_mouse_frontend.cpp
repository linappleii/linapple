// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstddef>
#include <cstdint>

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
  mouse_frontend_button(true);
  peripheral_manager_think(0);
}

#if defined(ENABLE_PERIPHERAL_MOUSE)

namespace {

constexpr size_t frame_size = 92;
constexpr size_t frame_position_x = 8;
constexpr size_t frame_position_y = 12;
constexpr size_t frame_max_x = 20;
constexpr size_t frame_max_y = 28;
constexpr size_t frame_parser_out_len = 52;
constexpr size_t frame_orb = 57;
constexpr size_t frame_ddrb = 59;
constexpr size_t frame_port_b_shadow = 73;
constexpr size_t frame_mode = 74;
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

// The card counts host motion only while its mode byte says so; the loader
// is the one public path that sets that byte without running the firmware.
auto turn_tracking_on(int slot) -> void {
  Frame_t frame{};
  frame.at(0) = MOUSE_STATE_VERSION;
  frame.at(4) = frame_size;
  frame.at(frame_max_x) = 0xFF;
  frame.at(frame_max_x + 1) = 0x03;
  frame.at(frame_max_y) = 0xFF;
  frame.at(frame_max_y + 1) = 0x03;
  frame.at(frame_parser_out_len) = 1;
  // Port B at rest as a real build saves it: PB6 answers the lowered PB4.
  frame.at(frame_orb) = 0x40;
  frame.at(frame_ddrb) = 0x3E;
  frame.at(frame_port_b_shadow) = 0x40;
  frame.at(frame_mode) = 1;
  REQUIRE(peripheral_load_state(slot, frame.data(), frame.size()) ==
          peripheral_ok);
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

  SUBCASE("a terminal box 80 cells wide: one cell is 3 and 4 counts in turn") {
    mouse_frontend_motion(1, 0, 80, 24);
    CHECK(position(4) == std::array<int16_t, 2>{3, 0});
    mouse_frontend_motion(1, 0, 80, 24);
    CHECK(position(4) == std::array<int16_t, 2>{7, 0});
    mouse_frontend_motion(1, 0, 80, 24);
    CHECK(position(4) == std::array<int16_t, 2>{10, 0});
    mouse_frontend_motion(1, 0, 80, 24);
    CHECK(position(4) == std::array<int16_t, 2>{14, 0});
    mouse_frontend_motion(0, 1, 80, 24);
    CHECK(position(4) == std::array<int16_t, 2>{14, 8});
  }

  SUBCASE("a picture of no size sends nothing") {
    mouse_frontend_motion(100, 100, 0, 0);
    CHECK(position(4) == std::array<int16_t, 2>{0, 0});
  }
}
#endif
