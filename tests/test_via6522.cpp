// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers) Justification: Hardware register offsets, bit masks and cycle-count goldens
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdint>

#include "apple2/chips/6522.h"
#include "doctest.h"

namespace {

constexpr uint8_t ier_set_timer1 = 0xC0;
constexpr uint8_t ier_set_timer2 = 0xA0;

auto armed_timer1(Via6522_t* v, uint16_t latch, uint8_t acr) -> void {
  via_write(v, via_reg::acr, acr);
  via_write(v, via_reg::ier, ier_set_timer1);
  via_write(v, via_reg::t1c_l, static_cast<uint8_t>(latch & 0xFF));
  via_write(v, via_reg::t1c_h, static_cast<uint8_t>(latch >> 8));
}

// Ticks the model one cycle at a time so a golden can name the exact cycle a
// flag appears on; the arithmetic path is exercised separately.
auto cycle_of_first_irq(Via6522_t* v, uint32_t limit) -> uint32_t {
  for (uint32_t t = 1; t <= limit; ++t) {
    via_step(v, 1);
    if (via_irq(v)) {
      return t;
    }
  }
  return 0;
}

}  // namespace

TEST_CASE("6522 VIA: Reset Clears Control Registers And Spares The Timers") {
  Via6522_t v;
  via_write(&v, via_reg::t1c_l, 0x34);
  via_write(&v, via_reg::t1c_h, 0x12);
  via_write(&v, via_reg::t2c_l, 0x78);
  via_write(&v, via_reg::t2c_h, 0x56);
  via_write(&v, via_reg::sr, 0xA5);
  via_write(&v, via_reg::orb, 0xFF);
  via_write(&v, via_reg::ora, 0xEE);
  via_write(&v, via_reg::ddrb, 0xFF);
  via_write(&v, via_reg::ddra, 0xFF);
  via_write(&v, via_reg::pcr, 0x0F);
  via_write(&v, via_reg::ier, ier_set_timer1);

  via_reset(&v);

  CHECK(via_read(&v, via_reg::orb) == 0x00);
  CHECK(via_read(&v, via_reg::ora) == 0x00);
  CHECK(via_read(&v, via_reg::ddrb) == 0x00);
  CHECK(via_read(&v, via_reg::ddra) == 0x00);
  CHECK(via_read(&v, via_reg::acr) == 0x00);
  CHECK(via_read(&v, via_reg::pcr) == 0x00);
  CHECK(via_read(&v, via_reg::ifr) == 0x00);
  CHECK(via_read(&v, via_reg::ier) == 0x80);
  CHECK_FALSE(via_irq(&v));

  CHECK(v.t1_counter == 0x1234);
  CHECK(v.t1_latch == 0x1234);
  CHECK(v.t2_counter == 0x5678);
  CHECK(via_read(&v, via_reg::sr) == 0xA5);
}

TEST_CASE("6522 VIA: An Unarmed Timer Never Interrupts") {
  Via6522_t v;
  via_reset(&v);
  via_write(&v, via_reg::ier, ier_set_timer1);
  via_write(&v, via_reg::ier, ier_set_timer2);

  CHECK_FALSE(via_step(&v, 200000));
  CHECK_FALSE(via_irq(&v));
  CHECK((via_read(&v, via_reg::ifr) & via_ifr::timer1) == 0);
  CHECK((via_read(&v, via_reg::ifr) & via_ifr::timer2) == 0);
}

TEST_CASE("6522 VIA: One-Shot Timer 1 Fires Once At N Plus Two") {
  constexpr uint16_t latch = 1000;
  Via6522_t v;
  via_reset(&v);
  armed_timer1(&v, latch, 0x00);

  CHECK(cycle_of_first_irq(&v, latch + 10) == latch + 2);

  // Past the flag the counter keeps running from 0xFFFF, but the one-shot
  // interrupts only once: clear the flag and nothing re-raises it.
  via_write(&v, via_reg::ifr, via_ifr::timer1);
  CHECK_FALSE(via_irq(&v));
  CHECK_FALSE(via_step(&v, 200000));
  CHECK_FALSE(via_irq(&v));
}

