// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

namespace via_reg {
constexpr uint8_t orb = 0x0;
constexpr uint8_t ora = 0x1;
constexpr uint8_t ddrb = 0x2;
constexpr uint8_t ddra = 0x3;
constexpr uint8_t t1c_l = 0x4;
constexpr uint8_t t1c_h = 0x5;
constexpr uint8_t t1l_l = 0x6;
constexpr uint8_t t1l_h = 0x7;
constexpr uint8_t t2c_l = 0x8;
constexpr uint8_t t2c_h = 0x9;
constexpr uint8_t sr = 0xA;
constexpr uint8_t acr = 0xB;
constexpr uint8_t pcr = 0xC;
constexpr uint8_t ifr = 0xD;
constexpr uint8_t ier = 0xE;
constexpr uint8_t ora_no_handshake = 0xF;
constexpr uint8_t mask = 0x0F;
}  // namespace via_reg

namespace via_ifr {
constexpr uint8_t timer2 = 0x20;
constexpr uint8_t timer1 = 0x40;
constexpr uint8_t any = 0x80;
constexpr uint8_t mask = 0x7F;
}  // namespace via_ifr

namespace via_acr {
constexpr uint8_t t2_pulse_count = 0x20;
constexpr uint8_t t1_free_run = 0x40;
constexpr uint8_t pb7_output = 0x80;
}  // namespace via_acr

// A counter loaded by a T1C-H / T2C-H write holds its value for one Phi2 cycle
// before the first decrement, and a free-running T1 spends one Phi2 cycle at
// 0xFFFF before the latch reappears. Both are the data sheet's half-cycle,
// quantized, and both are what puts the flag at N + 2 rather than N.
enum class ViaTimerPhase_t : uint8_t {
  running = 0,
  load_delay = 1,
  reload_pending = 2
};

// The shift register (0xA), the PCR (0xC) and the CA/CB handshake lines are
// transparent storage: a plain Mockingboard wires none of them to anything, so
// a write reads back and nothing else in the model observes it.
struct Via6522_t {
  uint8_t orb = 0;
  uint8_t ora = 0;
  uint8_t ddrb = 0;
  uint8_t ddra = 0;
  uint8_t shift_register = 0;
  uint8_t acr = 0;
  uint8_t pcr = 0;
  uint8_t ifr = 0;
  uint8_t ier = 0;
  uint8_t ora_no_handshake = 0;
  uint16_t t1_counter = 0;
  uint16_t t1_latch = 0;
  uint16_t t2_counter = 0;
  uint16_t t2_latch = 0;
  ViaTimerPhase_t t1_phase = ViaTimerPhase_t::running;
  ViaTimerPhase_t t2_phase = ViaTimerPhase_t::running;
  // Set by an underflow and cleared by a T1C-H / T2C-H write. A one-shot timer
  // raises its flag only while the latch is clear, which is why a reset leaves
  // both set: a timer nobody has armed must not interrupt.
  bool t1_fired = true;
  bool t2_fired = true;
  bool pb7 = false;
};

auto via_reset(Via6522_t* v) -> void;
auto via_write(Via6522_t* v, uint8_t reg, uint8_t val) -> void;
auto via_read(Via6522_t* v, uint8_t reg) -> uint8_t;
auto via_step(Via6522_t* v, uint32_t cycles) -> bool;
auto via_irq(const Via6522_t* v) -> bool;
