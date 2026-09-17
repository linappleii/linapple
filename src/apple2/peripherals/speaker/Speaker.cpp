// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/speaker/Speaker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "apple2/Apple2Types.h"
#include "apple2/peripherals/speaker/SpeakerCommands.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/Peripheral_Types.h"

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;

namespace {

// Hardware MMIO Mapping
constexpr uint16_t speaker_io_address = 0xC030;

// Per-Update Slice Capacity Limits
constexpr size_t speaker_max_events_per_update = 8192;
constexpr size_t speaker_max_samples_per_update = 4096;

// DSP & Analog Cone Modeling Parameters
constexpr float speaker_dsp_scale = 16384.0f;
constexpr float dc_blocker_coefficient = 0.999f;
constexpr float spindown_decay_coefficient = 0.99f;
constexpr float spindown_silence_epsilon = 0.001f;

struct SpeakerEvent_t {
  uint64_t cycle = 0;
  bool state = false;
};

struct SpeakerPeripheral_t {
  // --- Seam 3 Host Interface ---
  HostInterface_t* host = nullptr;

  // --- Hardware Latch & Inactivity Watchdog ---
  uint64_t quiet_cycle_count = 0;
  bool current_state = false;
  bool is_active = false;
  bool has_strobe = false;
  bool last_sample_state = false;

  // --- DSP Filter & Phase Accumulator ---
  double next_sample_cycle = 0.0;
  float filter_state = 0.0f;
  float previous_input = 0.0f;

  // --- Per-Update Synthesis Buffers & Queues ---
  uint32_t event_count = 0;
  std::array<int16_t, speaker_max_samples_per_update> sample_buffer{};
  std::array<SpeakerEvent_t, speaker_max_events_per_update> events{};

  // --- Legacy Snapshot Compatibility ---
  // Deprecated: Retained solely for backwards-compatible serialization with
  // the legacy SsIoSpeaker_t snapshot format. Active emulation timing is
  // driven by explicit elapsed cycle stepping.
  uint64_t last_update_cycle = 0;

