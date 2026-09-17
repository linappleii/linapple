// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-owning-memory)
#include "apple2/peripherals/speaker/Speaker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "apple2/peripherals/speaker/SpeakerCommands.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;

#ifndef VERSIONSTRING
#define VERSIONSTRING "3.1.0"
#endif

namespace {

constexpr size_t max_speaker_events = 16384;
constexpr size_t speaker_buffer_size = 16384;

enum { sound_none = 0, sound_wave = 1 };

struct SpeakerEvent_t {
  uint64_t cycle = 0;
  bool state = false;
};

constexpr int16_t speaker_sample_volume = 0x4000;

constexpr uint32_t default_sample_rate = 44100;
constexpr double ntsc_clock_hz = ((157500000.0 / 11.0) * 65.0) / 912.0;
constexpr double pal_clock_hz = 1015625.0;
constexpr uint64_t ntsc_frame_cycles = 17030;
constexpr uint64_t pal_frame_cycles = 20280;
constexpr uint64_t vbl_tolerance = 256;

constexpr float dc_blocker_coefficient = 0.999f;
constexpr float decay_coefficient = 0.99f;
constexpr float filter_epsilon = 0.001f;

struct SpeakerPeripheral_t {
  std::array<SpeakerEvent_t, max_speaker_events> events{};
  uint32_t event_count = 0;
  bool current_state = false;
  uint64_t last_update_cycle = 0;
  uint64_t quiet_cycle_count = 0;
  bool is_active = false;
  bool has_strobe = false;
  uint32_t sound_mode = sound_wave;

  bool last_sample_state = false;
  double next_sample_cycle = 0.0;
  std::array<int16_t, speaker_buffer_size> sample_buffer{};

  float filter_state = 0.0f;
  float previous_input = 0.0f;

  double current_clock_hz = ntsc_clock_hz;
  uint64_t last_vblank_cycle = 0;
  bool has_vblank = false;

  HostInterface_t* host = nullptr;
  int slot = 0;

  SpeakerPeripheral_t() = default;
};

static auto get_cycles(HostInterface_t* host) -> uint64_t {
  if (host != nullptr && host->GetCycles != nullptr) {
    return host->GetCycles();
  }
  return 0;
}

// --- Internal Implementation ---

auto speaker_initialize(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* speaker_peripheral = static_cast<SpeakerPeripheral_t*>(instance);

  auto* host = speaker_peripheral->host;
  const int slot = speaker_peripheral->slot;

  speaker_peripheral->event_count = 0;
  speaker_peripheral->current_state = false;
  speaker_peripheral->quiet_cycle_count = 0;
  speaker_peripheral->is_active = false;
  speaker_peripheral->has_strobe = false;
  speaker_peripheral->sound_mode = sound_wave;
  speaker_peripheral->last_sample_state = false;
  speaker_peripheral->filter_state = 0.0f;
  speaker_peripheral->previous_input = 0.0f;
  speaker_peripheral->host = host;
  speaker_peripheral->slot = slot;
  speaker_peripheral->last_update_cycle = get_cycles(host);
  speaker_peripheral->next_sample_cycle =
      static_cast<double>(speaker_peripheral->last_update_cycle);
  speaker_peripheral->current_clock_hz = ntsc_clock_hz;
  speaker_peripheral->last_vblank_cycle = 0;
  speaker_peripheral->has_vblank = false;
}

auto speaker_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  speaker_initialize(instance);
}

auto speaker_update(void* instance, uint32_t elapsed_cycles) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* speaker_peripheral = static_cast<SpeakerPeripheral_t*>(instance);

  if (speaker_peripheral->has_strobe) {
    speaker_peripheral->is_active = true;
    speaker_peripheral->quiet_cycle_count = 0;
    speaker_peripheral->has_strobe = false;
  } else if (speaker_peripheral->is_active) {
    speaker_peripheral->quiet_cycle_count += elapsed_cycles;

    const uint64_t inactivity_threshold =
        static_cast<uint64_t>(speaker_peripheral->current_clock_hz / 5.0);
    if (speaker_peripheral->quiet_cycle_count > inactivity_threshold) {
      speaker_peripheral->is_active = false;
    }
  }
  speaker_peripheral->last_update_cycle = get_cycles(speaker_peripheral->host);
}

