// SPDX-License-Identifier: GPL-2.0-only
#include "core/AudioMixer.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

#include "core/Common_Globals.h"

namespace {

struct SampleBuffer_t {
  std::vector<int16_t> buffer;
  std::atomic<size_t> read_index;
  std::atomic<size_t> write_index;
  int16_t last_value{0};

  explicit SampleBuffer_t(size_t size)
      : buffer(size), read_index(0), write_index(0) {}

  auto reinit() -> void {
    std::fill(buffer.begin(), buffer.end(), 0);
    read_index = 0;
    write_index = 0;
    last_value = 0;
  }

  auto skip(size_t len) -> void {
    size_t filled = get_filled();
    size_t num = (len < filled) ? len : filled;
    if (num == 0) {
      return;
    }
    size_t r = read_index.load(std::memory_order_relaxed);
    read_index.store((r + num) % buffer.size(), std::memory_order_release);
  }

  auto get_filled() const -> size_t {
    size_t r = read_index.load(std::memory_order_relaxed);
    size_t w = write_index.load(std::memory_order_relaxed);
    if (r <= w) {
      return w - r;
    }
    return buffer.size() + w - r;
  }

  auto get_free() const -> size_t {
    size_t filled = get_filled();
    if (filled >= buffer.size() - 1) {
      return 0;
    }
    return buffer.size() - 1 - filled;
  }

  auto upload(const int16_t* src, size_t len) -> void {
    size_t free = get_free();
    size_t num = (len < free) ? len : free;
    if (num == 0) {
      return;
    }

    size_t w = write_index.load(std::memory_order_relaxed);
    if (w + num < buffer.size()) {
      memcpy(&buffer[w], src, num * sizeof(int16_t));
      write_index.store(w + num, std::memory_order_release);
    } else {
      size_t len1 = buffer.size() - w;
      memcpy(&buffer[w], src, len1 * sizeof(int16_t));
      size_t len2 = num - len1;
      memcpy(&buffer[0], src + len1, len2 * sizeof(int16_t));
      write_index.store(len2, std::memory_order_release);
    }
  }

  auto drain_to(int16_t* dest, size_t len, bool mix) -> void {
    size_t available = get_filled();
    size_t num = (len < available) ? len : available;

    size_t r = read_index.load(std::memory_order_relaxed);
    auto process = [&](const int16_t* src, size_t count,
                       size_t offset) -> void {
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
        memcpy(dest + offset, src, count * sizeof(int16_t));
      }
    };

    if (num > 0) {
      if (r + num < buffer.size()) {
        process(&buffer[r], num, 0);
        r += num;
      } else {
        size_t len1 = buffer.size() - r;
        process(&buffer[r], len1, 0);
        size_t len2 = num - len1;
        process(&buffer[0], len2, len1);
        r = len2;
      }
      read_index.store(r, std::memory_order_release);
      last_value = (r > 0) ? buffer[r - 1] : buffer.back();
    }

    if (num < len) {
      for (size_t i = num; i < len; ++i) {
        if (last_value != 0) {
          const int16_t fade_step = 800;
          if (last_value > 0) {
            last_value =
                (last_value > fade_step) ? (last_value - fade_step) : 0;
          } else {
            last_value =
                (last_value < -fade_step) ? (last_value + fade_step) : 0;
          }
        }

        if (mix) {
          int32_t val =
              static_cast<int32_t>(dest[i]) + static_cast<int32_t>(last_value);
          if (val > 32767) {
            val = 32767;
          } else if (val < -32768) {
            val = -32768;
          }
          dest[i] = static_cast<int16_t>(val);
        } else {
          dest[i] = last_value;
        }
      }
    }
  }
};

static SampleBuffer_t* g_spkr_mix_buffer = nullptr;
static SampleBuffer_t* g_mock_mix_buffer = nullptr;

}  // namespace

auto audio_mixer_initialize() -> void {
  constexpr size_t buffer_size = 16384;
  if (g_spkr_mix_buffer == nullptr) {
    g_spkr_mix_buffer = new SampleBuffer_t(buffer_size);
  }
  if (g_mock_mix_buffer == nullptr) {
    g_mock_mix_buffer = new SampleBuffer_t(buffer_size);
  }
  g_spkr_mix_buffer->reinit();
  g_mock_mix_buffer->reinit();
}

auto audio_mixer_destroy() -> void {
  delete g_spkr_mix_buffer;
  delete g_mock_mix_buffer;
  g_spkr_mix_buffer = nullptr;
  g_mock_mix_buffer = nullptr;
}

auto audio_mixer_clear_buffers() -> void {
  if (g_spkr_mix_buffer != nullptr) {
    g_spkr_mix_buffer->reinit();
  }
  if (g_mock_mix_buffer != nullptr) {
    g_mock_mix_buffer->reinit();
  }
}

auto audio_mixer_set_fade(FadeType_t fade_type) -> void {
  (void)fade_type;
}

auto audio_mixer_upload_speaker_samples(const int16_t* buffer,
                                        uint32_t num_samples) -> void {
  if (g_spkr_mix_buffer != nullptr) {
    g_spkr_mix_buffer->upload(buffer, num_samples);
  }
}

auto audio_mixer_upload_mockingboard_samples(const int16_t* buffer,
                                             uint32_t num_samples) -> void {
  if (g_mock_mix_buffer != nullptr) {
    g_mock_mix_buffer->upload(buffer, num_samples);
  }
}

auto audio_mixer_get_samples(int16_t* out, size_t num_samples) -> void {
  if (g_spkr_mix_buffer == nullptr || g_mock_mix_buffer == nullptr) {
    memset(out, 0, num_samples * sizeof(int16_t));
    return;
  }

  const size_t target_backlog =
      std::min(num_samples + 1024, static_cast<size_t>(16384 - 2));

  size_t spkr_filled = g_spkr_mix_buffer->get_filled();
  if (spkr_filled > target_backlog) {
    g_spkr_mix_buffer->skip(spkr_filled - target_backlog);
  }

  size_t mock_filled = g_mock_mix_buffer->get_filled();
  if (mock_filled > target_backlog) {
    g_mock_mix_buffer->skip(mock_filled - target_backlog);
  }

  if (g_spkr_mix_buffer->get_filled() == 0 &&
      g_mock_mix_buffer->get_filled() == 0) {
    memset(out, 0, num_samples * sizeof(int16_t));
    g_spkr_mix_buffer->last_value = 0;
    g_mock_mix_buffer->last_value = 0;
    return;
  }

  g_spkr_mix_buffer->drain_to(out, num_samples, false);
  g_mock_mix_buffer->drain_to(out, num_samples, true);
}
