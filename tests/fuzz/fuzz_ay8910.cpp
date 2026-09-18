// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(modernize-use-trailing-return-type,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "apple2/chips/AY8910.h"

namespace {

constexpr size_t max_ticks_per_step = 1024;

// One register write and one run of the generators.
constexpr size_t record_size = 4;

std::array<std::array<float, max_ticks_per_step>, AY8910_NUM_VOICES> g_scratch;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  Ay8910_t psg;
  ay8910_reset(&psg);

  std::array<float*, AY8910_NUM_VOICES> voices = {
      {g_scratch[0].data(), g_scratch[1].data(), g_scratch[2].data()}};

  for (size_t offset = 0; offset + record_size <= size; offset += record_size) {
    const uint8_t* record = data + offset;
    if ((record[0] & 0x80) != 0) {
      ay8910_reset(&psg);
    }
    ay8910_write(&psg, record[0] & 0x1F, record[1]);

    // Wider than the scratch so the chip's own clamp is part of the search.
    const size_t ticks =
        static_cast<size_t>(record[2]) | (static_cast<size_t>(record[3]) << 8);
    for (auto& voice : g_scratch) {
      voice.fill(-1.0F);
    }
    ay8910_step(&psg, ticks, voices.data(), max_ticks_per_step);

    const size_t rendered = (ticks < max_ticks_per_step) ? ticks
                                                         : max_ticks_per_step;
    for (const auto& voice : g_scratch) {
      for (size_t i = 0; i < rendered; ++i) {
        // The bare chip is unipolar: it swings from ground to Vmax and the AC
        // coupling that centres it belongs to the card, not here.
        assert(std::isfinite(voice[i]));
        assert(voice[i] >= 0.0F);
        assert(voice[i] <= 1.0F);
      }
      // Nothing past what was asked for may be written.
      for (size_t i = rendered; i < max_ticks_per_step; ++i) {
        assert(voice[i] == -1.0F);
      }
    }
  }

  return 0;
}
// NOLINTEND(modernize-use-trailing-return-type,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
