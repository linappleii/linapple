// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/AudioMixer.h"

// Normalized float sample mixing, buffer pointer arithmetic, and the single
// conversion to 16-bit PCM
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-avoid-magic-numbers, bugprone-narrowing-conversions, cppcoreguidelines-narrowing-conversions, misc-include-cleaner)
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "core/LinAppleCore.h"
#include "core/Log.h"

namespace {

constexpr size_t MAX_AUDIO_SLOTS = 8;
constexpr size_t MAX_CHANNELS_PER_SLOT = 16;
constexpr size_t MIX_ACCUMULATOR_SAMPLES = 4096;

// Latency belongs in time, not in samples. 16384 and 6144 interleaved samples
// were a fixed backlog only at 44100; at 192000 they would be a quarter of
// the intended latency and at 22050 twice it.
//
// These are milliseconds of audio. The ring holds interleaved stereo pairs,
// so a millisecond of audio is two samples, and the conversion below doubles
// deliberately: a listener hears time, not sample counts.
constexpr size_t mixer_capacity_ms = 185;
constexpr size_t mixer_cushion_ms = 70;

constexpr size_t RESAMPLE_CHUNK_FRAMES = 512;
// One extra frame of slack: a fractional step can complete an output frame
// from the partial window left over by the previous chunk.
constexpr size_t RESAMPLE_SCRATCH_FRAMES = RESAMPLE_CHUNK_FRAMES + 2;

static uint32_t g_output_rate_hz = 0;
static size_t g_capacity_samples = 0;
static size_t g_cushion_samples = 0;

// Symmetric rails: -1.0 and +1.0 map to -32767 and +32767, so a full-scale
// signal stays symmetric instead of gaining a half-LSB bias from -32768.
static auto float_to_pcm16(float v) -> int16_t {
  const long scaled = std::lroundf(v * 32767.0f);
  if (scaled > 32767) {
    return 32767;
  }
  if (scaled < -32767) {
    return -32767;
  }
  return static_cast<int16_t>(scaled);
}

// A float decaying toward zero lands in the denormal range, where arithmetic
// is dramatically slower on some CPUs. Nothing audible lives this far down.
constexpr float DENORMAL_FLOOR = 1e-20f;

static auto flush_denormal(float v) -> float {
  return (v > -DENORMAL_FLOOR && v < DENORMAL_FLOOR) ? 0.0f : v;
}

// Lock-free single-producer single-consumer (SPSC) ring buffer structure. The
// ring is sized from the output rate, so it is allocated at initialize time
// and never resized while the audio thread can see it.
struct SampleBuffer_t {
  std::vector<float> buffer;
  std::atomic<size_t> read_index{0};
  std::atomic<size_t> write_index{0};
  std::atomic<uint32_t> flush_gen{0};
  uint32_t acked_flush_gen{0};
  float last_left{0.0f};
  float last_right{0.0f};
};

static auto sample_buffer_reinit(SampleBuffer_t* sb) -> void {
  if (sb == nullptr) return;
  sb->buffer.assign(g_capacity_samples, 0.0f);
  sb->read_index.store(0, std::memory_order_relaxed);
  sb->write_index.store(0, std::memory_order_relaxed);
  sb->flush_gen.store(0, std::memory_order_relaxed);
  sb->acked_flush_gen = 0;
  sb->last_left = 0.0f;
  sb->last_right = 0.0f;
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
    sb->last_left = 0.0f;
    sb->last_right = 0.0f;
  }
}

static auto sample_buffer_get_filled(const SampleBuffer_t* sb) -> size_t {
  if (sb == nullptr || sb->buffer.empty()) return 0;
  size_t r = sb->read_index.load(std::memory_order_relaxed);
  size_t w = sb->write_index.load(std::memory_order_acquire);
  if (r <= w) {
    return w - r;
  }
  return sb->buffer.size() + w - r;
}

static auto sample_buffer_get_free(const SampleBuffer_t* sb) -> size_t {
  if (sb == nullptr || sb->buffer.empty()) return 0;
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

static auto sample_buffer_upload(SampleBuffer_t* sb, const float* src,
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
    std::memcpy(&sb->buffer[w], src, num * sizeof(float));
    sb->write_index.store(w + num, std::memory_order_release);
  } else {
    size_t len1 = sb->buffer.size() - w;
    std::memcpy(&sb->buffer[w], src, len1 * sizeof(float));
    size_t len2 = num - len1;
    std::memcpy(&sb->buffer[0], src + len1, len2 * sizeof(float));
    sb->write_index.store(len2, std::memory_order_release);
  }
}