  SpeakerPeripheral_t() = default;
};

// --- Internal Helpers ---

static auto get_cycles(HostInterface_t* host) -> uint64_t {
  if (host != nullptr && host->GetCycles != nullptr) {
    return host->GetCycles();
  }
  return 0;
}

static auto get_clock_hz(HostInterface_t* host) -> double {
  if (host != nullptr && host->GetClockHz != nullptr) {
    return host->GetClockHz();
  }
  return CLOCK_6502;
}

static auto clamp_sample(float filter_value) -> int16_t {
  const float raw_sample = filter_value * speaker_dsp_scale;
  const float clamped_sample =
      std::max(-32768.0f, std::min(32767.0f, raw_sample));
  return static_cast<int16_t>(clamped_sample);
}

static auto update_activity(SpeakerPeripheral_t& speaker,
                            uint32_t elapsed_cycles) -> void {
  speaker.last_update_cycle = get_cycles(speaker.host);

  if (speaker.has_strobe) {
    speaker.is_active = true;
    speaker.quiet_cycle_count = 0;
    speaker.has_strobe = false;
    return;
  }

  if (!speaker.is_active) {
    return;
  }

  speaker.quiet_cycle_count += elapsed_cycles;

  const uint64_t inactivity_threshold =
      static_cast<uint64_t>(get_clock_hz(speaker.host) / 5.0);
  if (speaker.quiet_cycle_count > inactivity_threshold) {
    speaker.is_active = false;
  }
}

static auto synthesize_spindown_samples(SpeakerPeripheral_t& speaker,
                                        uint64_t end_cycle,
                                        double cycles_per_sample) -> size_t {
  size_t sample_count = 0;
  while (speaker.next_sample_cycle <= static_cast<double>(end_cycle) &&
         sample_count < speaker_max_samples_per_update) {
    if (std::abs(speaker.filter_state) > spindown_silence_epsilon) {
      speaker.filter_state *= spindown_decay_coefficient;
      speaker.sample_buffer[sample_count++] =
          clamp_sample(speaker.filter_state);
    } else {
      speaker.filter_state = 0.0f;
      speaker.sample_buffer[sample_count++] = 0;
    }
    speaker.next_sample_cycle += cycles_per_sample;
  }
  return sample_count;
}

static auto synthesize_active_samples(SpeakerPeripheral_t& speaker,
                                      uint64_t end_cycle,
                                      double cycles_per_sample) -> size_t {
  const uint32_t available_events = speaker.event_count;
  uint32_t event_index = 0;
  size_t sample_count = 0;

  while (speaker.next_sample_cycle <= static_cast<double>(end_cycle) &&
         sample_count < speaker_max_samples_per_update) {
    const double sample_start = speaker.next_sample_cycle;
    const double sample_end = speaker.next_sample_cycle + cycles_per_sample;

    double sum = 0.0;
    double current_time = sample_start;

    while (event_index < available_events &&
           static_cast<double>(speaker.events[event_index].cycle) <
               sample_end) {
      const auto& event = speaker.events[event_index];
      const auto event_time = static_cast<double>(event.cycle);

      if (event_time <= sample_start) {
        speaker.last_sample_state = event.state;
      } else {
        sum += (event_time - current_time) *
               (speaker.last_sample_state ? 1.0 : -1.0);
        speaker.last_sample_state = event.state;
        current_time = event_time;
      }
      event_index++;
    }

    sum +=
        (sample_end - current_time) * (speaker.last_sample_state ? 1.0 : -1.0);

    const auto average = static_cast<float>(sum / cycles_per_sample);

    speaker.filter_state = (average - speaker.previous_input) +
                           (dc_blocker_coefficient * speaker.filter_state);
    speaker.previous_input = average;

    speaker.sample_buffer[sample_count++] = clamp_sample(speaker.filter_state);
    speaker.next_sample_cycle += cycles_per_sample;
  }
  return sample_count;
}

static auto generate_samples(SpeakerPeripheral_t& speaker, void* instance,
                             uint32_t elapsed_cycles) -> void {
  if (elapsed_cycles == 0) {
    return;
  }

  const double cycles_per_sample =
      get_clock_hz(speaker.host) /
      static_cast<double>(PERIPHERAL_AUDIO_DEFAULT_SAMPLE_RATE);
  if (cycles_per_sample <= 0.0) {
    return;
  }

  const uint64_t end_cycle = get_cycles(speaker.host);
  const uint64_t start_cycle =
      (end_cycle >= elapsed_cycles) ? (end_cycle - elapsed_cycles) : 0;

  if (speaker.next_sample_cycle < static_cast<double>(start_cycle) ||
      speaker.next_sample_cycle >
          static_cast<double>(end_cycle) + (2.0 * cycles_per_sample)) {
    speaker.next_sample_cycle = static_cast<double>(start_cycle);
  }

  const size_t sample_count =
      speaker.is_active
          ? synthesize_active_samples(speaker, end_cycle, cycles_per_sample)
          : synthesize_spindown_samples(speaker, end_cycle, cycles_per_sample);

  speaker.event_count = 0;

  if (sample_count > 0 && speaker.host != nullptr &&
      speaker.host->AudioPushChannels != nullptr) {
    const int16_t* channel_ptrs[1] = {speaker.sample_buffer.data()};
    speaker.host->AudioPushChannels(instance, channel_ptrs, 1, sample_count);
  }
}

static auto query_is_active(void* instance, void* out, size_t* out_size)
    -> PeripheralStatus_t {
  constexpr size_t required_size = sizeof(uint8_t);
  if (out == nullptr) {
    *out_size = required_size;
    return peripheral_ok;
  }
  if (instance == nullptr || *out_size < required_size) {
    *out_size = required_size;
    return peripheral_error;
  }
  const auto& speaker = *static_cast<const SpeakerPeripheral_t*>(instance);
  *static_cast<uint8_t*>(out) = speaker.is_active ? 1 : 0;
  *out_size = required_size;
  return peripheral_ok;
}

static auto query_audio_info(void* out, size_t* out_size)
    -> PeripheralStatus_t {
  constexpr size_t required_size = sizeof(PeripheralAudioInfo_t);
  if (out == nullptr) {
    *out_size = required_size;
    return peripheral_ok;
  }
  if (*out_size < required_size) {
    *out_size = required_size;
    return peripheral_error;
  }
  std::memset(out, 0, required_size);
  auto& info = *static_cast<PeripheralAudioInfo_t*>(out);
  info.sample_rate = PERIPHERAL_AUDIO_DEFAULT_SAMPLE_RATE;
  info.num_channels = 1;
  std::strncpy(info.channels[0].name, "Speaker",
               sizeof(info.channels[0].name) - 1);
  info.channels[0].default_pan_left = 1.0f;
  info.channels[0].default_pan_right = 1.0f;
  *out_size = required_size;
  return peripheral_ok;
}

// --- Hardware Direct I/O Handler ---

auto speaker_toggle(void* instance, uint16_t program_counter,
                    uint16_t memory_address, uint8_t is_write,
                    uint8_t data_value, uint32_t remaining_cycles) -> uint8_t {
  (void)program_counter;
  (void)memory_address;
  (void)is_write;
  (void)data_value;
  if (instance == nullptr) {
    return mem_read_floating_bus(remaining_cycles);
  }
  auto& speaker = *static_cast<SpeakerPeripheral_t*>(instance);

  speaker.has_strobe = true;
  speaker.is_active = true;
  speaker.current_state = !speaker.current_state;

  if (speaker.event_count < speaker_max_events_per_update) {
    auto& event = speaker.events[speaker.event_count++];
    event.cycle = get_cycles(speaker.host);
    event.state = speaker.current_state;
  }

  return mem_read_floating_bus(remaining_cycles);
}

// --- ABI Implementation ---

auto speaker_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto& speaker = *static_cast<SpeakerPeripheral_t*>(instance);

