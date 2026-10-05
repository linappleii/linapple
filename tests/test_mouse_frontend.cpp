// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstddef>
#include <cstdint>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "doctest.h"
#include "frontends/common/MouseFrontend.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

namespace {

constexpr int first_slot = 1;
constexpr int last_slot = 7;
constexpr size_t frame_size = 92;
constexpr size_t frame_position_x = 8;
constexpr size_t frame_position_y = 12;

auto read_u32(const std::array<uint8_t, frame_size>& frame, size_t at)
    -> uint32_t {
  return static_cast<uint32_t>(frame.at(at)) |
         (static_cast<uint32_t>(frame.at(at + 1)) << 8) |
         (static_cast<uint32_t>(frame.at(at + 2)) << 16) |
         (static_cast<uint32_t>(frame.at(at + 3)) << 24);
}

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
  mouse_frontend_dispatch_motion(10, 10);
  mouse_frontend_dispatch_button(k_mouse_button_left, true);
  peripheral_manager_think(0);
}

#if defined(ENABLE_PERIPHERAL_MOUSE)
TEST_CASE(
    "Mouse frontend: the probe finds the card in slot 5 and a motion reaches "
    "it") {
  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[4] = "Mouse Interface";
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);

  mouse_frontend_initialize();
  CHECK(mouse_frontend_card_slot() == 5);
  CHECK(mouse_frontend_card_present());

  // The centre of the 560 x 384 frame lands at the centre of the card's
  // 0..1023 range.
  mouse_frontend_dispatch_motion(280, 192);
  peripheral_manager_think(0);

  std::array<uint8_t, frame_size> frame{};
  size_t size = frame.size();
  peripheral_save_state(5, frame.data(), &size);
  REQUIRE(size == frame_size);
  CHECK(read_u32(frame, frame_position_x) == 511);
  CHECK(read_u32(frame, frame_position_y) == 511);
}
#endif
