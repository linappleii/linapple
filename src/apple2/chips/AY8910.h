#pragma once

#include <cstddef>
#include <cstdint>

constexpr int MAX_8910 = 4;
constexpr size_t AY8910_NUM_REGISTERS = 16;

// AY-3-8910 emulation
// Based on General Instrument AY-3-8910 Datasheet

struct Ay8910_t {
  uint8_t regs[AY8910_NUM_REGISTERS] = {};
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
  uint32_t step = 0;
};

void ay8910_reset_instance(Ay8910_t* p);
void ay8910_write_instance(Ay8910_t* p, int r, int v, int ay_clock,
                           int sample_rate);
void ay8910_update_instance(Ay8910_t* p, int16_t** buffer, int length,
                            int ay_clock, int sample_rate);

// Legacy stubs
void ay8910_init_all(int clock_rate, int sample_rate);
void ay8910_init_clock(int clock);
void ay8910_reset(int chip);
void ay8910_write_ym(int chip, int addr, int data);
void _ay_write_reg(int n, int r, int v);
void ay8910_update(int chip, int16_t** buffer, int length);
auto ay8910_get_regs_ptr(uint32_t ay_num) -> uint8_t*;
