// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/mockingboard/Mockingboard.h"
#include "apple2/peripherals/mockingboard/MockingboardCommands.h"

namespace {

constexpr int card_slot = 4;
constexpr size_t card_voices = 6;
constexpr uint32_t cycles_per_tick = 8;
constexpr size_t ticks_per_chunk = 1024;
constexpr uint16_t card_page = 0xC400;

// Eight control bytes and one candidate save state, so a wrong byte anywhere
// in the layout is part of the search rather than a separate harness.
constexpr size_t record_size = 8 + sizeof(MockingboardSaveState_t);

std::vector<size_t> g_push_sizes;
bool g_samples_in_range = true;
bool g_shape_ok = true;

// The card is the only thing that drives the line, and it may only report a
// change. /RES is the exception: it reports the line inactive whether or not
// that is news, so the alternation restarts there.
bool g_irq_line = false;
bool g_in_reset = false;
bool g_irq_alternates = true;

auto mock_assert_irq(int slot, bool assert_irq) -> void {
  (void)slot;
  if (g_in_reset) {
    g_irq_line = assert_irq;
    return;
  }
  g_irq_alternates = g_irq_alternates && (assert_irq != g_irq_line);
  g_irq_line = assert_irq;
}

PeripheralIOHandler g_read_cx = nullptr;
PeripheralIOHandler g_write_cx = nullptr;

auto mock_register_io(int slot, PeripheralIOHandler read_c0,
                      PeripheralIOHandler write_c0, PeripheralIOHandler read_cx,
                      PeripheralIOHandler write_cx) -> void {
  (void)slot;
  (void)read_c0;
  (void)write_c0;
  g_read_cx = read_cx;
  g_write_cx = write_cx;
}

auto mock_audio_push_channels(void* instance, const float* const* channels,
                              size_t num_channels, size_t num_samples) -> void {
  (void)instance;
  g_shape_ok = g_shape_ok && (channels != nullptr) &&
               (num_channels == card_voices) && (num_samples > 0) &&
               (num_samples <= ticks_per_chunk);
  if (channels == nullptr || num_channels != card_voices) {
    return;
  }
  g_push_sizes.push_back(num_samples);
  for (size_t c = 0; c < num_channels; ++c) {
    if (channels[c] == nullptr) {
      g_shape_ok = false;
      continue;
    }
    for (size_t i = 0; i < num_samples; ++i) {
      const float sample = channels[c][i];
      // The card's output stage is AC coupled, so what leaves it is bipolar
      // and bounded by the peak magnitude it declares.
      g_samples_in_range = g_samples_in_range && (std::isfinite(sample) != 0) &&
                           (sample >= -1.0F) && (sample <= 1.0F);
    }
  }
}

auto mock_log(void* instance, PeripheralLogLevel_t level, const char* fmt, ...)
    -> void {
  (void)instance;
  (void)level;
  (void)fmt;
}

auto begin_observation() -> void {
  g_push_sizes.clear();
  g_samples_in_range = true;
  g_shape_ok = true;
}

/**
 * @brief Check the pushes one advance produced against the cycles it was given.
 *
 * The card renders in chunks of `ticks_per_chunk` and never pushes a chunk
 * that came out silent, so the observed sizes are a subsequence of the sizes
 * the cycle count implies: every one of them is a whole chunk except the
 * slice's last, and when none was dropped they add up to every tick.
 */
auto check_pushes(uint32_t advanced, uint32_t* carry) -> void {
  const uint64_t total = static_cast<uint64_t>(*carry) + advanced;
  const uint64_t ticks = total / cycles_per_tick;
  *carry = static_cast<uint32_t>(total % cycles_per_tick);

  const uint64_t implied_chunks =
      (ticks + ticks_per_chunk - 1) / ticks_per_chunk;
  const size_t tail = static_cast<size_t>(ticks % ticks_per_chunk);

  assert(g_samples_in_range);
  assert(g_shape_ok);
  assert(g_irq_alternates);
  assert(g_push_sizes.size() <= implied_chunks);

  uint64_t pushed = 0;
  for (size_t i = 0; i < g_push_sizes.size(); ++i) {
    const bool is_last = (i + 1 == g_push_sizes.size());
    const bool whole_chunk = (g_push_sizes[i] == ticks_per_chunk);
    const bool is_tail = is_last && (tail != 0) && (g_push_sizes[i] == tail);
    assert(whole_chunk || is_tail);
    pushed += g_push_sizes[i];
  }
  assert(pushed <= ticks);
  if (g_push_sizes.size() == implied_chunks) {
    assert(pushed == ticks);
  }
}

auto read_carry(Peripheral_t* card, void* instance) -> uint32_t {
  MockingboardSaveState_t state{};
  size_t size = sizeof(state);
  if (card->save_state(instance, &state, &size) != peripheral_ok) {
    return 0;
  }
  return state.psg_remainder;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  Peripheral_t* card = mockingboard_get_descriptor();

  HostInterface_t host{};
  host.Log = mock_log;
  host.AssertIrq = mock_assert_irq;
  host.RegisterIO = mock_register_io;
  host.AudioPushChannels = mock_audio_push_channels;

  g_read_cx = nullptr;
  g_write_cx = nullptr;
  g_irq_line = false;
  g_irq_alternates = true;

  void* instance = card->init(card_slot, &host);
  if (instance == nullptr) {
    return 0;
  }

  uint32_t carry = 0;

  for (size_t offset = 0; offset + record_size <= size; offset += record_size) {
    const uint8_t* record = data + offset;
    const uint8_t flags = record[0];
    const uint16_t addr = card_page | record[1];
    const uint8_t value = record[2];
    const uint32_t executed = static_cast<uint32_t>(record[3]) |
                              (static_cast<uint32_t>(record[4]) << 8);
    const uint32_t slice = static_cast<uint32_t>(record[5]) |
                           (static_cast<uint32_t>(record[6]) << 8);

    if ((flags & 0x01) != 0) {
      g_in_reset = true;
      card->reset(instance);
      g_in_reset = false;
      carry = 0;
    }
    if ((flags & 0x02) != 0) {
      MockingboardSaveState_t candidate{};
      std::memcpy(&candidate, record + 8, sizeof(candidate));
      // The claimed size comes out of the input too, so a load that lies about
      // its length is part of the search space.
      const size_t claimed =
          ((flags & 0x04) != 0) ? sizeof(candidate) + (flags >> 5)
                                : sizeof(candidate);
      if (card->load_state(instance, &candidate, claimed) == peripheral_ok) {
        carry = read_carry(card, instance);
      }
    }

    uint32_t synced = 0;
    if ((flags & 0x08) != 0) {
      begin_observation();
      const uint32_t advanced = (executed > synced) ? executed - synced : 0;
      if ((flags & 0x10) != 0 && g_write_cx != nullptr) {
        g_write_cx(instance, 0, addr, 1, value, executed);
      } else if (g_read_cx != nullptr) {
        g_read_cx(instance, 0, addr, 0, 0, executed);
      }
      synced = (executed > synced) ? executed : synced;
      check_pushes(advanced, &carry);
    }

    begin_observation();
    const uint32_t advanced = (slice > synced) ? slice - synced : 0;
    card->think(instance, slice);
    check_pushes(advanced, &carry);
  }

  card->shutdown(instance);
  return 0;
}
// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)