TEST_CASE("6522 VIA: Timer 1 Counter Reads N Minus K After The Flag") {
  constexpr uint16_t latch = 0x0100;
  Via6522_t v;
  via_reset(&v);
  armed_timer1(&v, latch, via_acr::t1_free_run);

  via_step(&v, latch + 2);
  REQUIRE(via_irq(&v));
  CHECK(v.t1_counter == 0xFFFF);

  via_step(&v, 1);
  CHECK(v.t1_counter == latch);

  for (uint16_t k = 1; k <= 8; ++k) {
    via_step(&v, 1);
    CHECK(v.t1_counter == static_cast<uint16_t>(latch - k));
  }
}

TEST_CASE("6522 VIA: Free-Running Timer 1 Repeats Every N Plus Two") {
  constexpr uint16_t latch = 0x1000;
  constexpr uint32_t period = latch + 2;
  Via6522_t v;
  via_reset(&v);
  armed_timer1(&v, latch, via_acr::t1_free_run);

  for (int i = 0; i < 10; ++i) {
    CHECK(cycle_of_first_irq(&v, period + 4) == period);
    via_write(&v, via_reg::ifr, via_ifr::timer1);
    REQUIRE_FALSE(via_irq(&v));
  }
}

TEST_CASE("6522 VIA: A Latch Of Zero Free-Runs Every Two Cycles") {
  Via6522_t v;
  via_reset(&v);
  armed_timer1(&v, 0x0000, via_acr::t1_free_run);

  CHECK(cycle_of_first_irq(&v, 8) == 2);
  via_write(&v, via_reg::ifr, via_ifr::timer1);
  CHECK(cycle_of_first_irq(&v, 8) == 2);

  // The arithmetic path must consume the same number of underflows a
  // one-cycle-at-a-time drive would, without looping past `cycles`.
  via_write(&v, via_reg::ifr, via_ifr::timer1);
  CHECK(via_step(&v, 100000));
  CHECK(via_irq(&v));
}

TEST_CASE("6522 VIA: Writing T1L-H Does Not Restart Timer 1") {
  constexpr uint16_t latch = 500;
  Via6522_t v;
  via_reset(&v);
  armed_timer1(&v, latch, 0x00);

  via_step(&v, 100);
  const uint16_t counter_before = v.t1_counter;

  via_write(&v, via_reg::t1l_h, 0x7F);
  CHECK(v.t1_counter == counter_before);
  CHECK(via_read(&v, via_reg::t1l_h) == 0x7F);

  // Still counting from where it was: the remaining 402 cycles land the flag.
  CHECK(cycle_of_first_irq(&v, latch + 10) == latch + 2 - 100);
}

TEST_CASE("6522 VIA: A Second T1C-H Write Rearms A Fired One-Shot") {
  constexpr uint16_t latch = 300;
  Via6522_t v;
  via_reset(&v);
  armed_timer1(&v, latch, 0x00);

  REQUIRE(cycle_of_first_irq(&v, latch + 4) == latch + 2);
  via_write(&v, via_reg::ifr, via_ifr::timer1);
  REQUIRE_FALSE(via_irq(&v));

  via_write(&v, via_reg::t1c_h, 0x01);
  CHECK(cycle_of_first_irq(&v, 0x0200) == 0x012C + 2);
}

TEST_CASE("6522 VIA: Timer 2 Fires Once And Keeps Counting") {
  constexpr uint16_t latch = 700;
  Via6522_t v;
  via_reset(&v);
  via_write(&v, via_reg::ier, ier_set_timer2);
  via_write(&v, via_reg::t2c_l, static_cast<uint8_t>(latch & 0xFF));
  via_write(&v, via_reg::t2c_h, static_cast<uint8_t>(latch >> 8));

  CHECK(cycle_of_first_irq(&v, latch + 10) == latch + 2);
  CHECK(v.t2_counter == 0xFFFF);

  via_write(&v, via_reg::ifr, via_ifr::timer2);
  REQUIRE_FALSE(via_irq(&v));

  via_step(&v, 100);
  CHECK(v.t2_counter == static_cast<uint16_t>(0xFFFF - 100));
  CHECK_FALSE(via_irq(&v));

  CHECK_FALSE(via_step(&v, 65536));
  CHECK_FALSE(via_irq(&v));
}