static auto sample_buffer_drain_to(SampleBuffer_t* sb, float* dest, size_t len,
                                   bool mix) -> void {
  if (sb == nullptr || dest == nullptr || len == 0) {
    return;
  }
  size_t available = sample_buffer_get_filled(sb);
  size_t num = (len < available) ? len : (available & ~1UL);

  size_t r = sb->read_index.load(std::memory_order_relaxed);
  auto process = [&](const float* src, size_t count, size_t offset) -> void {
    if (mix) {
      for (size_t i = 0; i < count; ++i) {
        dest[offset + i] = flush_denormal(dest[offset + i] + src[i]);
      }
    } else {
      std::memcpy(dest + offset, src, count * sizeof(float));
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
    // Normalized equivalent of the 800-per-frame step this has always used.
    constexpr float fade_step = 800.0f / 32767.0f;
    for (size_t i = num; i < len; i += 2) {
      if (sb->last_left > 0.0f) {
        sb->last_left =
            (sb->last_left > fade_step) ? (sb->last_left - fade_step) : 0.0f;
      } else if (sb->last_left < 0.0f) {
        sb->last_left =
            (sb->last_left < -fade_step) ? (sb->last_left + fade_step) : 0.0f;
      }

      if (sb->last_right > 0.0f) {
        sb->last_right =
            (sb->last_right > fade_step) ? (sb->last_right - fade_step) : 0.0f;
      } else if (sb->last_right < 0.0f) {
        sb->last_right =
            (sb->last_right < -fade_step) ? (sb->last_right + fade_step) : 0.0f;
      }

      if (mix) {
        dest[i] = flush_denormal(dest[i] + sb->last_left);
        if (i + 1 < len) {
          dest[i + 1] = flush_denormal(dest[i + 1] + sb->last_right);
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

// Per-channel resampler state. An upload rarely ends on an output-frame
// boundary, so the partial window has to survive between calls.
struct ResamplerState_t {
  double window_sum = 0.0;   // area accumulated into the frame in progress
  double window_span = 0.0;  // how much of that frame's window is covered
  double phase = 0.0;        // position between previous and current input
  float previous = 0.0f;
  bool primed = false;
};

struct AudioSourceSlot_t {
  // Read on the audio thread, written on the emulation thread.
  std::atomic<bool> active{false};
  PeripheralAudioInfo_t info{};
  std::array<ChannelPan_t, MAX_CHANNELS_PER_SLOT> pan{};
  std::array<ResamplerState_t, MAX_CHANNELS_PER_SLOT> resampler{};
  float gain = 1.0f;
  std::unique_ptr<SampleBuffer_t> buffer;
};

static std::array<AudioSourceSlot_t, MAX_AUDIO_SLOTS> g_slots;
static AudioChannelTapCallback_t g_channel_tap_cb = nullptr;

// A source declares its time base in one of two forms, and the mixer resolves
// both to Hz here. g_current_clk_6502 is the core's public API and the mixer is
// a frontend consuming it.
static auto source_rate_hz(const PeripheralAudioInfo_t& info) -> double {
  if (info.time_base == peripheral_audio_cpu_clocked) {
    const uint32_t divisor = (info.cycle_divisor == 0) ? 1 : info.cycle_divisor;
    return g_current_clk_6502 / static_cast<double>(divisor);
  }
  return static_cast<double>(info.sample_rate);
}

// Summing headroom is the mixer's, so a source that legitimately exceeds full
// scale is attenuated here rather than pre-attenuating itself. The speaker's
// 1/2.0 reproduces the old 16384-times-2 full-scale peak exactly.
static auto default_source_gain(const PeripheralAudioInfo_t& info) -> float {
  if (info.peak_magnitude > 0.0f) {
    return 1.0f / info.peak_magnitude;
  }
  return 1.0f;
}

// step is source samples per output sample: a fractional-window box average
// when the source runs faster than the device, linear interpolation when it
// runs slower. Neither is a windowed sinc and neither needs to be for a
// one-bit cone and a PSG, but both live behind this one function so the choice
// can be upgraded without touching callers. An integer window or dropped
// samples would alias audibly.
static auto resample_channel(ResamplerState_t& state, double step,
                             const float* in, size_t in_count, float* out,
                             size_t out_capacity) -> size_t {
  size_t produced = 0;

  if (step >= 1.0) {
    for (size_t i = 0; i < in_count && produced < out_capacity; ++i) {
      double remaining = 1.0;
      // step >= 1 means at most one output frame can complete per input
      // sample, so this runs at most twice.
      while (remaining > 0.0 && produced < out_capacity) {
        const double take = std::min(step - state.window_span, remaining);
        state.window_sum += static_cast<double>(in[i]) * take;
        state.window_span += take;
        remaining -= take;
        // Closing on a tolerance rather than on equality: the span is a sum of
        // fractions and can land a rounding error short of step, which would
        // stall the window forever.
        if (state.window_span >= step - 1e-9) {
          out[produced++] =
              static_cast<float>(state.window_sum / state.window_span);
          state.window_sum = 0.0;
          state.window_span = 0.0;
        }
      }
    }
    return produced;
  }

  for (size_t i = 0; i < in_count && produced < out_capacity; ++i) {
    const float current = in[i];
    if (!state.primed) {
      state.previous = current;
      state.primed = true;
    }
    while (state.phase < 1.0 && produced < out_capacity) {
      out[produced++] = static_cast<float>(
          state.previous + ((current - state.previous) * state.phase));
      state.phase += step;
    }
    state.phase -= 1.0;
    state.previous = current;
  }
  return produced;
}

}  // namespace

auto audio_mixer_initialize(uint32_t output_rate_hz) -> void {
  g_output_rate_hz = output_rate_hz;
  if (output_rate_hz == 0) {
    Logger::error("Audio mixer initialized with a zero output rate\n");
    g_capacity_samples = 0;
    g_cushion_samples = 0;
  } else {
    // Two samples per frame, and several length computations round down to an
    // even count, so the capacity is even too.
    g_capacity_samples =
        (static_cast<size_t>(output_rate_hz) * mixer_capacity_ms / 1000 * 2) &
        ~static_cast<size_t>(1);
    g_cushion_samples =
        static_cast<size_t>(output_rate_hz) * mixer_cushion_ms / 1000 * 2;
  }

  for (size_t i = 0; i < MAX_AUDIO_SLOTS; ++i) {
    if (!g_slots[i].buffer) {
      g_slots[i].buffer = std::unique_ptr<SampleBuffer_t>(new SampleBuffer_t());
    }
    sample_buffer_reinit(g_slots[i].buffer.get());
    g_slots[i].active.store(false, std::memory_order_relaxed);
    g_slots[i].info = PeripheralAudioInfo_t{};
    g_slots[i].gain = 1.0f;
    for (size_t c = 0; c < MAX_CHANNELS_PER_SLOT; ++c) {
      g_slots[i].pan[c] = {1.0f, 1.0f};
      g_slots[i].resampler[c] = ResamplerState_t{};
    }
  }
}

auto audio_mixer_destroy() -> void {
  for (auto& slot : g_slots) {
    slot.buffer.reset();
    slot.active.store(false, std::memory_order_relaxed);
  }
  g_channel_tap_cb = nullptr;
  g_output_rate_hz = 0;
  g_capacity_samples = 0;
  g_cushion_samples = 0;
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
  // A source that cannot say what its samples mean in time cannot be
  // resampled, and a zeroed PeripheralAudioInfo_t is invalid by construction
  // because the time-base enumeration starts at one.
  if (info->time_base != peripheral_audio_cpu_clocked &&
      info->time_base != peripheral_audio_absolute) {
    Logger::error(
        "Audio source in slot %d declares no valid time base; not "
        "registered\n",
        slot);
    return;
  }

  auto& s = g_slots[static_cast<size_t>(slot)];

  // A peripheral re-announcing an unchanged layout must not flush audio or
  // clobber a user pan or gain override. info is written only on the emulation
  // thread, so this comparison is safe while the audio thread is inside
  // audio_mixer_get_samples.
  const bool reconfiguring = s.active.load(std::memory_order_relaxed);
  if (reconfiguring && std::memcmp(&s.info, info, sizeof(s.info)) == 0) {
    return;
  }

  s.info = *info;
  s.gain = default_source_gain(s.info);
  for (auto& state : s.resampler) {
    state = ResamplerState_t{};
  }
  // Pan assignments made against the old channel count are meaningless, so a
  // genuine layout change resets them.
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
  if (reconfiguring) {
    // The audio thread owns read_index once the slot is live, so ask it to
    // drop the backlog rather than reinitializing the ring underneath it.
    sample_buffer_request_flush(s.buffer.get());
  } else {
    sample_buffer_reinit(s.buffer.get());
  }
  s.active.store(true, std::memory_order_release);
}

auto audio_mixer_unregister_source(int slot) -> void {
  if (slot < 0 || slot >= static_cast<int>(MAX_AUDIO_SLOTS)) {
    return;
  }
  auto& s = g_slots[static_cast<size_t>(slot)];
  s.active.store(false, std::memory_order_release);
  if (s.buffer) {
    sample_buffer_reinit(s.buffer.get());
  }
}

auto audio_mixer_upload_channels(const char* peripheral_id, int slot,
                                 const float* const* channels,
                                 size_t num_channels, uint32_t num_samples)
    -> void {
  if (channels == nullptr || num_channels == 0 ||
      num_channels > MAX_CHANNELS_PER_SLOT || num_samples == 0 || slot < 0 ||
      slot >= static_cast<int>(MAX_AUDIO_SLOTS)) {
    return;
  }
  for (size_t c = 0; c < num_channels; ++c) {
    if (channels[c] == nullptr) {
      return;
    }
  }

  // The tap sits ahead of gain, resampling and the output clip on purpose: it
  // is there to observe what the peripheral actually emitted.
  if (g_channel_tap_cb != nullptr) {
    g_channel_tap_cb(peripheral_id, slot, channels, num_channels, num_samples);
  }

  auto& s = g_slots[static_cast<size_t>(slot)];
  // An unannounced source has no declared time base, so there is no rate to
  // resample from and nothing honest to do with its samples.
  if (!s.buffer || !s.active.load(std::memory_order_relaxed) ||
      g_output_rate_hz == 0) {
    return;
  }

  const double step =
      source_rate_hz(s.info) / static_cast<double>(g_output_rate_hz);
  if (!(step > 0.0)) {
    return;
  }

  // Bound the input chunk so the resampled output cannot outrun the scratch,
  // which matters in the up-sampling direction where one input sample yields
  // several output frames.
  const size_t in_chunk = std::max<size_t>(
      1, static_cast<size_t>(static_cast<double>(RESAMPLE_CHUNK_FRAMES) *
                             std::min(step, 1.0)));

  // Uploads only ever arrive on the emulation thread, so one scratch buffer
  // serves them all rather than putting 33 KB on the stack.
  static std::array<std::array<float, RESAMPLE_SCRATCH_FRAMES>,
                    MAX_CHANNELS_PER_SLOT>
      resampled;

  size_t offset = 0;
  while (offset < num_samples) {
    const size_t take =
        std::min(static_cast<size_t>(num_samples) - offset, in_chunk);

    size_t produced = 0;
    for (size_t c = 0; c < num_channels; ++c) {
      produced =
          resample_channel(s.resampler[c], step, channels[c] + offset, take,
                           resampled[c].data(), RESAMPLE_SCRATCH_FRAMES);
    }

    std::array<float, RESAMPLE_SCRATCH_FRAMES * 2> stereo_chunk{};
    for (size_t i = 0; i < produced; ++i) {
      float l_acc = 0.0f;
      float r_acc = 0.0f;
      for (size_t c = 0; c < num_channels; ++c) {
        const float sample = resampled[c][i] * s.gain;
        l_acc += sample * s.pan[c].left;
        r_acc += sample * s.pan[c].right;
      }
      stereo_chunk[i * 2] = l_acc;
      stereo_chunk[i * 2 + 1] = r_acc;
    }

    sample_buffer_upload(s.buffer.get(), stereo_chunk.data(), produced * 2);
    offset += take;
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

auto audio_mixer_set_source_gain(int slot, float gain) -> void {
  if (slot >= 0 && slot < static_cast<int>(MAX_AUDIO_SLOTS)) {
    g_slots[static_cast<size_t>(slot)].gain = gain;
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
    if (g_slots[slot].active.load(std::memory_order_acquire) &&
        g_slots[slot].buffer) {
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
        g_slots[slot].buffer->last_left = 0.0f;
        g_slots[slot].buffer->last_right = 0.0f;
      }
    }
    return;
  }

  const size_t target_backlog =
      std::min(num_samples + g_cushion_samples, g_capacity_samples - 2);

  for (size_t slot = 0; slot < MAX_AUDIO_SLOTS; ++slot) {
    if (!g_slots[slot].active.load(std::memory_order_acquire)) continue;
    auto* sb = g_slots[slot].buffer.get();
    if (sb == nullptr) continue;

    sample_buffer_check_flush(sb);

    size_t filled = sample_buffer_get_filled(sb);
    if (filled > target_backlog) {
      sample_buffer_skip(sb, (filled - target_backlog) & ~1UL);
    }
  }

  // Every source sums into the float accumulator with unlimited headroom, so
  // the conversion below is the only clip in the whole audio path. The
  // accumulator is chunked rather than sized to the request because it lives
  // on the audio thread, where allocating is not an option.
  static std::array<float, MIX_ACCUMULATOR_SAMPLES> accumulator;

  size_t done = 0;
  while (done < num_samples) {
    const size_t chunk = std::min(num_samples - done, accumulator.size());
    std::fill_n(accumulator.begin(), chunk, 0.0f);

    for (size_t slot = 0; slot < MAX_AUDIO_SLOTS; ++slot) {
      if (!g_slots[slot].active.load(std::memory_order_acquire)) continue;
      auto* sb = g_slots[slot].buffer.get();
      if (sb == nullptr) continue;
      sample_buffer_drain_to(sb, accumulator.data(), chunk, true);
    }

    for (size_t i = 0; i < chunk; ++i) {
      out[done + i] = float_to_pcm16(accumulator[i]);
    }
    done += chunk;
  }
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-avoid-magic-numbers, bugprone-narrowing-conversions, cppcoreguidelines-narrowing-conversions, misc-include-cleaner)
