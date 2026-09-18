// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

constexpr size_t AY8910_NUM_REGISTERS = 16;
constexpr size_t AY8910_NUM_VOICES = 3;

// AY-3-8910 emulation, per the General Instrument data sheet.
//
// The chip knows nothing about hosts, clocks in hertz or sample rates. Its
// tone, noise and envelope counters all advance at clock / 8, and one sample
// comes out per counter tick, so the caller -- which is the only party that
// knows what the chip is wired to -- decides how many ticks elapsed.
struct Ay8910_t {
  std::array<uint8_t, AY8910_NUM_REGISTERS> regs = {};
  uint16_t count_a = 0;
  uint16_t count_b = 0;
  uint16_t count_c = 0;
  uint8_t out_a = 0;
  uint8_t out_b = 0;
  uint8_t out_c = 0;

  uint32_t count_n = 0;
  // A zero LFSR shifts zeroes forever, so the noise source is dead until the
  // next reset.
  uint32_t rng = 1;
  uint8_t out_n = 0;

  uint32_t count_e = 0;
  uint32_t envelope_step = 0;
  uint8_t envelope_vol = 0;
  bool env_holding = false;
  bool env_attack = false;
};

auto ay8910_reset(Ay8910_t* p) -> void;
auto ay8910_write(Ay8910_t* p, uint8_t reg, uint8_t val) -> void;

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays) Justification: Planar output buffers handed in by the card, one per voice
auto ay8910_step(Ay8910_t* p, size_t ticks, float* const out[AY8910_NUM_VOICES],
                 size_t max) -> void;
// NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
