// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/AudioMixer.h"

// PCM audio sample mixing, buffer pointer arithmetic, and 16-bit integer
// saturation thresholds
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-avoid-magic-numbers, bugprone-narrowing-conversions, cppcoreguidelines-narrowing-conversions, misc-include-cleaner)
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>

namespace {

constexpr size_t AUDIO_BUFFER_SIZE = 16384;
constexpr size_t MAX_AUDIO_SLOTS = 8;
constexpr size_t MAX_CHANNELS_PER_SLOT = 16;
constexpr size_t BACKLOG_CUSHION = 6144;

// Lock-free single-producer single-consumer (SPSC) ring buffer structure
struct SampleBuffer_t {
  std::array<int16_t, AUDIO_BUFFER_SIZE> buffer{};
  std::atomic<size_t> read_index{0};
  std::atomic<size_t> write_index{0};
  std::atomic<uint32_t> flush_gen{0};
  uint32_t acked_flush_gen{0};
  int16_t last_left{0};
  int16_t last_right{0};
};

static auto sample_buffer_reinit(SampleBuffer_t* sb) -> void {
  if (sb == nullptr) return;
  sb->buffer.fill(0);
  sb->read_index.store(0, std::memory_order_relaxed);
  sb->write_index.store(0, std::memory_order_relaxed);
  sb->flush_gen.store(0, std::memory_order_relaxed);
  sb->acked_flush_gen = 0;
  sb->last_left = 0;
  sb->last_right = 0;
}

static auto sample_buffer_request_flush(SampleBuffer_t* sb) -> void {
  if (sb != nullptr) {
    sb->flush_gen.fetch_add(1, std::memory_order_release);
  }
}

static auto sample_buffer_check_flush(SampleBuffer_t* sb) -> void {
  if (sb == nullptr) {
    return;
  }
  const uint32_t gen = sb->flush_gen.load(std::memory_order_acquire);
  if (sb->acked_flush_gen != gen) {
    sb->acked_flush_gen = gen;
    const size_t w = sb->write_index.load(std::memory_order_acquire);
    sb->read_index.store(w, std::memory_order_release);
    sb->last_left = 0;
    sb->last_right = 0;
  }
}

static auto sample_buffer_get_filled(const SampleBuffer_t* sb) -> size_t {
  if (sb == nullptr) return 0;
  size_t r = sb->read_index.load(std::memory_order_relaxed);
  size_t w = sb->write_index.load(std::memory_order_acquire);
  if (r <= w) {
    return w - r;
  }
  return sb->buffer.size() + w - r;
}

static auto sample_buffer_get_free(const SampleBuffer_t* sb) -> size_t {
  if (sb == nullptr) return 0;
  size_t filled = sample_buffer_get_filled(sb);
  if (filled >= sb->buffer.size() - 1) {
    return 0;
  }
  return sb->buffer.size() - 1 - filled;
}

static auto sample_buffer_skip(SampleBuffer_t* sb, size_t len) -> void {
  if (sb == nullptr) return;
  size_t filled = sample_buffer_get_filled(sb);
  size_t num = (len < filled) ? len : filled;
  num &= ~1UL;
  if (num == 0) {
    return;
  }
  size_t r = sb->read_index.load(std::memory_order_relaxed);
  sb->read_index.store((r + num) % sb->buffer.size(),
                       std::memory_order_release);
}

static auto sample_buffer_upload(SampleBuffer_t* sb, const int16_t* src,
                                 size_t len) -> void {
  if (sb == nullptr || src == nullptr || len == 0) {
    return;
  }
  size_t free_space = sample_buffer_get_free(sb);
  size_t num = (len < free_space) ? len : (free_space & ~1UL);
  if (num == 0) {
    return;
  }

  size_t w = sb->write_index.load(std::memory_order_relaxed);
  if (w + num < sb->buffer.size()) {
    std::memcpy(&sb->buffer[w], src, num * sizeof(int16_t));
    sb->write_index.store(w + num, std::memory_order_release);
  } else {
    size_t len1 = sb->buffer.size() - w;
    std::memcpy(&sb->buffer[w], src, len1 * sizeof(int16_t));
    size_t len2 = num - len1;
    std::memcpy(&sb->buffer[0], src + len1, len2 * sizeof(int16_t));
    sb->write_index.store(len2, std::memory_order_release);
  }
}

static auto sample_buffer_drain_to(SampleBuffer_t* sb, int16_t* dest,
                                   size_t len, bool mix) -> void {
  if (sb == nullptr || dest == nullptr || len == 0) {
    return;
  }
  size_t available = sample_buffer_get_filled(sb);
  size_t num = (len < available) ? len : (available & ~1UL);

  size_t r = sb->read_index.load(std::memory_order_relaxed);
  auto process = [&](const int16_t* src, size_t count, size_t offset) -> void {
    if (mix) {
      for (size_t i = 0; i < count; ++i) {
        int32_t val = static_cast<int32_t>(dest[offset + i]) +
                      static_cast<int32_t>(src[i]);
        if (val > 32767) {
          val = 32767;
        } else if (val < -32768) {
          val = -32768;
        }
        dest[offset + i] = static_cast<int16_t>(val);
      }
    } else {
      std::memcpy(dest + offset, src, count * sizeof(int16_t));
    }
  };

  if (num > 0) {
    if (r + num < sb->buffer.size()) {
      process(&sb->buffer[r], num, 0);
      r += num;
    } else {
      size_t len1 = sb->buffer.size() - r;
      process(&sb->buffer[r], len1, 0);
      size_t len2 = num - len1;
      process(&sb->buffer[0], len2, len1);
      r = len2;
    }
    sb->read_index.store(r, std::memory_order_release);
    size_t last_r_idx = (r > 0) ? (r - 1) : (sb->buffer.size() - 1);
    size_t last_l_idx =
        (last_r_idx > 0) ? (last_r_idx - 1) : (sb->buffer.size() - 1);
    sb->last_left = sb->buffer[last_l_idx];
    sb->last_right = sb->buffer[last_r_idx];
  }

  // Smoothly fade out residual DC offset if audio underruns occur
  if (num < len) {
    constexpr int16_t fade_step = 800;
    for (size_t i = num; i < len; i += 2) {
      if (sb->last_left > 0) {
        sb->last_left = static_cast<int16_t>(
            (sb->last_left > fade_step) ? (sb->last_left - fade_step) : 0);
      } else if (sb->last_left < 0) {
        sb->last_left = static_cast<int16_t>(
            (sb->last_left < -fade_step) ? (sb->last_left + fade_step) : 0);
      }

      if (sb->last_right > 0) {
        sb->last_right = static_cast<int16_t>(
            (sb->last_right > fade_step) ? (sb->last_right - fade_step) : 0);
      } else if (sb->last_right < 0) {
        sb->last_right = static_cast<int16_t>(
            (sb->last_right < -fade_step) ? (sb->last_right + fade_step) : 0);
      }

      if (mix) {
        int32_t val_l =
            static_cast<int32_t>(dest[i]) + static_cast<int32_t>(sb->last_left);
        dest[i] =
            static_cast<int16_t>(std::max(-32768, std::min(32767, val_l)));

        if (i + 1 < len) {
          int32_t val_r = static_cast<int32_t>(dest[i + 1]) +
                          static_cast<int32_t>(sb->last_right);
          dest[i + 1] =
              static_cast<int16_t>(std::max(-32768, std::min(32767, val_r)));
        }
      } else {
        dest[i] = sb->last_left;
        if (i + 1 < len) {
          dest[i + 1] = sb->last_right;
        }
      }
    }
  }
}

struct ChannelPan_t {
  float left;
  float right;
  ChannelPan_t(float l = 1.0f, float r = 1.0f) : left(l), right(r) {}
};

struct AudioSourceSlot_t {
  bool active = false;
  PeripheralAudioInfo_t info{};
  std::array<ChannelPan_t, MAX_CHANNELS_PER_SLOT> pan{};
  std::unique_ptr<SampleBuffer_t> buffer;
};

static std::array<AudioSourceSlot_t, MAX_AUDIO_SLOTS> g_slots;
static AudioChannelTapCallback_t g_channel_tap_cb = nullptr;

}  // namespace

