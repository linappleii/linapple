// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers) Justification: Hardware emulation register masks, bit widths, volume tables, and clock divider constants
// NOLINTBEGIN(bugprone-easily-swappable-parameters) Justification: Hardware signal interface and multi-channel audio buffer parameters
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) Justification: Global legacy state maintained for backwards-compatible chip emulation
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic) Justification: Multi-channel audio sample buffer output indexing
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) Justification: Direct indexed access to hardware registers and volume tables
/*
LinApple : Apple ][ emulator for Linux

Copyright (C) 2026, LinApple Team

LinApple is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

LinApple is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with LinApple; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: AY-3-8910 sound chip emulation */

#include "apple2/chips/AY8910.h"

#include <array>
#include <cstdint>

// Logarithmic volume table for AY-3-8910 (16 levels)
// Based on -3dB per step as indicated in datasheet Fig 3.
static constexpr std::array<uint16_t, 16> vol_table = {
    {0, 103, 150, 218, 316, 458, 665, 963, 1396, 2023, 2933, 4251, 6163, 8934,
     12952, 18776}};

auto ay8910_reset_instance(Ay8910_t* p) -> void {
  if (!p) {
    return;
  }
  *p = Ay8910_t{};
  p->rng = 1;
}

auto ay8910_write_instance(Ay8910_t* p, int r, int v, int ay_clock,
                           int sample_rate) -> void {
  (void)ay_clock;
  (void)sample_rate;
  if (!p || r < 0 || r >= 16) {
    return;
  }
  p->regs[r] = v & 0xFF;
  switch (r) {
    case 1:
    case 3:
    case 5:
      p->regs[r] &= 0x0F;
      break;
    case 6:
    case 8:
    case 9:
    case 10:
      p->regs[r] &= 0x1F;
      break;
    case 13:
      p->regs[r] &= 0x0F;
      p->count_e = 0;
      p->envelope_step = 0;
      p->env_holding = false;
      break;
    default:
      break;
  }
}

