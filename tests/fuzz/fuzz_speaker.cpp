// SPDX-License-Identifier: GPL-2.0-only
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

uint64_t cycles = 0;
size_t pushed_samples = 0;
size_t push_calls = 0;
bool all_finite = true;

auto mock_get_cycles() -> uint64_t { return cycles; }

auto mock_register_direct_io_strobe(void* instance, uint16_t addr,
                                    PeripheralStrobeHandler on_strobe)
    -> void;

auto mock_audio_push_channels(void* instance,
                              const float* const* channel_buffers,
                              size_t num_channels, size_t num_samples) -> void {
  (void)instance;
  push_calls++;
  pushed_samples += num_samples;
  assert(channel_buffers != nullptr);
  assert(num_channels == 1);
  for (size_t i = 0; i < num_samples; ++i) {
    all_finite = all_finite && (std::isfinite(channel_buffers[0][i]) != 0);
  }
}

auto mock_log(void* instance, PeripheralLogLevel level, const char* fmt, ...)
    -> void {
  (void)instance;
  (void)level;
  (void)fmt;
}

void* strobe_instance = nullptr;
PeripheralStrobeHandler strobe_handler = nullptr;

auto mock_register_direct_io_strobe(void* instance, uint16_t addr,
                                    PeripheralStrobeHandler on_strobe)
    -> void {
  (void)addr;
  strobe_instance = instance;
  strobe_handler = on_strobe;
}

// [0..3] elapsed cycles, [4..7] cycle delta, [8] strobe count, [9] operation
// flags, [10..49] a candidate SsIoSpeaker. The flags are 0x01 reset, 0x02
// load_state, 0x04 claim a wrong size for that load, 0x08 save_state, and the
// top three bits the size overshoot.
constexpr size_t record_size = 10 + sizeof(SsIoSpeaker);

auto read_u32(const uint8_t* p) -> uint32_t {
  uint32_t value = 0;
  std::memcpy(&value, p, sizeof(value));
  return value;
}

}  // namespace

// NOLINTNEXTLINE(modernize-use-trailing-return-type) - libFuzzer C entrypoint
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  Peripheral* speaker = speaker_get_descriptor();

  HostInterface host{};
  host.Log = mock_log;
  host.GetCycles = mock_get_cycles;
  host.AudioPushChannels = mock_audio_push_channels;
  host.RegisterDirectIOStrobe = mock_register_direct_io_strobe;

  cycles = 0;
  strobe_instance = nullptr;
  strobe_handler = nullptr;

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
      SsIoSpeaker state{};
      std::memcpy(&state, record + 10, sizeof(state));
      // The size is deliberately taken from the record too, so wrong-size
      // loads are part of the search space.
      const size_t claimed =
          ((flags & 0x04) != 0) ? sizeof(state) + (flags >> 5) : sizeof(state);
      speaker->load_state(instance, &state, claimed);
    }
    if ((flags & 0x08) != 0) {
      SsIoSpeaker saved{};
      size_t saved_size = sizeof(saved);
      speaker->save_state(instance, &saved, &saved_size);
      assert(saved_size == sizeof(SsIoSpeaker));
    }

    cycles += delta;
    for (uint8_t i = 0; i < strobes; ++i) {
      if (strobe_handler != nullptr) {
        strobe_handler(strobe_instance);
      }
    }

    pushed_samples = 0;
    push_calls = 0;
    all_finite = true;
    speaker->think(instance, elapsed);

    assert(all_finite);
    assert(push_calls <= 1);
    assert(pushed_samples <= speaker_max_samples_per_update);
    assert(pushed_samples <= elapsed);
  }

  speaker->shutdown(instance);
  return 0;
}