auto audio_mixer_initialize() -> void {
  for (size_t i = 0; i < MAX_AUDIO_SLOTS; ++i) {
    if (!g_slots[i].buffer) {
      g_slots[i].buffer = std::unique_ptr<SampleBuffer_t>(new SampleBuffer_t());
    }
    sample_buffer_reinit(g_slots[i].buffer.get());
    g_slots[i].active = false;
    for (size_t c = 0; c < MAX_CHANNELS_PER_SLOT; ++c) {
      g_slots[i].pan[c] = {1.0f, 1.0f};
    }
  }

  // Pre-configure Slot 0 (Speaker) as default
  g_slots[0].active = true;
  g_slots[0].info.num_channels = 1;
  std::strncpy(g_slots[0].info.channels[0].name, "Speaker",
               sizeof(g_slots[0].info.channels[0].name) - 1);
  g_slots[0].info.channels[0].default_pan_left = 1.0f;
  g_slots[0].info.channels[0].default_pan_right = 1.0f;
  g_slots[0].pan[0] = {1.0f, 1.0f};
}

auto audio_mixer_destroy() -> void {
  for (auto& slot : g_slots) {
    slot.buffer.reset();
    slot.active = false;
  }
  g_channel_tap_cb = nullptr;
}

auto audio_mixer_clear_buffers() -> void {
  for (auto& slot : g_slots) {
    if (slot.buffer) {
      sample_buffer_request_flush(slot.buffer.get());
    }
  }
}

