// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

constexpr int MAX_8910 = 4;
constexpr size_t AY8910_NUM_REGISTERS = 16;

// AY-3-8910 emulation
// Based on General Instrument AY-3-8910 Datasheet

struct Ay8910_t {
  std::array<uint8_t, AY8910_NUM_REGISTERS> regs = {};
  uint16_t count_a = 0, count_b = 0, count_c = 0;
  uint8_t out_a = 0, out_b = 0, out_c = 0;

  uint32_t count_n = 0;
  uint32_t rng = 0;
  uint8_t out_n = 0;

  uint32_t count_e = 0;
  uint8_t envelope_vol = 0;
  uint32_t envelope_step = 0;
  bool env_holding = false;

  double count_accum = 0.0;
};

auto ay8910_reset_instance(Ay8910_t* p) -> void;
auto ay8910_write_instance(Ay8910_t* p, int r, int v, int ay_clock,
                           int sample_rate) -> void;
auto ay8910_update_instance(Ay8910_t* p, int16_t** buffer, int length,
                            int ay_clock, int sample_rate) -> void;

// Legacy stubs
auto ay8910_init_all(int clock_rate, int sample_rate) -> void;
auto ay8910_init_clock(int clock) -> void;
auto ay8910_reset(int chip) -> void;
auto ay8910_write_ym(int chip, int addr, int data) -> void;
auto ay_write_reg_internal(int n, int r, int v) -> void;
auto ay8910_update(int chip, int16_t** buffer, int length) -> void;
auto ay8910_get_regs_ptr(uint32_t ay_num) -> uint8_t*;
