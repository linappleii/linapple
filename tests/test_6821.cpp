// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdint>

#include "apple2/chips/6821.h"
#include "doctest.h"

namespace {

struct CallbackRecord_t {
  uint32_t call_count = 0;
  uint8_t last_val = 0;
};

auto test_callback(void* obj_to, uint8_t data) -> void {
  auto* rec = static_cast<CallbackRecord_t*>(obj_to);
  if (rec != nullptr) {
    ++rec->call_count;
    rec->last_val = data;
  }
}

}  // namespace

TEST_CASE("6821 PIA: Null Instance Is Completely Inert") {
  pia_6821_reset(nullptr);
  CHECK(pia_6821_read(nullptr, 0) == 0);
  CHECK(pia_6821_read(nullptr, 1) == 0);
  CHECK(pia_6821_read(nullptr, 2) == 0);
  CHECK(pia_6821_read(nullptr, 3) == 0);

  pia_6821_write(nullptr, 0, 0xFF);
  pia_6821_write(nullptr, 1, 0xFF);

  pia_6821_set_port_a(nullptr, 0x55);
  pia_6821_set_port_b(nullptr, 0xAA);
  pia_6821_set_ca1(nullptr, true);
  pia_6821_set_ca2(nullptr, true);
  pia_6821_set_cb1(nullptr, true);
  pia_6821_set_cb2(nullptr, true);

  CHECK(pia_6821_get_port_a(nullptr) == 0);
  CHECK(pia_6821_get_port_b(nullptr) == 0);

  pia_6821_set_listener_a(nullptr, nullptr, nullptr);
  pia_6821_set_listener_b(nullptr, nullptr, nullptr);
  pia_6821_set_listener_ca2(nullptr, nullptr, nullptr);
  pia_6821_set_listener_cb2(nullptr, nullptr, nullptr);
  pia_6821_set_listener_irqa(nullptr, nullptr, nullptr);
  pia_6821_set_listener_irqb(nullptr, nullptr, nullptr);
}

TEST_CASE(
    "6821 PIA: Reset Establishes Datasheet Pull-Ups And Clears Registers") {
  Pia6821_t pia;
  pia.ora = 0xAA;
  pia.orb = 0x55;
  pia.ddra = 0xFF;
  pia.ddrb = 0xFF;
  pia.cra = 0x3F;
  pia.crb = 0x3F;

  pia_6821_reset(&pia);

  CHECK(pia.ora == 0);
  CHECK(pia.orb == 0);
  CHECK(pia.ddra == 0);
  CHECK(pia.ddrb == 0);
  CHECK(pia.cra == 0);
  CHECK(pia.crb == 0);
  // MC6821 internal pull-ups on Port A and Port B lines
  CHECK(pia.port_a_in == 0xFF);
  CHECK(pia.port_b_in == 0xFF);
  CHECK(pia_6821_get_port_a(&pia) == 0);
  CHECK(pia_6821_get_port_b(&pia) == 0);
}

TEST_CASE("6821 PIA: DDR vs Port Selection via CRA/CRB Bit 2") {
  Pia6821_t pia;
  pia_6821_reset(&pia);

  // When CRA bit 2 is 0, address 0 accesses DDRA
  pia_6821_write(&pia, 1, 0x00);  // CRA bit 2 = 0
  pia_6821_write(&pia, 0, 0x0F);  // Lower 4 bits output, upper 4 bits input
  CHECK(pia_6821_read(&pia, 0) == 0x0F);

  // When CRA bit 2 is 1, address 0 accesses Port A (pin state)
  pia_6821_write(&pia, 1, 0x04);  // CRA bit 2 = 1
  pia_6821_set_port_a(&pia, 0xA5);
  CHECK(pia_6821_read(&pia, 0) == 0xA5);

  // When CRB bit 2 is 0, address 2 accesses DDRB
  pia_6821_write(&pia, 3, 0x00);  // CRB bit 2 = 0
  pia_6821_write(&pia, 2, 0xF0);  // Upper 4 bits output, lower 4 bits input
  CHECK(pia_6821_read(&pia, 2) == 0xF0);

  // When CRB bit 2 is 1, address 2 reads (orb & ddrb) | (port_b_in & ~ddrb)
  pia_6821_write(&pia, 3, 0x04);  // CRB bit 2 = 1
  pia_6821_write(&pia, 2, 0xA0);  // ORB = 0xA0
  pia_6821_set_port_b(&pia, 0x05);
  // Expected: (0xA0 & 0xF0) | (0x05 & 0x0F) = 0xA0 | 0x05 = 0xA5
  CHECK(pia_6821_read(&pia, 2) == 0xA5);
}

TEST_CASE("6821 PIA: Output Listeners Receive Masked Values") {
  Pia6821_t pia;
  pia_6821_reset(&pia);

  CallbackRecord_t rec_a{};
  CallbackRecord_t rec_b{};

  pia_6821_set_listener_a(&pia, &rec_a, test_callback);
  pia_6821_set_listener_b(&pia, &rec_b, test_callback);

  // Set DDRA to 0x0F, select ORA
  pia_6821_write(&pia, 1, 0x00);
  pia_6821_write(&pia, 0, 0x0F);
  pia_6821_write(&pia, 1, 0x04);

  // Write 0xFF to Port A; listener should receive (0xFF & 0x0F) == 0x0F
  pia_6821_write(&pia, 0, 0xFF);
  CHECK(rec_a.call_count == 1);
  CHECK(rec_a.last_val == 0x0F);

  // Set DDRB to 0xAA, select ORB
  pia_6821_write(&pia, 3, 0x00);
  pia_6821_write(&pia, 2, 0xAA);
  pia_6821_write(&pia, 3, 0x04);

  // Write 0xFF to Port B; listener should receive (0xFF & 0xAA) == 0xAA
  pia_6821_write(&pia, 2, 0xFF);
  CHECK(rec_b.call_count == 1);
  CHECK(rec_b.last_val == 0xAA);

  CHECK(pia_6821_get_port_a(&pia) == 0x0F);
  CHECK(pia_6821_get_port_b(&pia) == 0xAA);
}

// "The B side read comes from an output latch" (MC6821 data sheet, page 8); the
// AppleMouse firmware relies on it with DDRB $3E, reading its bank bits back.
TEST_CASE(
    "6821 PIA: Port B reads the latch for output bits and the pins for input "
    "bits") {
  Pia6821_t pia;
  pia_6821_reset(&pia);
  pia_6821_write(&pia, 3, 0x00);
  pia_6821_write(&pia, 2, 0x3E);
  pia_6821_write(&pia, 3, 0x04);
  pia_6821_write(&pia, 2, 0x12);

  pia_6821_set_port_b(&pia, 0xC1);
  CHECK(pia_6821_read(&pia, 2) == 0xD3);

  pia_6821_set_port_b(&pia, 0xFF);
  CHECK(pia_6821_read(&pia, 2) == 0xD3);
  pia_6821_set_port_b(&pia, 0x00);
  CHECK(pia_6821_read(&pia, 2) == 0x12);
}
