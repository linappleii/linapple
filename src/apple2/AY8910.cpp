// NOLINTBEGIN(bugprone-easily-swappable-parameters, modernize-use-trailing-return-type, cppcoreguidelines-owning-memory, cppcoreguidelines-avoid-non-const-global-variables, cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index, bugprone-branch-clone, google-readability-braces-around-statements, cppcoreguidelines-no-malloc, cppcoreguidelines-pro-type-const-cast, google-readability-todo, cppcoreguidelines-pro-type-reinterpret-cast, bugprone-narrowing-conversions, cppcoreguidelines-narrowing-conversions, bugprone-switch-missing-default-case, cppcoreguidelines-use-default-member-init, modernize-use-default-member-init, cppcoreguidelines-use-enum-class, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access, cppcoreguidelines-macro-usage, bugprone-macro-parentheses)
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

#include "AY8910.h"

#include <array>
#include <cstdint>
#include <cstring>

// Logarithmic volume table for AY-3-8910 (16 levels)
// Based on -3dB per step as indicated in datasheet Fig 3.
static const std::array<uint16_t, 16> vol_table = {
    {0, 103, 150, 218, 316, 458, 665, 963, 1396, 2023, 2933, 4251, 6163, 8934,
     12952, 18776}};

void AY8910_reset_instance(AY8910* p) {
  if (!p) return;
  memset(p, 0, sizeof(AY8910));
  p->rng = 1;
}

void AY8910_write_instance(AY8910* p, int r, int v, int ay_clock,
                           int sample_rate) {
  (void)ay_clock;
  (void)sample_rate;
  if (!p || r < 0 || r >= 16) return;
  p->regs[r] = v & 0xFF;
  switch (r) {
    case 1:
    case 3:
    case 5:
      p->regs[r] &= 0x0F;
      break;
    case 6:
      p->regs[r] &= 0x1F;
      break;
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
  }
}

void AY8910_update_instance(AY8910* p, int16_t** buffer, int length,
                            int ay_clock, int sample_rate) {
  if (!p) return;

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
      if (((p->rng + 1) & 2) ^ (p->rng & 1)) p->out_n ^= 1;
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
              if (alt) shape ^= 0x04;
            }
          }
        }
      }
    }

    bool cur_attack = (shape & 0x04) != 0;
    p->envelope_vol = cur_attack ? p->envelope_step : (15 - p->envelope_step);

    int chan_a = 0, chan_b = 0, chan_c = 0;
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
static std::array<AY8910, MAX_8910> ay_chips;
static int ay_clock = 1000000;
static int ay_sample_rate = 44100;

void AY8910_InitAll(int clock_rate, int sample_rate) {
  ay_clock = clock_rate;
  ay_sample_rate = sample_rate;
  for (int i = 0; i < MAX_8910; i++) {
    AY8910_reset(i);
  }
}
void AY8910_InitClock(int nClock) { ay_clock = nClock; }
void AY8910_reset(int chip) {
  if (chip >= 0 && chip < MAX_8910) AY8910_reset_instance(&ay_chips[chip]);
}
void AY8910_write_ym(int chip, int addr, int data) {
  if (chip >= 0 && chip < MAX_8910)
    AY8910_write_instance(&ay_chips[chip], addr, data, ay_clock,
                          ay_sample_rate);
}
void _AYWriteReg(int n, int r, int v) {
  if (n >= 0 && n < MAX_8910)
    AY8910_write_instance(&ay_chips[n], r, v, ay_clock, ay_sample_rate);
}
void AY8910Update(int chip, int16_t** buffer, int length) {
  if (chip >= 0 && chip < MAX_8910)
    AY8910_update_instance(&ay_chips[chip], buffer, length, ay_clock,
                           ay_sample_rate);
}
auto AY8910_GetRegsPtr(uint32_t nAyNum) -> uint8_t* {
  if (nAyNum >= MAX_8910) return nullptr;
  return ay_chips[nAyNum].regs;
}
// NOLINTEND(bugprone-easily-swappable-parameters, modernize-use-trailing-return-type, cppcoreguidelines-owning-memory, cppcoreguidelines-avoid-non-const-global-variables, cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index, bugprone-branch-clone, google-readability-braces-around-statements, cppcoreguidelines-no-malloc, cppcoreguidelines-pro-type-const-cast, google-readability-todo, cppcoreguidelines-pro-type-reinterpret-cast, bugprone-narrowing-conversions, cppcoreguidelines-narrowing-conversions, bugprone-switch-missing-default-case, cppcoreguidelines-use-default-member-init, modernize-use-default-member-init, cppcoreguidelines-use-enum-class, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access, cppcoreguidelines-macro-usage, bugprone-macro-parentheses)
