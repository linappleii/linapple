// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers) Justification: Hardware register offsets, bit masks and counter widths
#include "apple2/chips/6522.h"

#include <cstdint>

namespace {

auto lo(uint16_t w) -> uint8_t { return static_cast<uint8_t>(w & 0xFF); }

auto hi(uint16_t w) -> uint8_t { return static_cast<uint8_t>(w >> 8); }

auto set_lo(uint16_t* w, uint8_t v) -> void {
  *w = static_cast<uint16_t>((*w & 0xFF00) | v);
}

auto set_hi(uint16_t* w, uint8_t v) -> void {
  *w = static_cast<uint16_t>((*w & 0x00FF) | (static_cast<uint16_t>(v) << 8));
}

// Phi2 ticks from now until the counter's 0x0000 -> 0xFFFF transition. A
// counter at c wraps c + 1 ticks from now, and each pending phase tick costs
// one more.
auto ticks_to_underflow(uint16_t counter, uint16_t latch, ViaTimerPhase_t phase)
    -> uint32_t {
  if (phase == ViaTimerPhase_t::load_delay) {
    return static_cast<uint32_t>(counter) + 2U;
  }
  if (phase == ViaTimerPhase_t::reload_pending) {
    return static_cast<uint32_t>(latch) + 2U;
  }
  return static_cast<uint32_t>(counter) + 1U;
}

// Modular, so any tick count is correct; the caller decides where flags fall.
auto advance(uint16_t* counter, uint16_t latch, ViaTimerPhase_t* phase,
             uint32_t ticks) -> void {
  if (ticks == 0) {
    return;
  }
  if (*phase == ViaTimerPhase_t::reload_pending) {
    *counter = latch;
    *phase = ViaTimerPhase_t::running;
    --ticks;
  } else if (*phase == ViaTimerPhase_t::load_delay) {
    *phase = ViaTimerPhase_t::running;
    --ticks;
  }
  *counter = static_cast<uint16_t>(*counter - ticks);
}

auto step_timer1(Via6522_t* v, uint32_t cycles) -> void {
  const bool free_run = (v->acr & via_acr::t1_free_run) != 0;
  const bool pb7_driven = (v->acr & via_acr::pb7_output) != 0;
  uint32_t remaining = cycles;
  while (remaining > 0) {
    if (!free_run && v->t1_fired) {
      advance(&v->t1_counter, v->t1_latch, &v->t1_phase, remaining);
      return;
    }
    const uint32_t span =
        ticks_to_underflow(v->t1_counter, v->t1_latch, v->t1_phase);
    if (span > remaining) {
      advance(&v->t1_counter, v->t1_latch, &v->t1_phase, remaining);
      return;
    }
    advance(&v->t1_counter, v->t1_latch, &v->t1_phase, span);
    remaining -= span;
    v->ifr |= via_ifr::timer1;
    v->t1_fired = true;
    v->t1_phase =
        free_run ? ViaTimerPhase_t::reload_pending : ViaTimerPhase_t::running;
    if (pb7_driven) {
      v->pb7 = free_run ? !v->pb7 : true;
    }
  }
}

auto step_timer2(Via6522_t* v, uint32_t cycles) -> void {
  // Pulse-count mode clocks T2 from PB6, which no Mockingboard trace reaches.
  if ((v->acr & via_acr::t2_pulse_count) != 0) {
    return;
  }
  uint32_t remaining = cycles;
  while (remaining > 0) {
    if (v->t2_fired) {
      advance(&v->t2_counter, v->t2_latch, &v->t2_phase, remaining);
      return;
    }
    const uint32_t span =
        ticks_to_underflow(v->t2_counter, v->t2_latch, v->t2_phase);
    if (span > remaining) {
      advance(&v->t2_counter, v->t2_latch, &v->t2_phase, remaining);
      return;
    }
    advance(&v->t2_counter, v->t2_latch, &v->t2_phase, span);
    remaining -= span;
    v->ifr |= via_ifr::timer2;
    v->t2_fired = true;
    v->t2_phase = ViaTimerPhase_t::running;
  }
}

}  // namespace

auto via_irq(const Via6522_t* v) -> bool {
  return v != nullptr && (v->ifr & v->ier & via_ifr::mask) != 0;
}

auto via_reset(Via6522_t* v) -> void {
  if (v == nullptr) {
    return;
  }
  // /RES clears the port, control and interrupt registers and leaves the timer
  // counters, the latches and the shift register holding whatever they held.
  v->orb = 0;
  v->ora = 0;
  v->ddrb = 0;
  v->ddra = 0;
  v->acr = 0;
  v->pcr = 0;
  v->ifr = 0;
  v->ier = 0;
  v->ora_no_handshake = 0;
  v->t1_phase = ViaTimerPhase_t::running;
  v->t2_phase = ViaTimerPhase_t::running;
  v->t1_fired = true;
  v->t2_fired = true;
  v->pb7 = false;
}