TEST_CASE("6522 VIA: Timer 2 Holds In Pulse-Count Mode") {
  constexpr uint16_t latch = 50;
  Via6522_t v;
  via_reset(&v);
  via_write(&v, via_reg::acr, via_acr::t2_pulse_count);
  via_write(&v, via_reg::ier, ier_set_timer2);
  via_write(&v, via_reg::t2c_l, static_cast<uint8_t>(latch));
  via_write(&v, via_reg::t2c_h, 0x00);

  CHECK_FALSE(via_step(&v, 10000));
  CHECK(v.t2_counter == latch);
  CHECK_FALSE(via_irq(&v));
}

TEST_CASE("6522 VIA: A Counter Read Clears Its Own Flag Only") {
  constexpr uint16_t latch = 40;
  Via6522_t v;
  via_reset(&v);
  via_write(&v, via_reg::ier, ier_set_timer1);
  via_write(&v, via_reg::ier, ier_set_timer2);
  armed_timer1(&v, latch, 0x00);
  via_write(&v, via_reg::t2c_l, static_cast<uint8_t>(latch));
  via_write(&v, via_reg::t2c_h, 0x00);

  via_step(&v, latch + 2);
  REQUIRE((v.ifr & via_ifr::timer1) != 0);
  REQUIRE((v.ifr & via_ifr::timer2) != 0);

  via_read(&v, via_reg::t1c_l);
  CHECK((v.ifr & via_ifr::timer1) == 0);
  CHECK((v.ifr & via_ifr::timer2) != 0);

  via_read(&v, via_reg::t2c_l);
  CHECK((v.ifr & via_ifr::timer2) == 0);
  CHECK_FALSE(via_irq(&v));

  // A latch read is not an acknowledge.
  armed_timer1(&v, latch, 0x00);
  via_step(&v, latch + 2);
  REQUIRE(via_irq(&v));
  via_read(&v, via_reg::t1l_l);
  via_read(&v, via_reg::t1c_h);
  CHECK(via_irq(&v));
}

TEST_CASE("6522 VIA: Both Timers Pending, Clearing One Leaves IRQ Asserted") {
  constexpr uint16_t latch = 64;
  Via6522_t v;
  via_reset(&v);
  via_write(&v, via_reg::ier, ier_set_timer1);
  via_write(&v, via_reg::ier, ier_set_timer2);
  armed_timer1(&v, latch, 0x00);
  via_write(&v, via_reg::t2c_l, static_cast<uint8_t>(latch));
  via_write(&v, via_reg::t2c_h, 0x00);

  via_step(&v, latch + 2);
  REQUIRE(via_irq(&v));
  CHECK((via_read(&v, via_reg::ifr) & via_ifr::any) != 0);

  via_write(&v, via_reg::ifr, via_ifr::timer1);
  CHECK(via_irq(&v));
  CHECK((via_read(&v, via_reg::ifr) & via_ifr::any) != 0);

  via_write(&v, via_reg::ifr, via_ifr::timer2);
  CHECK_FALSE(via_irq(&v));
  CHECK((via_read(&v, via_reg::ifr) & via_ifr::any) == 0);
}