auto speaker_is_active(void* instance) -> bool {
  if (instance == nullptr) {
    return false;
  }
  auto* speaker_peripheral = static_cast<SpeakerPeripheral_t*>(instance);
  return speaker_peripheral->is_active;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
// Justification: ABI signature required by Core.
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
  auto* speaker_peripheral = static_cast<SpeakerPeripheral_t*>(instance);

  speaker_peripheral->has_strobe = true;
  speaker_peripheral->is_active = true;
  speaker_peripheral->current_state = !speaker_peripheral->current_state;

  if (speaker_peripheral->sound_mode == static_cast<uint32_t>(sound_wave) &&
      static_cast<size_t>(speaker_peripheral->event_count) <
          max_speaker_events) {
    const auto event_index =
        static_cast<size_t>(speaker_peripheral->event_count);
    speaker_peripheral->events.at(event_index).cycle =
        get_cycles(speaker_peripheral->host);
    speaker_peripheral->events.at(event_index).state =
        speaker_peripheral->current_state;
    speaker_peripheral->event_count++;
  }

  return mem_read_floating_bus(remaining_cycles);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// --- ABI Implementation ---

constexpr uint16_t addr_speaker = 0xC030;

auto speaker_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }
  auto speaker_peripheral =
      std::unique_ptr<SpeakerPeripheral_t>(new SpeakerPeripheral_t());
  speaker_peripheral->host = host;
  speaker_peripheral->slot = slot;
  speaker_initialize(speaker_peripheral.get());

  if (host->RegisterDirectIO != nullptr) {
    host->RegisterDirectIO(speaker_peripheral.get(), addr_speaker,
                           speaker_toggle, speaker_toggle);
  }

  return speaker_peripheral.release();
}

auto speaker_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  std::unique_ptr<SpeakerPeripheral_t> speaker_peripheral(
      static_cast<SpeakerPeripheral_t*>(instance));
}

