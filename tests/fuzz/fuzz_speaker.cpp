// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/speaker/Speaker.h"

namespace {

// The peripheral's own per-update capacity limits.
constexpr size_t speaker_max_samples_per_update = 24000;

uint64_t g_cycles = 0;
size_t g_pushed_samples = 0;
size_t g_push_calls = 0;
bool g_all_finite = true;

auto mock_get_cycles() -> uint64_t { return g_cycles; }

auto mock_register_direct_io_strobe(void* instance, uint16_t addr,
                                    PeripheralStrobeHandler_t on_strobe)
    -> void;

auto mock_audio_push_channels(void* instance,
                              const float* const* channel_buffers,
                              size_t num_channels, size_t num_samples) -> void {
  (void)instance;
  g_push_calls++;
  g_pushed_samples += num_samples;
  assert(channel_buffers != nullptr);
  assert(num_channels == 1);
  for (size_t i = 0; i < num_samples; ++i) {
    g_all_finite = g_all_finite && (std::isfinite(channel_buffers[0][i]) != 0);
  }
}

auto mock_log(void* instance, PeripheralLogLevel_t level, const char* fmt, ...)
    -> void {
  (void)instance;
  (void)level;
  (void)fmt;
}

void* g_strobe_instance = nullptr;
PeripheralStrobeHandler_t g_strobe_handler = nullptr;

auto mock_register_direct_io_strobe(void* instance, uint16_t addr,
                                    PeripheralStrobeHandler_t on_strobe)
    -> void {
  (void)addr;
  g_strobe_instance = instance;
  g_strobe_handler = on_strobe;
}

// [0..3] elapsed cycles, [4..7] cycle delta, [8] strobe count, [9] operation
// flags, [10..49] a candidate SsIoSpeaker_t. The flags are 0x01 reset, 0x02
// load_state, 0x04 claim a wrong size for that load, 0x08 save_state, and the
// top three bits the size overshoot.
constexpr size_t record_size = 10 + sizeof(SsIoSpeaker_t);

auto read_u32(const uint8_t* p) -> uint32_t {
  uint32_t value = 0;
  std::memcpy(&value, p, sizeof(value));
  return value;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  Peripheral_t* speaker = speaker_get_descriptor();

  HostInterface_t host{};
  host.Log = mock_log;
  host.GetCycles = mock_get_cycles;
  host.AudioPushChannels = mock_audio_push_channels;
  host.RegisterDirectIOStrobe = mock_register_direct_io_strobe;

  g_cycles = 0;
  g_strobe_instance = nullptr;
  g_strobe_handler = nullptr;

  void* instance = speaker->init(0, &host);
  if (instance == nullptr) {
    return 0;
  }

  for (size_t offset = 0; offset + record_size <= size; offset += record_size) {
    const uint8_t* record = data + offset;
    const uint32_t elapsed = read_u32(record);
    const uint32_t delta = read_u32(record + 4);
    const uint8_t strobes = record[8];
    const uint8_t flags = record[9];

    if ((flags & 0x01) != 0) {
      speaker->reset(instance);
    }
    if ((flags & 0x02) != 0) {
      SsIoSpeaker_t state{};
      std::memcpy(&state, record + 10, sizeof(state));
      // The size is deliberately taken from the record too, so wrong-size
      // loads are part of the search space.
      const size_t claimed =
          ((flags & 0x04) != 0) ? sizeof(state) + (flags >> 5) : sizeof(state);
      speaker->load_state(instance, &state, claimed);
    }
    if ((flags & 0x08) != 0) {
      SsIoSpeaker_t saved{};
      size_t saved_size = sizeof(saved);
      speaker->save_state(instance, &saved, &saved_size);
      assert(saved_size == sizeof(SsIoSpeaker_t));
    }

    g_cycles += delta;
    for (uint8_t i = 0; i < strobes; ++i) {
      if (g_strobe_handler != nullptr) {
        g_strobe_handler(g_strobe_instance);
      }
    }

    g_pushed_samples = 0;
    g_push_calls = 0;
    g_all_finite = true;
    speaker->think(instance, elapsed);

    assert(g_all_finite);
    assert(g_push_calls <= 1);
    assert(g_pushed_samples <= speaker_max_samples_per_update);
    assert(g_pushed_samples <= elapsed);
  }

  speaker->shutdown(instance);
  return 0;
}
// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