auto audio_mixer_register_source(int slot, const char* peripheral_id,
                                 const PeripheralAudioInfo_t* info) -> void {
  (void)peripheral_id;
  if (slot < 0 || slot >= static_cast<int>(MAX_AUDIO_SLOTS) ||
      info == nullptr) {
    return;
  }
  auto& s = g_slots[static_cast<size_t>(slot)];
  s.active = true;
  s.info = *info;
  for (size_t c = 0; c < MAX_CHANNELS_PER_SLOT; ++c) {
    if (c < s.info.num_channels) {
      s.pan[c].left = s.info.channels[c].default_pan_left;
      s.pan[c].right = s.info.channels[c].default_pan_right;
    } else {
      s.pan[c] = {1.0f, 1.0f};
    }
  }
  if (!s.buffer) {
    s.buffer = std::unique_ptr<SampleBuffer_t>(new SampleBuffer_t());
  }
  sample_buffer_reinit(s.buffer.get());
}

auto audio_mixer_unregister_source(int slot) -> void {
  if (slot < 0 || slot >= static_cast<int>(MAX_AUDIO_SLOTS)) {
    return;
  }
  auto& s = g_slots[static_cast<size_t>(slot)];
  s.active = false;
  if (s.buffer) {
    sample_buffer_reinit(s.buffer.get());
  }
}

auto audio_mixer_upload_channels(const char* peripheral_id, int slot,
                                 const int16_t* const* channels,
                                 size_t num_channels, uint32_t num_samples)
    -> void {
  if (channels == nullptr || num_channels == 0 || num_samples == 0 ||
      slot < 0 || slot >= static_cast<int>(MAX_AUDIO_SLOTS)) {
    return;
  }

  if (g_channel_tap_cb != nullptr) {
    g_channel_tap_cb(peripheral_id, slot, channels, num_channels, num_samples);
  }

  auto& s = g_slots[static_cast<size_t>(slot)];
  if (!s.buffer) {
    return;
  }

  if (!s.active) {
    s.active = true;
    for (size_t c = 0; c < MAX_CHANNELS_PER_SLOT; ++c) {
      s.pan[c] = {1.0f, 1.0f};
    }
  }

  constexpr size_t CHUNK_FRAMES = 512;
  size_t offset = 0;
  while (offset < num_samples) {
    size_t chunk =
        std::min(static_cast<size_t>(num_samples - offset), CHUNK_FRAMES);
    std::array<int16_t, CHUNK_FRAMES * 2> stereo_chunk{};

    for (size_t i = 0; i < chunk; ++i) {
      float l_acc = 0.0f;
      float r_acc = 0.0f;
      for (size_t c = 0; c < num_channels; ++c) {
        if (channels[c] == nullptr) continue;
        float sample = static_cast<float>(channels[c][offset + i]);
        float pan_l = (c < MAX_CHANNELS_PER_SLOT) ? s.pan[c].left : 1.0f;
        float pan_r = (c < MAX_CHANNELS_PER_SLOT) ? s.pan[c].right : 1.0f;
        l_acc += sample * pan_l;
        r_acc += sample * pan_r;
      }
      int32_t l_val =
          static_cast<int32_t>(std::max(-32768.0f, std::min(32767.0f, l_acc)));
      int32_t r_val =
          static_cast<int32_t>(std::max(-32768.0f, std::min(32767.0f, r_acc)));
      stereo_chunk[i * 2] = static_cast<int16_t>(l_val);
      stereo_chunk[i * 2 + 1] = static_cast<int16_t>(r_val);
    }

    sample_buffer_upload(s.buffer.get(), stereo_chunk.data(), chunk * 2);
    offset += chunk;
  }
}

