// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers) Justification: Hardware emulation register masks, bit widths, volume tables, and clock divider constants
// NOLINTBEGIN(bugprone-easily-swappable-parameters) Justification: Hardware signal interface and multi-channel audio buffer parameters
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic) Justification: Multi-channel audio sample buffer output indexing
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) Justification: Direct indexed access to hardware registers and volume tables
// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays) Justification: Planar output buffers handed in by the card, one per voice
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
#include <cstddef>
#include <cstdint>

namespace {

// Logarithmic amplitudes, -3 dB per step, from data sheet Fig 3. Normalizing
// by the loudest step makes a full-volume voice exactly 1.0, which is what the
// card declares as its peak magnitude. The chip is unipolar: it really does
// swing 0..Vmax, and the AC coupling that centres it is the card's output
// stage.
constexpr float vol_full_scale = 18776.0F;
constexpr std::array<float, 16> vol_table = {
    {0.0F / vol_full_scale, 103.0F / vol_full_scale, 150.0F / vol_full_scale,
     218.0F / vol_full_scale, 316.0F / vol_full_scale, 458.0F / vol_full_scale,
     665.0F / vol_full_scale, 963.0F / vol_full_scale, 1396.0F / vol_full_scale,
     2023.0F / vol_full_scale, 2933.0F / vol_full_scale,
     4251.0F / vol_full_scale, 6163.0F / vol_full_scale,
     8934.0F / vol_full_scale, 12952.0F / vol_full_scale,
     18776.0F / vol_full_scale}};

auto tone_period(uint8_t fine, uint8_t coarse) -> uint16_t {
  return static_cast<uint16_t>(fine | ((coarse & 0x0F) << 8));
}

// The output toggles every TP ticks, giving f = clock / (16 * TP).
auto step_tone(uint16_t* count, uint8_t* out, uint16_t period) -> void {
  if (period == 0) {
    *out = 1;
    return;
  }
  ++*count;
  if (*count >= period) {
    *count = 0;
    *out ^= 1;
  }
}

auto advance_noise(Ay8910_t* p) -> void {
  if ((((p->rng + 1) & 2) ^ (p->rng & 1)) != 0) {
    p->out_n ^= 1;
  }
  p->rng = (p->rng >> 1) | (((p->rng & 1) ^ ((p->rng >> 3) & 1)) << 16);
}

auto refresh_envelope_vol(Ay8910_t* p) -> void {
  p->envelope_vol = static_cast<uint8_t>(
      p->env_attack ? p->envelope_step : (15U - p->envelope_step));
}

// The step that produces `amplitude` depends on which way the sweep was
// running, because the level is read off the step in opposite directions.
auto hold_at(Ay8910_t* p, uint32_t amplitude) -> void {
  p->env_holding = true;
  p->envelope_step = p->env_attack ? amplitude : (15U - amplitude);
}

auto step_envelope(Ay8910_t* p, bool cont, bool alt, bool hold) -> void {
  ++p->envelope_step;
  if (p->envelope_step > 15) {
    if (!cont) {
      // Shapes 0x0-0x7 end the cycle at silence whichever way they swept.
      hold_at(p, 0U);
    } else if (hold) {
      // 0x9 and 0xF hold at silence, 0xB and 0xD at full scale.
      hold_at(p, (p->env_attack != alt) ? 15U : 0U);
    } else {
      p->envelope_step = 0;
      if (alt) {
        p->env_attack = !p->env_attack;
      }
    }
  }
  refresh_envelope_vol(p);
}

auto voice_level(const Ay8910_t* p, uint8_t tone_out, bool tone_off,
                 bool noise_off, uint8_t amplitude) -> float {
  const uint8_t tone = tone_off ? uint8_t{1} : tone_out;
  const uint8_t noise = noise_off ? uint8_t{1} : p->out_n;
  if ((tone & noise) == 0) {
    return 0.0F;
  }
  const uint8_t vol =
      ((amplitude & 0x10) != 0) ? p->envelope_vol : (amplitude & 0x0F);
  return vol_table[vol & 0x0F];
}

}  // namespace

auto ay8910_reset(Ay8910_t* p) -> void {
  if (p == nullptr) {
    return;
  }
  *p = Ay8910_t{};
}

auto ay8910_write(Ay8910_t* p, uint8_t reg, uint8_t val) -> void {
  if (p == nullptr || reg >= AY8910_NUM_REGISTERS) {
    return;
  }
  p->regs[reg] = val;
  switch (reg) {
    case 1:
    case 3:
    case 5:
      p->regs[reg] &= 0x0F;
      break;
    case 6:
    case 8:
    case 9:
    case 10:
      p->regs[reg] &= 0x1F;
      break;
    case 13:
      p->regs[reg] &= 0x0F;
      p->count_e = 0;
      p->envelope_step = 0;
      p->env_holding = false;
      p->env_attack = (val & 0x04) != 0;
      refresh_envelope_vol(p);
      break;
    default:
      break;
  }
}

auto ay8910_step(Ay8910_t* p, size_t ticks, float* const out[AY8910_NUM_VOICES],
                 size_t max) -> void {
  if (p == nullptr || out == nullptr) {
    return;
  }
  // Asking for more than the buffers hold would overrun them, so the chip
  // renders what fits and advances only that far.
  const size_t count = (ticks < max) ? ticks : max;

  const uint16_t period_a = tone_period(p->regs[0], p->regs[1]);
  const uint16_t period_b = tone_period(p->regs[2], p->regs[3]);
  const uint16_t period_c = tone_period(p->regs[4], p->regs[5]);

  // The LFSR advances every 2 * NP ticks and the envelope steps every 2 * EP,
  // both with a period of zero behaving as one.
  const uint32_t noise_reg = p->regs[6] & 0x1F;
  const uint32_t noise_div = 2U * ((noise_reg != 0) ? noise_reg : 1U);
  const uint32_t env_reg = static_cast<uint32_t>(p->regs[11]) |
                           (static_cast<uint32_t>(p->regs[12]) << 8);
  const uint32_t env_div = 2U * ((env_reg != 0) ? env_reg : 1U);

  const uint8_t enable = p->regs[7];
  const uint8_t shape = p->regs[13];
  const bool cont = (shape & 0x08) != 0;
  const bool alt = (shape & 0x02) != 0;
  const bool hold = (shape & 0x01) != 0;

  for (size_t i = 0; i < count; ++i) {
    step_tone(&p->count_a, &p->out_a, period_a);
    step_tone(&p->count_b, &p->out_b, period_b);
    step_tone(&p->count_c, &p->out_c, period_c);

    ++p->count_n;
    if (p->count_n >= noise_div) {
      p->count_n = 0;
      advance_noise(p);
    }

    if (!p->env_holding) {
      ++p->count_e;
      if (p->count_e >= env_div) {
        p->count_e = 0;
        step_envelope(p, cont, alt, hold);
      }
    }

    out[0][i] = voice_level(p, p->out_a, (enable & 0x01) != 0,
                            (enable & 0x08) != 0, p->regs[8]);
    out[1][i] = voice_level(p, p->out_b, (enable & 0x02) != 0,
                            (enable & 0x10) != 0, p->regs[9]);
    out[2][i] = voice_level(p, p->out_c, (enable & 0x04) != 0,
                            (enable & 0x20) != 0, p->regs[10]);
  }
}
// NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// NOLINTEND(bugprone-easily-swappable-parameters)
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
