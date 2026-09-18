// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "apple2/peripherals/Peripheral_Audio.h"
#include "core/LinAppleCore.h"
#include "frontends/common/AudioMixer.h"

namespace {

constexpr size_t max_upload_samples = 4096;
constexpr size_t max_drain_frames = 2048;
// A sentinel word on each side of the drain buffer: get_samples writing past
// what it was asked for is the one overrun a caller can see from outside.
constexpr int16_t guard_value = 0x5A5A;

class ByteReader_t {
 public:
  ByteReader_t(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  auto exhausted() const -> bool { return offset_ >= size_; }

  auto u8() -> uint8_t {
    if (offset_ >= size_) {
      return 0;
    }
    return data_[offset_++];
  }

  auto u16() -> uint16_t {
    const auto low = static_cast<uint16_t>(u8());
    const auto high = static_cast<uint16_t>(u8());
    return static_cast<uint16_t>(low | (high << 8));
  }

  // A byte mapped onto roughly [-2, 2], so uploads straddle full scale in
  // both directions and exercise the clip.
  auto level() -> float { return (static_cast<float>(u8()) - 128.0f) / 64.0f; }

 private:
  const uint8_t* data_;
  size_t size_;
  size_t offset_ = 0;
};

auto make_info(ByteReader_t& reader) -> PeripheralAudioInfo_t {
  PeripheralAudioInfo_t info{};
  const uint8_t form = reader.u8();
  info.time_base = static_cast<PeripheralAudioTimeBase_t>(form & 0x03);
  info.cycle_divisor = reader.u8();
  info.sample_rate = static_cast<uint32_t>(reader.u16()) * 8;
  info.num_channels = reader.u8() % (PERIPHERAL_AUDIO_MAX_CHANNELS + 2);
  info.peak_magnitude = reader.level();
  for (size_t c = 0; c < PERIPHERAL_AUDIO_MAX_CHANNELS; ++c) {
    info.channels[c].default_pan_left = reader.level();
    info.channels[c].default_pan_right = reader.level();
  }
  return info;
}

auto drain_and_check(size_t frames) -> void {
  std::vector<int16_t> buffer(frames + 2, guard_value);
  audio_mixer_get_samples(buffer.data() + 1, frames);
  assert(buffer.front() == guard_value);
  assert(buffer.back() == guard_value);
  for (size_t i = 0; i < frames; ++i) {
    assert(buffer[i + 1] >= -32767);
    assert(buffer[i + 1] <= 32767);
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  ByteReader_t reader(data, size);

  // Capped at 192 kHz: the ring is sized from the rate, and there is nothing
  // to learn from allocating megabytes per iteration.
  const auto rate = static_cast<uint32_t>(reader.u16() % 24001) * 8;
  g_current_clk_6502 = 1020484.0;
  audio_mixer_initialize(rate);

  std::vector<float> plane(max_upload_samples, 0.0f);
  std::vector<const float*> channels(PERIPHERAL_AUDIO_MAX_CHANNELS + 2,
                                     plane.data());

  while (!reader.exhausted()) {
    const uint8_t op = reader.u8();
    const int slot = static_cast<int>(reader.u8() % 10) - 1;

    switch (op % 8) {
      case 0: {
        const PeripheralAudioInfo_t info = make_info(reader);
        audio_mixer_register_source(slot, "fuzz.source", &info);
        break;
      }
      case 1:
        audio_mixer_unregister_source(slot);
        break;
      case 2: {
        const size_t count = reader.u16() % (max_upload_samples + 1);
        const size_t num_channels =
            reader.u8() % (PERIPHERAL_AUDIO_MAX_CHANNELS + 2);
        const float level = reader.level();
        for (size_t i = 0; i < count; ++i) {
          plane[i] = level;
        }
        audio_mixer_upload_channels("fuzz.source", slot, channels.data(),
                                    num_channels, static_cast<uint32_t>(count));
        break;
      }
      case 3:
        audio_mixer_set_channel_pan(slot, reader.u8() % 18, reader.level(),
                                    reader.level());
        break;
      case 4:
        audio_mixer_set_source_gain(slot, reader.level());
        break;
      case 5:
        drain_and_check(reader.u16() % (max_drain_frames + 1));
        break;
      case 6:
        audio_mixer_clear_buffers();
        break;
      default:
        audio_mixer_reset_channel_pan(slot);
        break;
    }
  }

  drain_and_check(max_drain_frames);
  audio_mixer_destroy();
  return 0;
}
// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