auto audio_mixer_set_channel_pan(int slot, size_t channel, float left,
                                 float right) -> void {
  if (slot >= 0 && slot < static_cast<int>(MAX_AUDIO_SLOTS) &&
      channel < MAX_CHANNELS_PER_SLOT) {
    g_slots[static_cast<size_t>(slot)].pan[channel].left = left;
    g_slots[static_cast<size_t>(slot)].pan[channel].right = right;
  }
}

auto audio_mixer_get_channel_pan(int slot, size_t channel, float* left,
                                 float* right) -> void {
  if (slot >= 0 && slot < static_cast<int>(MAX_AUDIO_SLOTS) &&
      channel < MAX_CHANNELS_PER_SLOT) {
    const auto& s = g_slots[static_cast<size_t>(slot)];
    if (left != nullptr) {
      *left = s.pan[channel].left;
    }
    if (right != nullptr) {
      *right = s.pan[channel].right;
    }
  }
}

auto audio_mixer_reset_channel_pan(int slot) -> void {
  if (slot >= 0 && slot < static_cast<int>(MAX_AUDIO_SLOTS)) {
    auto& s = g_slots[static_cast<size_t>(slot)];
    for (size_t c = 0; c < MAX_CHANNELS_PER_SLOT; ++c) {
      if (c < s.info.num_channels) {
        s.pan[c].left = s.info.channels[c].default_pan_left;
        s.pan[c].right = s.info.channels[c].default_pan_right;
      } else {
        s.pan[c] = {1.0f, 1.0f};
      }
    }
  }
}

auto audio_mixer_set_channel_tap_callback(AudioChannelTapCallback_t cb)
    -> void {
  g_channel_tap_cb = cb;
}

auto audio_mixer_set_fade(FadeType_t fade_type) -> void { (void)fade_type; }

auto audio_mixer_get_samples(int16_t* out, size_t num_samples) -> void {
  if (out == nullptr || num_samples == 0) {
    return;
  }

  bool any_samples = false;
  for (size_t slot = 0; slot < MAX_AUDIO_SLOTS; ++slot) {
    if (g_slots[slot].active && g_slots[slot].buffer) {
      if (sample_buffer_get_filled(g_slots[slot].buffer.get()) > 0) {
        any_samples = true;
        break;
      }
    }
  }

  if (!any_samples) {
    std::memset(out, 0, num_samples * sizeof(int16_t));
    for (size_t slot = 0; slot < MAX_AUDIO_SLOTS; ++slot) {
      if (g_slots[slot].buffer) {
        g_slots[slot].buffer->last_left = 0;
        g_slots[slot].buffer->last_right = 0;
      }
    }
    return;
  }

  std::memset(out, 0, num_samples * sizeof(int16_t));

  const size_t target_backlog =
      std::min(num_samples + BACKLOG_CUSHION,
               static_cast<size_t>(AUDIO_BUFFER_SIZE - 2));

  for (size_t slot = 0; slot < MAX_AUDIO_SLOTS; ++slot) {
    if (!g_slots[slot].active) continue;
    auto* sb = g_slots[slot].buffer.get();
    if (sb == nullptr) continue;

    sample_buffer_check_flush(sb);

    size_t filled = sample_buffer_get_filled(sb);
    if (filled > target_backlog) {
      sample_buffer_skip(sb, (filled - target_backlog) & ~1UL);
    }

    sample_buffer_drain_to(sb, out, num_samples, true);
  }
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-avoid-magic-numbers, bugprone-narrowing-conversions, cppcoreguidelines-narrowing-conversions, misc-include-cleaner)