  speaker.event_count = 0;
  speaker.current_state = false;
  speaker.quiet_cycle_count = 0;
  speaker.is_active = false;
  speaker.has_strobe = false;
  speaker.last_sample_state = false;
  speaker.filter_state = 0.0f;
  speaker.previous_input = 0.0f;
  speaker.last_update_cycle = get_cycles(speaker.host);
  speaker.next_sample_cycle = static_cast<double>(speaker.last_update_cycle);
}

auto speaker_abi_init(int slot, HostInterface_t* host) -> void* {
  (void)slot;
  if (host == nullptr) {
    return nullptr;
  }

  auto speaker = std::unique_ptr<SpeakerPeripheral_t>(
      new (std::nothrow) SpeakerPeripheral_t());
  if (!speaker) {
    return nullptr;
  }

  speaker->host = host;
  speaker_reset(speaker.get());

  if (host->RegisterDirectIO != nullptr) {
    host->RegisterDirectIO(speaker.get(), speaker_io_address, speaker_toggle,
                           speaker_toggle);
  }

  return speaker.release();
}

auto speaker_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  delete static_cast<SpeakerPeripheral_t*>(instance);
}

auto speaker_think(void* instance, uint32_t elapsed_cycles) -> void {
  if (instance == nullptr) {
    return;
  }
  auto& speaker = *static_cast<SpeakerPeripheral_t*>(instance);
  update_activity(speaker, elapsed_cycles);
  generate_samples(speaker, instance, elapsed_cycles);
}

auto speaker_save_state(void* instance, void* state_buffer, size_t* buffer_size)
    -> PeripheralStatus_t {
  if (buffer_size == nullptr) {
    return peripheral_error;
  }
  const size_t required_size = sizeof(SsIoSpeaker_t);
  if (state_buffer == nullptr) {
    *buffer_size = required_size;
    return peripheral_ok;
  }
  if (instance == nullptr || *buffer_size < required_size) {
    return peripheral_error;
  }

  const auto& speaker = *static_cast<const SpeakerPeripheral_t*>(instance);
  auto& ss = *static_cast<SsIoSpeaker_t*>(state_buffer);
  ss.g_spkr_last_cycle = speaker.last_update_cycle;
  ss.quiet_cycle_count = speaker.quiet_cycle_count;
  ss.recently_active = speaker.is_active ? 1 : 0;
  ss.state = speaker.current_state ? 1 : 0;
  ss.next_sample_cycle = speaker.next_sample_cycle;
  ss.last_sample_state = speaker.last_sample_state ? 1 : 0;
  ss.filter_state = speaker.filter_state;

  *buffer_size = required_size;
  return peripheral_ok;
}

auto speaker_load_state(void* instance, const void* state_buffer,
                        size_t buffer_size) -> PeripheralStatus_t {
  const size_t required_size = sizeof(SsIoSpeaker_t);
  if (instance == nullptr || state_buffer == nullptr ||
      buffer_size != required_size) {
    return peripheral_error;
  }

  auto& speaker = *static_cast<SpeakerPeripheral_t*>(instance);
  const auto& ss = *static_cast<const SsIoSpeaker_t*>(state_buffer);
  speaker.last_update_cycle = ss.g_spkr_last_cycle;
  speaker.quiet_cycle_count = ss.quiet_cycle_count;
  speaker.is_active = (ss.recently_active != 0);
  speaker.current_state = (ss.state != 0);
  speaker.next_sample_cycle = ss.next_sample_cycle;
  speaker.last_sample_state = (ss.last_sample_state != 0);
  speaker.filter_state = ss.filter_state;

  if (!std::isfinite(speaker.filter_state)) {
    speaker.filter_state = 0.0f;
  }
  if (!std::isfinite(speaker.next_sample_cycle) ||
      speaker.next_sample_cycle < 0.0) {
    speaker.next_sample_cycle = static_cast<double>(speaker.last_update_cycle);
  }

  speaker.previous_input = speaker.last_sample_state ? 1.0f : -1.0f;
  speaker.has_strobe = false;
  speaker.event_count = 0;

  return peripheral_ok;
}

auto speaker_query(void* instance, uint32_t cmd_id, void* out, size_t* out_size)
    -> PeripheralStatus_t {
  if (out_size == nullptr) {
    return peripheral_error;
  }

  if (cmd_id == speaker_query_is_active) {
    return query_is_active(instance, out, out_size);
  }

  if (cmd_id == PERIPHERAL_QUERY_AUDIO_INFO) {
    return query_audio_info(out, out_size);
  }

  return peripheral_incompatible;
}

static Peripheral_t g_speaker_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.speaker",
    .name = "Speaker",
    .description = "Built-in Apple II speaker and cassette port emulation",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_INTERNAL,
    .default_slot = 0,
    .init = speaker_abi_init,
    .reset = speaker_reset,
    .shutdown = speaker_shutdown,
    .think = speaker_think,
    .on_vblank = nullptr,
    .save_state = speaker_save_state,
    .load_state = speaker_load_state,
    .command = nullptr,
    .query = speaker_query};

}  // namespace

auto speaker_get_descriptor() -> Peripheral_t* { return &g_speaker_peripheral; }

PERIPHERAL_REGISTER(g_speaker_peripheral)