auto speaker_generate_samples(void* instance, uint32_t elapsed_cycles) -> void {
  if (elapsed_cycles == 0 || instance == nullptr) {
    return;
  }
  auto* speaker_peripheral = static_cast<SpeakerPeripheral_t*>(instance);

  const double cycles_per_sample = speaker_peripheral->current_clock_hz /
                                   static_cast<double>(default_sample_rate);
  if (cycles_per_sample <= 0.0) {
    return;
  }

  const uint64_t end_cycle = get_cycles(speaker_peripheral->host);
  const uint64_t start_cycle =
      (end_cycle >= elapsed_cycles) ? (end_cycle - elapsed_cycles) : 0;

  if (speaker_peripheral->next_sample_cycle <
          static_cast<double>(start_cycle) ||
      speaker_peripheral->next_sample_cycle >
          static_cast<double>(end_cycle) + (2.0 * cycles_per_sample)) {
    speaker_peripheral->next_sample_cycle = static_cast<double>(start_cycle);
  }

  size_t sample_count = 0;

  if (!speaker_peripheral->is_active) {
    while (speaker_peripheral->next_sample_cycle <=
               static_cast<double>(end_cycle) &&
           sample_count < (speaker_buffer_size - 1)) {
      if (std::abs(speaker_peripheral->filter_state) > filter_epsilon) {
        speaker_peripheral->filter_state *= decay_coefficient;
        const float raw_sample =
            speaker_peripheral->filter_state * speaker_sample_volume;
        const float clamped_sample =
            std::max(-32768.0f, std::min(32767.0f, raw_sample));
        const auto val = static_cast<int16_t>(clamped_sample);
        speaker_peripheral->sample_buffer.at(sample_count++) = val;
      } else {
        speaker_peripheral->filter_state = 0.0f;
        speaker_peripheral->sample_buffer.at(sample_count++) = 0;
      }
      speaker_peripheral->next_sample_cycle += cycles_per_sample;
    }
    speaker_peripheral->event_count = 0;
  } else {
    const uint32_t available_events = speaker_peripheral->event_count;
    uint32_t event_index = 0;

    while (speaker_peripheral->next_sample_cycle <=
               static_cast<double>(end_cycle) &&
           sample_count < (speaker_buffer_size - 1)) {
      const double sample_start = speaker_peripheral->next_sample_cycle;
      const double sample_end =
          speaker_peripheral->next_sample_cycle + cycles_per_sample;

      double sum = 0.0;
      double current_time = sample_start;

      while (event_index < available_events &&
             static_cast<double>(
                 speaker_peripheral->events.at(event_index).cycle) <
                 sample_end) {
        const auto& event = speaker_peripheral->events.at(event_index);
        const auto event_time = static_cast<double>(event.cycle);

        if (event_time <= sample_start) {
          speaker_peripheral->last_sample_state = event.state;
        } else {
          sum += (event_time - current_time) *
                 (speaker_peripheral->last_sample_state ? 1.0 : -1.0);
          speaker_peripheral->last_sample_state = event.state;
          current_time = event_time;
        }
        event_index++;
      }

      sum += (sample_end - current_time) *
             (speaker_peripheral->last_sample_state ? 1.0 : -1.0);

      const auto average = static_cast<float>(sum / cycles_per_sample);

      speaker_peripheral->filter_state =
          (average - speaker_peripheral->previous_input) +
          (dc_blocker_coefficient * speaker_peripheral->filter_state);
      speaker_peripheral->previous_input = average;

      const float raw_sample =
          speaker_peripheral->filter_state * speaker_sample_volume;
      const float clamped_sample =
          std::max(-32768.0f, std::min(32767.0f, raw_sample));
      const auto val = static_cast<int16_t>(clamped_sample);

      speaker_peripheral->sample_buffer.at(sample_count++) = val;
      speaker_peripheral->next_sample_cycle += cycles_per_sample;
    }
    speaker_peripheral->event_count = 0;
  }

  if (sample_count > 0) {
    auto* host = static_cast<HostInterface_t*>(speaker_peripheral->host);
    if (host != nullptr && host->AudioPushChannels != nullptr) {
      const int16_t* channel_ptrs[1] = {
          speaker_peripheral->sample_buffer.data()};
      host->AudioPushChannels(instance, channel_ptrs, 1, sample_count);
    }
  }
}

auto speaker_think(void* instance, uint32_t elapsed_cycles) -> void {
  speaker_update(instance, elapsed_cycles);
  speaker_generate_samples(instance, elapsed_cycles);
}

