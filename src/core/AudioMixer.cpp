// SPDX-License-Identifier: GPL-2.0-only
#include "core/AudioMixer.h"

// PCM audio sample mixing, buffer pointer arithmetic, and 16-bit integer
// saturation thresholds
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-avoid-magic-numbers, bugprone-narrowing-conversions,
// cppcoreguidelines-narrowing-conversions, misc-include-cleaner)
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>

namespace {

constexpr size_t AUDIO_BUFFER_SIZE = 16384;

// Lock-free single-producer single-consumer (SPSC) ring buffer structure
struct SampleBuffer_t {
  std::array<int16_t, AUDIO_BUFFER_SIZE> buffer{};
  std::atomic<size_t> read_index{0};
  std::atomic<size_t> write_index{0};
  std::atomic<uint32_t> flush_gen{0};
  uint32_t acked_flush_gen{0};
  int16_t last_value{0};
};

static auto sample_buffer_reinit(SampleBuffer_t* sb) -> void {
  sb->buffer.fill(0);
  sb->read_index.store(0, std::memory_order_relaxed);
  sb->write_index.store(0, std::memory_order_relaxed);
  sb->flush_gen.store(0, std::memory_order_relaxed);
  sb->acked_flush_gen = 0;
  sb->last_value = 0;
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
    sb->last_value = 0;
  }
}

static auto sample_buffer_get_filled(const SampleBuffer_t* sb) -> size_t {
  // Synchronize memory order with producer writes to prevent speculative
  // payload reads
  size_t r = sb->read_index.load(std::memory_order_relaxed);
  size_t w = sb->write_index.load(std::memory_order_acquire);
  if (r <= w) {
    return w - r;
  }
  return sb->buffer.size() + w - r;
}

static auto sample_buffer_get_free(const SampleBuffer_t* sb) -> size_t {
  size_t filled = sample_buffer_get_filled(sb);
  if (filled >= sb->buffer.size() - 1) {
    return 0;
  }
  return sb->buffer.size() - 1 - filled;
}

static auto sample_buffer_skip(SampleBuffer_t* sb, size_t len) -> void {
  size_t filled = sample_buffer_get_filled(sb);
  size_t num = (len < filled) ? len : filled;
  if (num == 0) {
    return;
  }
  size_t r = sb->read_index.load(std::memory_order_relaxed);
  sb->read_index.store((r + num) % sb->buffer.size(),
                       std::memory_order_release);
}

static auto sample_buffer_upload(SampleBuffer_t* sb, const int16_t* src,
                                 size_t len) -> void {
  if (src == nullptr || len == 0) {
    return;
  }
  size_t free_space = sample_buffer_get_free(sb);
  size_t num = (len < free_space) ? len : free_space;
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
  if (dest == nullptr || len == 0) {
    return;
  }
  size_t available = sample_buffer_get_filled(sb);
  size_t num = (len < available) ? len : available;

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
    sb->last_value = (r > 0) ? sb->buffer[r - 1] : sb->buffer.back();
  }

  // Smoothly fade out residual DC offset if audio underruns occur
  if (num < len) {
    for (size_t i = num; i < len; ++i) {
      if (sb->last_value != 0) {
        constexpr int16_t fade_step = 800;
        if (sb->last_value > 0) {
          sb->last_value = static_cast<int16_t>(
              (sb->last_value > fade_step) ? (sb->last_value - fade_step) : 0);
        } else {
          sb->last_value = static_cast<int16_t>(
              (sb->last_value < -fade_step) ? (sb->last_value + fade_step) : 0);
        }
      }

      if (mix) {
        int32_t val = static_cast<int32_t>(dest[i]) +
                      static_cast<int32_t>(sb->last_value);
        if (val > 32767) {
          val = 32767;
        } else if (val < -32768) {
          val = -32768;
        }
        dest[i] = static_cast<int16_t>(val);
      } else {
        dest[i] = sb->last_value;
      }
    }
  }
}

static std::unique_ptr<SampleBuffer_t> g_spkr_mix_buffer;
static std::unique_ptr<SampleBuffer_t> g_mock_mix_buffer;

}  // namespace

auto audio_mixer_initialize() -> void {
  if (!g_spkr_mix_buffer) {
    g_spkr_mix_buffer = std::unique_ptr<SampleBuffer_t>(new SampleBuffer_t());
  }
  if (!g_mock_mix_buffer) {
    g_mock_mix_buffer = std::unique_ptr<SampleBuffer_t>(new SampleBuffer_t());
  }
  sample_buffer_reinit(g_spkr_mix_buffer.get());
  sample_buffer_reinit(g_mock_mix_buffer.get());
}

auto audio_mixer_destroy() -> void {
  g_spkr_mix_buffer.reset();
  g_mock_mix_buffer.reset();
}

auto audio_mixer_clear_buffers() -> void {
  if (g_spkr_mix_buffer) {
    sample_buffer_request_flush(g_spkr_mix_buffer.get());
  }
  if (g_mock_mix_buffer) {
    sample_buffer_request_flush(g_mock_mix_buffer.get());
  }
}

auto audio_mixer_set_fade(FadeType_t fade_type) -> void { (void)fade_type; }

auto audio_mixer_upload_speaker_samples(const int16_t* buffer,
                                        uint32_t num_samples) -> void {
  if (g_spkr_mix_buffer && buffer != nullptr && num_samples > 0) {
    sample_buffer_upload(g_spkr_mix_buffer.get(), buffer, num_samples);
  }
}

auto audio_mixer_upload_mockingboard_samples(const int16_t* buffer,
                                             uint32_t num_samples) -> void {
  if (g_mock_mix_buffer && buffer != nullptr && num_samples > 0) {
    sample_buffer_upload(g_mock_mix_buffer.get(), buffer, num_samples);
  }
}

auto audio_mixer_get_samples(int16_t* out, size_t num_samples) -> void {
  if (out == nullptr || num_samples == 0) {
    return;
  }
  if (!g_spkr_mix_buffer || !g_mock_mix_buffer) {
    std::memset(out, 0, num_samples * sizeof(int16_t));
    return;
  }

  sample_buffer_check_flush(g_spkr_mix_buffer.get());
  sample_buffer_check_flush(g_mock_mix_buffer.get());

  // Throttle backlog to target capacity to maintain low latency during bursts
  const size_t target_backlog =
      std::min(num_samples + 1024, static_cast<size_t>(AUDIO_BUFFER_SIZE - 2));

  size_t spkr_filled = sample_buffer_get_filled(g_spkr_mix_buffer.get());
  if (spkr_filled > target_backlog) {
    sample_buffer_skip(g_spkr_mix_buffer.get(), spkr_filled - target_backlog);
  }

  size_t mock_filled = sample_buffer_get_filled(g_mock_mix_buffer.get());
  if (mock_filled > target_backlog) {
    sample_buffer_skip(g_mock_mix_buffer.get(), mock_filled - target_backlog);
  }

  if (sample_buffer_get_filled(g_spkr_mix_buffer.get()) == 0 &&
      sample_buffer_get_filled(g_mock_mix_buffer.get()) == 0) {
    std::memset(out, 0, num_samples * sizeof(int16_t));
    g_spkr_mix_buffer->last_value = 0;
    g_mock_mix_buffer->last_value = 0;
    return;
  }

  sample_buffer_drain_to(g_spkr_mix_buffer.get(), out, num_samples, false);
  sample_buffer_drain_to(g_mock_mix_buffer.get(), out, num_samples, true);
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-avoid-magic-numbers, bugprone-narrowing-conversions,
// cppcoreguidelines-narrowing-conversions, misc-include-cleaner)