auto ay8910_update_instance(Ay8910_t* p, int16_t** buffer, int length,
                            int ay_clock, int sample_rate) -> void {
  if (!p) {
    return;
  }

  uint16_t period_a = p->regs[0] | (p->regs[1] << 8);
  uint16_t period_b = p->regs[2] | (p->regs[3] << 8);
  uint16_t period_c = p->regs[4] | (p->regs[5] << 8);
  uint8_t noise_period = (p->regs[6] & 0x1F) * 2;
  uint16_t period_e = p->regs[11] | (p->regs[12] << 8);
  uint8_t enable = p->regs[7];
  uint8_t shape = p->regs[13];

  double psg_cycles_per_sample =
      static_cast<double>(ay_clock) / (16.0 * sample_rate);

  for (int i = 0; i < length; i++) {
    p->count_accum += psg_cycles_per_sample;
    auto psg_cycles = static_cast<uint32_t>(p->count_accum);
    p->count_accum -= psg_cycles;

    if (period_a > 0) {
      p->count_a += psg_cycles;
      while (p->count_a >= period_a) {
        p->count_a -= period_a;
        p->out_a ^= 1;
      }
    } else {
      p->out_a = 1;
    }

    if (period_b > 0) {
      p->count_b += psg_cycles;
      while (p->count_b >= period_b) {
        p->count_b -= period_b;
        p->out_b ^= 1;
      }
    } else {
      p->out_b = 1;
    }

    if (period_c > 0) {
      p->count_c += psg_cycles;
      while (p->count_c >= period_c) {
        p->count_c -= period_c;
        p->out_c ^= 1;
      }
    } else {
      p->out_c = 1;
    }

    uint32_t n_p = noise_period ? noise_period : 1;
    p->count_n += psg_cycles;
    while (p->count_n >= n_p) {
      p->count_n -= n_p;
      if (((p->rng + 1) & 2) ^ (p->rng & 1)) {
        p->out_n ^= 1;
      }
      p->rng = (p->rng >> 1) | (((p->rng & 1) ^ ((p->rng >> 3) & 1)) << 16);
    }

    if (!p->env_holding) {
      uint32_t e_p = (period_e ? period_e : 1) * 16;
      p->count_e += psg_cycles;
      while (p->count_e >= e_p) {
        p->count_e -= e_p;
        p->envelope_step++;

        bool cont = (shape & 0x08) != 0;
        bool attack = (shape & 0x04) != 0;
        bool alt = (shape & 0x02) != 0;
        bool hold = (shape & 0x01) != 0;

        if (p->envelope_step > 15) {
          if (!cont) {
            p->env_holding = true;
            p->envelope_step = 0;
          } else {
            if (hold) {
              p->env_holding = true;
              p->envelope_step = (alt ^ attack) ? 0 : 15;
            } else {
              p->envelope_step = 0;
              if (alt) {
                shape ^= 0x04;
              }
            }
          }
        }
      }
    }

    bool cur_attack = (shape & 0x04) != 0;
    p->envelope_vol = cur_attack ? p->envelope_step : (15 - p->envelope_step);

    int chan_a = 0;
    int chan_b = 0;
    int chan_c = 0;
    if ((!(enable & 0x01) ? p->out_a : 1) & (!(enable & 0x08) ? p->out_n : 1)) {
      uint8_t vol = (p->regs[8] & 0x10) ? p->envelope_vol : (p->regs[8] & 0x0F);
      chan_a = vol_table[vol];
    }
    if ((!(enable & 0x02) ? p->out_b : 1) & (!(enable & 0x10) ? p->out_n : 1)) {
      uint8_t vol = (p->regs[9] & 0x10) ? p->envelope_vol : (p->regs[9] & 0x0F);
      chan_b = vol_table[vol];
    }
    if ((!(enable & 0x04) ? p->out_c : 1) & (!(enable & 0x20) ? p->out_n : 1)) {
      uint8_t vol =
          (p->regs[10] & 0x10) ? p->envelope_vol : (p->regs[10] & 0x0F);
      chan_c = vol_table[vol];
    }

    buffer[0][i] = static_cast<int16_t>(chan_a);
    buffer[1][i] = static_cast<int16_t>(chan_b);
    buffer[2][i] = static_cast<int16_t>(chan_c);
  }
}

// Legacy Global State for compatibility
static std::array<Ay8910_t, MAX_8910> ay_chips;
static int ay_clock = 1000000;
static int ay_sample_rate = 44100;

auto ay8910_init_all(int clock_rate, int sample_rate) -> void {
  ay_clock = clock_rate;
  ay_sample_rate = sample_rate;
  for (int i = 0; i < MAX_8910; i++) {
    ay8910_reset(i);
  }
}
auto ay8910_init_clock(int clock) -> void { ay_clock = clock; }
auto ay8910_reset(int chip) -> void {
  if (chip >= 0 && chip < MAX_8910) {
    ay8910_reset_instance(&ay_chips[chip]);
  }
}
auto ay8910_write_ym(int chip, int addr, int data) -> void {
  if (chip >= 0 && chip < MAX_8910) {
    ay8910_write_instance(&ay_chips[chip], addr, data, ay_clock,
                          ay_sample_rate);
  }
}
auto ay_write_reg_internal(int n, int r, int v) -> void {
  if (n >= 0 && n < MAX_8910) {
    ay8910_write_instance(&ay_chips[n], r, v, ay_clock, ay_sample_rate);
  }
}
auto ay8910_update(int chip, int16_t** buffer, int length) -> void {
  if (chip >= 0 && chip < MAX_8910) {
    ay8910_update_instance(&ay_chips[chip], buffer, length, ay_clock,
                           ay_sample_rate);
  }
}
auto ay8910_get_regs_ptr(uint32_t ay_num) -> uint8_t* {
  if (ay_num >= MAX_8910) {
    return nullptr;
  }
  return ay_chips[ay_num].regs.data();
}
// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTEND(bugprone-easily-swappable-parameters)
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