TEST_CASE("6522 VIA: IFR And IER Masking Truth Table") {
  Via6522_t v;
  via_reset(&v);

  // IER bit 7 selects set or clear of bits 0 through 6, and reads back set.
  via_write(&v, via_reg::ier, 0xFF);
  CHECK(via_read(&v, via_reg::ier) == 0xFF);
  via_write(&v, via_reg::ier, 0x7F);
  CHECK(via_read(&v, via_reg::ier) == 0x80);
  via_write(&v, via_reg::ier, 0x80 | via_ifr::timer1);
  CHECK(via_read(&v, via_reg::ier) == (0x80 | via_ifr::timer1));

  // A flag with no enable sets IFR but not the IRQ line or bit 7.
  armed_timer1(&v, 10, 0x00);
  via_write(&v, via_reg::ier, via_ifr::timer1);
  via_step(&v, 12);
  CHECK((v.ifr & via_ifr::timer1) != 0);
  CHECK_FALSE(via_irq(&v));
  CHECK((via_read(&v, via_reg::ifr) & via_ifr::any) == 0);

  // Enabling it afterwards is enough: bit 7 is computed, never stored.
  via_write(&v, via_reg::ier, 0x80 | via_ifr::timer1);
  CHECK(via_irq(&v));
  CHECK((via_read(&v, via_reg::ifr) & via_ifr::any) != 0);

  // Writing IFR clears only the bits set in the value; bit 7 is ignored.
  via_write(&v, via_reg::ifr, 0x80);
  CHECK(via_irq(&v));
  via_write(&v, via_reg::ifr, 0xFF);
  CHECK_FALSE(via_irq(&v));
  CHECK(via_read(&v, via_reg::ifr) == 0x00);
}

TEST_CASE("6522 VIA: PB7 Output Is Visible In An ORB Read") {
  constexpr uint16_t latch = 20;
  Via6522_t v;
  via_reset(&v);
  via_write(&v, via_reg::orb, 0xFF);
  CHECK(via_read(&v, via_reg::orb) == 0xFF);

  armed_timer1(&v, latch, via_acr::pb7_output | via_acr::t1_free_run);
  CHECK((via_read(&v, via_reg::orb) & 0x80) == 0x00);

  via_step(&v, latch + 2);
  CHECK((via_read(&v, via_reg::orb) & 0x80) == 0x80);

  via_step(&v, latch + 2);
  CHECK((via_read(&v, via_reg::orb) & 0x80) == 0x00);

  // With ACR bit 7 clear the ORB latch is what a read returns.
  via_write(&v, via_reg::acr, 0x00);
  CHECK(via_read(&v, via_reg::orb) == 0xFF);
}

TEST_CASE("6522 VIA: Transparent Registers Read Back What Was Written") {
  Via6522_t v;
  via_reset(&v);
  via_write(&v, via_reg::sr, 0x5A);
  via_write(&v, via_reg::pcr, 0xA5);
  via_write(&v, via_reg::ora_no_handshake, 0x3C);
  CHECK(via_read(&v, via_reg::sr) == 0x5A);
  CHECK(via_read(&v, via_reg::pcr) == 0xA5);
  CHECK(via_read(&v, via_reg::ora_no_handshake) == 0x3C);

  // Offsets above 0x0F alias down: the model owns sixteen registers.
  via_write(&v, 0x13, 0x77);
  CHECK(via_read(&v, 0x03) == 0x77);
}

TEST_CASE("6522 VIA: Step Reports Only Real IRQ Line Changes") {
  constexpr uint16_t latch = 100;
  Via6522_t v;
  via_reset(&v);
  armed_timer1(&v, latch, via_acr::t1_free_run);

  CHECK_FALSE(via_step(&v, latch));
  CHECK(via_step(&v, 2));
  REQUIRE(via_irq(&v));

  // Already asserted: further periods raise the flag again but not the line.
  CHECK_FALSE(via_step(&v, 4 * (latch + 2)));
  CHECK(via_irq(&v));

  via_write(&v, via_reg::ifr, via_ifr::timer1);
  CHECK_FALSE(via_irq(&v));
  CHECK(via_step(&v, latch + 2));
}

TEST_CASE("6522 VIA: Null Instance Is Inert") {
  via_reset(nullptr);
  via_write(nullptr, via_reg::orb, 0xFF);
  CHECK(via_read(nullptr, via_reg::orb) == 0x00);
  CHECK_FALSE(via_step(nullptr, 100));
  CHECK_FALSE(via_irq(nullptr));
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