static auto speaker_abi_on_vblank(void* instance, bool vblank) -> void {
  if (instance == nullptr || !vblank) {
    return;
  }
  auto* speaker = static_cast<SpeakerPeripheral_t*>(instance);
  const uint64_t current_cycle = get_cycles(speaker->host);
  if (speaker->has_vblank && current_cycle > speaker->last_vblank_cycle) {
    const uint64_t delta = current_cycle - speaker->last_vblank_cycle;
    if (delta >= (pal_frame_cycles - vbl_tolerance) &&
        delta <= (pal_frame_cycles + vbl_tolerance)) {
      speaker->current_clock_hz = pal_clock_hz;
    } else if (delta >= (ntsc_frame_cycles - vbl_tolerance) &&
               delta <= (ntsc_frame_cycles + vbl_tolerance)) {
      speaker->current_clock_hz = ntsc_clock_hz;
    }
  }
  speaker->last_vblank_cycle = current_cycle;
  speaker->has_vblank = true;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
// Justification: Peripheral ABI signature.
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

  auto* speaker_peripheral = static_cast<SpeakerPeripheral_t*>(instance);
  auto* save_state_ptr = static_cast<SsIoSpeaker_t*>(state_buffer);
  save_state_ptr->g_spkr_last_cycle = speaker_peripheral->last_update_cycle;
  save_state_ptr->quiet_cycle_count = speaker_peripheral->quiet_cycle_count;
  save_state_ptr->recently_active = speaker_peripheral->is_active ? 1 : 0;
  save_state_ptr->state = speaker_peripheral->current_state ? 1 : 0;
  save_state_ptr->next_sample_cycle = speaker_peripheral->next_sample_cycle;
  save_state_ptr->last_sample_state =
      speaker_peripheral->last_sample_state ? 1 : 0;
  save_state_ptr->filter_state = speaker_peripheral->filter_state;

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
  auto* speaker_peripheral = static_cast<SpeakerPeripheral_t*>(instance);
  const auto* save_state_ptr = static_cast<const SsIoSpeaker_t*>(state_buffer);
  speaker_peripheral->last_update_cycle = save_state_ptr->g_spkr_last_cycle;
  speaker_peripheral->quiet_cycle_count = save_state_ptr->quiet_cycle_count;
  speaker_peripheral->is_active = (save_state_ptr->recently_active != 0);
  speaker_peripheral->current_state = (save_state_ptr->state != 0);
  speaker_peripheral->next_sample_cycle = save_state_ptr->next_sample_cycle;
  speaker_peripheral->last_sample_state =
      (save_state_ptr->last_sample_state != 0);
  speaker_peripheral->filter_state = save_state_ptr->filter_state;

  if (!std::isfinite(speaker_peripheral->filter_state)) {
    speaker_peripheral->filter_state = 0.0f;
  }
  if (!std::isfinite(speaker_peripheral->next_sample_cycle) ||
      speaker_peripheral->next_sample_cycle < 0.0) {
    speaker_peripheral->next_sample_cycle =
        static_cast<double>(speaker_peripheral->last_update_cycle);
  }

  speaker_peripheral->previous_input =
      speaker_peripheral->last_sample_state ? 1.0f : -1.0f;
  speaker_peripheral->event_count = 0;

  return peripheral_ok;
}

auto speaker_query(void* instance, uint32_t cmd_id, void* out, size_t* out_size)
    -> PeripheralStatus_t {
  if (out_size == nullptr) {
    return peripheral_error;
  }

  switch (cmd_id) {
    case speaker_query_is_active: {
      const size_t required_size = sizeof(uint8_t);
      if (out == nullptr) {
        *out_size = required_size;
        return peripheral_ok;
      }
      if (instance == nullptr || *out_size < required_size) {
        return peripheral_error;
      }
      *static_cast<uint8_t*>(out) = speaker_is_active(instance) ? 1 : 0;
      *out_size = required_size;
      return peripheral_ok;
    }
    case PERIPHERAL_QUERY_AUDIO_INFO: {
      const size_t required_size = sizeof(PeripheralAudioInfo_t);
      if (out == nullptr) {
        *out_size = required_size;
        return peripheral_ok;
      }
      if (*out_size < required_size) {
        return peripheral_error;
      }
      auto* info = static_cast<PeripheralAudioInfo_t*>(out);
      info->num_channels = 1;
      std::strncpy(info->channels[0].name, "Speaker",
                   sizeof(info->channels[0].name) - 1);
      info->channels[0].name[sizeof(info->channels[0].name) - 1] = '\0';
      info->channels[0].default_pan_left = 1.0f;
      info->channels[0].default_pan_right = 1.0f;
      *out_size = required_size;
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}
// NOLINTEND(bugprone-easily-swappable-parameters)

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
    .on_vblank = speaker_abi_on_vblank,
    .save_state = speaker_save_state,
    .load_state = speaker_load_state,
    .command = nullptr,
    .query = speaker_query};

}  // namespace

auto speaker_get_descriptor() -> Peripheral_t* { return &g_speaker_peripheral; }

PERIPHERAL_REGISTER(g_speaker_peripheral)
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-owning-memory)
