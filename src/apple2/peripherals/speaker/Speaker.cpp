// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//             cppcoreguidelines-owning-memory)
#include "apple2/peripherals/speaker/Speaker.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "core/AudioMixer.h"
#include "core/Peripheral.h"

namespace {

constexpr int inactivity_divisor = 5;
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

  HostInterface_t* host = nullptr;
  int slot = 0;

  SpeakerPeripheral_t() = default;
};

static auto get_cycles(HostInterface_t* host) -> uint64_t {
  if (host != nullptr && host->GetCycles != nullptr) {
    return host->GetCycles();
  }
  return cpu_get_cumulative_cycles();
}

// --- Internal Implementation ---

auto speaker_initialize(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* speaker_peripheral = static_cast<SpeakerPeripheral_t*>(instance);

  auto* host = speaker_peripheral->host;
  const int slot = speaker_peripheral->slot;

  *speaker_peripheral = SpeakerPeripheral_t();

  speaker_peripheral->host = host;
  speaker_peripheral->slot = slot;
  speaker_peripheral->last_update_cycle = get_cycles(host);
  speaker_peripheral->next_sample_cycle = static_cast<double>(get_cycles(host));
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
        static_cast<uint64_t>(g_current_clk_6502 / inactivity_divisor);

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

  cpu_calc_cycles(remaining_cycles);
  speaker_peripheral->has_strobe = true;

  if (!g_full_speed) {
    speaker_peripheral->is_active = true;

    if (speaker_peripheral->sound_mode == static_cast<uint32_t>(sound_wave) &&
        static_cast<size_t>(speaker_peripheral->event_count) <
            max_speaker_events) {
      const auto event_index =
          static_cast<size_t>(speaker_peripheral->event_count);
      speaker_peripheral->current_state = !speaker_peripheral->current_state;
      speaker_peripheral->events.at(event_index).cycle =
          get_cycles(speaker_peripheral->host);
      speaker_peripheral->events.at(event_index).state =
          speaker_peripheral->current_state;
      speaker_peripheral->event_count++;
    }
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

auto speaker_think(void* instance, uint32_t elapsed_cycles) -> void {
  speaker_update(instance, elapsed_cycles);
  speaker_generate_samples(instance, elapsed_cycles);
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
      buffer_size < required_size) {
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

  return peripheral_ok;
}

auto speaker_query(void* instance, uint32_t query_id, void* output_buffer,
                   size_t* buffer_size) -> PeripheralStatus_t {
  if (instance == nullptr || buffer_size == nullptr) {
    return peripheral_error;
  }

  switch (query_id) {
    case speaker_query_is_active: {
      const size_t required_size = sizeof(bool);

      if (output_buffer == nullptr) {
        *buffer_size = required_size;
        return peripheral_ok;
      }

      if (*buffer_size < required_size) {
        return peripheral_error;
      }

      *static_cast<bool*>(output_buffer) = speaker_is_active(instance);
      *buffer_size = required_size;
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
    .on_vblank = nullptr,
    .save_state = speaker_save_state,
    .load_state = speaker_load_state,
    .command = nullptr,
    .query = speaker_query};

}  // namespace

// --- Public Synthesis API ---

auto speaker_generate_samples(void* instance, uint32_t elapsed_cycles) -> void {
  if (elapsed_cycles == 0 || instance == nullptr) {
    return;
  }
  auto* speaker_peripheral = static_cast<SpeakerPeripheral_t*>(instance);

  const double cycles_per_sample = g_current_clk_6502 / SPKR_SAMPLE_RATE;
  if (cycles_per_sample <= 0.0) {
    return;
  }

  const uint64_t start_cycle =
      get_cycles(speaker_peripheral->host) - elapsed_cycles;
  const uint64_t end_cycle = get_cycles(speaker_peripheral->host);

  if (speaker_peripheral->next_sample_cycle <
      static_cast<double>(start_cycle)) {
    speaker_peripheral->next_sample_cycle = static_cast<double>(start_cycle);
  }

  size_t sample_count = 0;

  if (!speaker_peripheral->is_active) {
    while (speaker_peripheral->next_sample_cycle <=
               static_cast<double>(end_cycle) &&
           sample_count < (speaker_buffer_size - 2)) {
      speaker_peripheral->sample_buffer.at(sample_count++) = 0;
      speaker_peripheral->sample_buffer.at(sample_count++) = 0;
      speaker_peripheral->next_sample_cycle += cycles_per_sample;
    }
    speaker_peripheral->event_count = 0;
  } else {
    const uint32_t available_events = speaker_peripheral->event_count;
    uint32_t event_index = 0;

    while (speaker_peripheral->next_sample_cycle <=
               static_cast<double>(end_cycle) &&
           sample_count < (speaker_buffer_size - 2)) {
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

      const auto val = static_cast<int16_t>(speaker_peripheral->filter_state *
                                            speaker_sample_volume);

      speaker_peripheral->sample_buffer.at(sample_count++) = val;
      speaker_peripheral->sample_buffer.at(sample_count++) = val;
      speaker_peripheral->next_sample_cycle += cycles_per_sample;
    }
    speaker_peripheral->event_count = 0;
  }

  if (!speaker_peripheral->is_active &&
      speaker_peripheral->filter_state != 0.0f) {
    while (sample_count < (speaker_buffer_size - 2) &&
           std::abs(speaker_peripheral->filter_state) > filter_epsilon) {
      speaker_peripheral->filter_state *= decay_coefficient;
      const auto val = static_cast<int16_t>(speaker_peripheral->filter_state *
                                            speaker_sample_volume);
      speaker_peripheral->sample_buffer.at(sample_count++) = val;
      speaker_peripheral->sample_buffer.at(sample_count++) = val;
    }
    if (std::abs(speaker_peripheral->filter_state) <= filter_epsilon) {
      speaker_peripheral->filter_state = 0.0f;
    }
  }

  if (sample_count > 0) {
    auto* host = static_cast<HostInterface_t*>(speaker_peripheral->host);
    if (host != nullptr && host->AudioPushSamples != nullptr) {
      host->AudioPushSamples(instance, speaker_peripheral->sample_buffer.data(),
                             sample_count);
    }
  }
}

auto speaker_get_events(void* instance, SpeakerEvent_t* event_buffer,
                        uint32_t buffer_capacity) -> uint32_t {
  if (event_buffer == nullptr || instance == nullptr) {
    return 0;
  }
  auto* speaker_peripheral = static_cast<SpeakerPeripheral_t*>(instance);
  const auto count = std::min(speaker_peripheral->event_count, buffer_capacity);
  if (count > 0) {
    std::copy_n(speaker_peripheral->events.begin(), count, event_buffer);
    speaker_peripheral->event_count = 0;
  }
  return count;
}

auto speaker_get_last_cycle(void* instance) -> uint64_t {
  if (instance == nullptr) {
    return 0;
  }
  auto* speaker_peripheral = static_cast<SpeakerPeripheral_t*>(instance);
  return speaker_peripheral->last_update_cycle;
}

auto speaker_get_descriptor() -> Peripheral_t* { return &g_speaker_peripheral; }

PERIPHERAL_REGISTER(g_speaker_peripheral)
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//           cppcoreguidelines-owning-memory)