auto via_write(Via6522_t* v, uint8_t reg, uint8_t val) -> void {
  if (v == nullptr) {
    return;
  }
  switch (reg & via_reg::mask) {
    case via_reg::orb:
      v->orb = val;
      break;
    case via_reg::ora:
      v->ora = val;
      break;
    case via_reg::ddrb:
      v->ddrb = val;
      break;
    case via_reg::ddra:
      v->ddra = val;
      break;
    case via_reg::t1c_l:
    case via_reg::t1l_l:
      set_lo(&v->t1_latch, val);
      break;
    case via_reg::t1c_h:
      set_hi(&v->t1_latch, val);
      v->t1_counter = v->t1_latch;
      v->t1_phase = ViaTimerPhase_t::load_delay;
      v->t1_fired = false;
      v->ifr &= static_cast<uint8_t>(~via_ifr::timer1);
      if ((v->acr & via_acr::pb7_output) != 0) {
        v->pb7 = false;
      }
      break;
    case via_reg::t1l_h:
      set_hi(&v->t1_latch, val);
      v->ifr &= static_cast<uint8_t>(~via_ifr::timer1);
      break;
    case via_reg::t2c_l:
      set_lo(&v->t2_latch, val);
      break;
    case via_reg::t2c_h:
      v->t2_counter = static_cast<uint16_t>((static_cast<uint16_t>(val) << 8) |
                                            lo(v->t2_latch));
      v->t2_phase = ViaTimerPhase_t::load_delay;
      v->t2_fired = false;
      v->ifr &= static_cast<uint8_t>(~via_ifr::timer2);
      break;
    case via_reg::sr:
      v->shift_register = val;
      break;
    case via_reg::acr:
      v->acr = val;
      break;
    case via_reg::pcr:
      v->pcr = val;
      break;
    case via_reg::ifr:
      v->ifr &= static_cast<uint8_t>(~(val & via_ifr::mask));
      break;
    case via_reg::ier:
      if ((val & via_ifr::any) != 0) {
        v->ier |= static_cast<uint8_t>(val & via_ifr::mask);
      } else {
        v->ier &= static_cast<uint8_t>(~(val & via_ifr::mask));
      }
      break;
    case via_reg::ora_no_handshake:
      v->ora_no_handshake = val;
      break;
    default:
      break;
  }
}

auto via_read(Via6522_t* v, uint8_t reg) -> uint8_t {
  if (v == nullptr) {
    return 0;
  }
  switch (reg & via_reg::mask) {
    case via_reg::orb:
      if ((v->acr & via_acr::pb7_output) != 0) {
        return static_cast<uint8_t>((v->orb & 0x7F) | (v->pb7 ? 0x80 : 0x00));
      }
      return v->orb;
    case via_reg::ora:
      return v->ora;
    case via_reg::ddrb:
      return v->ddrb;
    case via_reg::ddra:
      return v->ddra;
    case via_reg::t1c_l:
      v->ifr &= static_cast<uint8_t>(~via_ifr::timer1);
      return lo(v->t1_counter);
    case via_reg::t1c_h:
      return hi(v->t1_counter);
    case via_reg::t1l_l:
      return lo(v->t1_latch);
    case via_reg::t1l_h:
      return hi(v->t1_latch);
    case via_reg::t2c_l:
      v->ifr &= static_cast<uint8_t>(~via_ifr::timer2);
      return lo(v->t2_counter);
    case via_reg::t2c_h:
      return hi(v->t2_counter);
    case via_reg::sr:
      return v->shift_register;
    case via_reg::acr:
      return v->acr;
    case via_reg::pcr:
      return v->pcr;
    case via_reg::ifr:
      return static_cast<uint8_t>(v->ifr |
                                  (via_irq(v) ? via_ifr::any : uint8_t{0}));
    case via_reg::ier:
      return static_cast<uint8_t>(v->ier | via_ifr::any);
    case via_reg::ora_no_handshake:
      return v->ora_no_handshake;
    default:
      return 0;
  }
}

auto via_step(Via6522_t* v, uint32_t cycles) -> bool {
  if (v == nullptr) {
    return false;
  }
  const bool was_asserted = via_irq(v);
  step_timer1(v, cycles);
  step_timer2(v, cycles);
  return via_irq(v) != was_asserted;
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
