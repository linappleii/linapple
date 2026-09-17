// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/speaker/Speaker.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/Peripheral_Types.h"

namespace {

// Hardware MMIO Mapping
constexpr uint16_t speaker_io_address = 0xC030;

// Per-Update Slice Capacity Limits
constexpr size_t speaker_max_events_per_update = 8192;
// One sample per 6502 cycle means a frame of samples is a frame of cycles:
// 17030 at NTSC, 20313 at PAL. Sized for the longer frame with headroom.
// Anything below the PAL frame truncates samples every frame and breaks
// think(n) yielding n samples.
constexpr size_t speaker_max_samples_per_update = 24000;

// The speaker's finest possible state change is one 6502 cycle, so it
// synthesizes at the cycle rate and leaves resampling to the mixer, which is
// the only party that knows the device's rate.
constexpr double cycles_per_sample = 1.0;

// DSP & Analog Cone Modeling Parameters
// The tuning choice is the time constant, not the per-sample coefficient.
// 23000 cycles is 22.5 ms at the NTSC clock, which preserves the feel of the
// old 0.999-at-44.1-kHz value and a high-pass corner near 7 Hz. At six nines a
// float coefficient carries only about three significant digits of the decay
// rate, so the coefficient and the filter state are both double.
constexpr double dc_blocker_tau_cycles = 23000.0;
constexpr double dc_blocker_coefficient = 1.0 - (1.0 / dc_blocker_tau_cycles);
// The DC blocker's decay never reaches exactly zero, so without a cutoff the
// cone emits a trickle forever. It is also what keeps filter_state out of the
// denormal range.
constexpr double spindown_silence_epsilon = 0.001;

struct SpeakerEvent_t {
  uint64_t cycle = 0;
  bool state = false;
};

struct SpeakerPeripheral_t {
  // --- Seam 3 Host Interface ---
  HostInterface_t* host = nullptr;

  // --- Hardware Latch ---
  bool current_state = false;
  bool last_sample_state = false;

  // --- DSP Filter & Phase Accumulator ---
  double next_sample_cycle = 0.0;
  double filter_state = 0.0;
  double previous_input = 0.0;

  // --- Per-Update Synthesis Buffers & Queues ---
  uint32_t event_count = 0;
  std::array<float, speaker_max_samples_per_update> sample_buffer{};
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

static auto synthesize_samples(SpeakerPeripheral_t& speaker, uint64_t end_cycle)
    -> size_t {
  const uint32_t available_events = speaker.event_count;
  uint32_t event_index = 0;
  size_t sample_count = 0;

  // A sample covers the window that starts at its own cycle, so it can only be
  // emitted once that window has closed inside the slice the host handed over.
  while (speaker.next_sample_cycle + cycles_per_sample <=
             static_cast<double>(end_cycle) &&
         sample_count < speaker_max_samples_per_update) {
    const double sample_start = speaker.next_sample_cycle;

    // One sample per cycle puts every strobe on a window boundary, so the
    // window average is simply the level the last edge in it left behind.
    while (event_index < available_events &&
           static_cast<double>(speaker.events[event_index].cycle) <=
               sample_start) {
      speaker.last_sample_state = speaker.events[event_index].state;
      event_index++;
    }

    const double drive_level = speaker.last_sample_state ? 1.0 : -1.0;

    speaker.filter_state = (drive_level - speaker.previous_input) +
                           (dc_blocker_coefficient * speaker.filter_state);
    speaker.previous_input = drive_level;

    if (std::abs(speaker.filter_state) < spindown_silence_epsilon) {
      speaker.filter_state = 0.0;
    }

    // Values near +/-2.0 at an edge are correct and expected: nothing in the
    // peripheral limits them, because nothing downstream is an integer until
    // the mixer's single conversion.
    speaker.sample_buffer[sample_count++] =
        static_cast<float>(speaker.filter_state);
    speaker.next_sample_cycle += cycles_per_sample;
  }
  return sample_count;
}

static auto generate_samples(SpeakerPeripheral_t& speaker, void* instance,
                             uint32_t elapsed_cycles) -> void {
  if (elapsed_cycles == 0) {
    return;
  }

  // A cone at rest with nothing driving it is silent, and silence is no
  // samples rather than a stream of zeros. This is what the inactivity
  // watchdog was reaching for, stated in terms of the model instead of a
  // timer.
  if (speaker.event_count == 0 &&
      std::abs(speaker.filter_state) < spindown_silence_epsilon) {
    speaker.filter_state = 0.0;
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

  const size_t sample_count = synthesize_samples(speaker, end_cycle);

  // An edge that did not fit in this slice still has to land: a strobe at
  // exactly end_cycle belongs to the next slice's first sample, and the sample
  // cap can cut a slice short. Carrying the last state forward keeps the
  // flip-flop and the synthesizer from ending a slice disagreeing.
  if (speaker.event_count > 0) {
    speaker.last_sample_state = speaker.events[speaker.event_count - 1].state;
  }
  speaker.event_count = 0;

  if (sample_count > 0 && speaker.host != nullptr &&
      speaker.host->AudioPushChannels != nullptr) {
    const float* channel_ptrs[1] = {speaker.sample_buffer.data()};
    speaker.host->AudioPushChannels(instance, channel_ptrs, 1, sample_count);
  }
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
  info.time_base = peripheral_audio_cpu_clocked;
  info.cycle_divisor = 1;
  info.num_channels = 1;
  // A square-wave edge through the DC blocker is a step of exactly 2.0
  info.peak_magnitude = 2.0f;
  std::strncpy(info.channels[0].name, "Speaker",
               sizeof(info.channels[0].name) - 1);
  info.channels[0].default_pan_left = 1.0f;
  info.channels[0].default_pan_right = 1.0f;
  *out_size = required_size;
  return peripheral_ok;
}

// --- Hardware Strobe Handler ---

static auto speaker_strobe(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto& speaker = *static_cast<SpeakerPeripheral_t*>(instance);

  speaker.current_state = !speaker.current_state;

  // A full queue overwrites its own tail rather than dropping the edge, so the
  // level the synthesizer drives always ends the slice equal to the flip-flop.
  // Losing intermediate edges inside one slice costs resolution; losing the
  // final polarity would leave the cone where no Apple II would leave it.
  auto& event = (speaker.event_count < speaker_max_events_per_update)
                    ? speaker.events[speaker.event_count++]
                    : speaker.events[speaker_max_events_per_update - 1];
  event.cycle = get_cycles(speaker.host);
  event.state = speaker.current_state;
}

// --- ABI Implementation ---

auto speaker_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto& speaker = *static_cast<SpeakerPeripheral_t*>(instance);

  speaker.event_count = 0;
  speaker.current_state = false;
  speaker.last_sample_state = false;
  speaker.filter_state = 0.0;
  // previous_input is the drive level, not zero: the blocker sees no step
  // until something strobes, so a reset with no strobe emits nothing and every
  // edge afterwards is a step of exactly two.
  speaker.previous_input = speaker.last_sample_state ? 1.0 : -1.0;
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

  if (host->RegisterDirectIOStrobe != nullptr) {
    host->RegisterDirectIOStrobe(speaker.get(), speaker_io_address,
                                 speaker_strobe);
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
  speaker.last_update_cycle = get_cycles(speaker.host);
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
  // quiet_cycle_count and recently_active are .aws format fields whose backing
  // state is gone with the inactivity watchdog. They are written as constants
  // and ignored on load so that the snapshot layout keeps its promise.
  ss.quiet_cycle_count = 0;
  ss.recently_active = 0;
  ss.state = speaker.current_state ? 1 : 0;
  ss.next_sample_cycle = speaker.next_sample_cycle;
  ss.last_sample_state = speaker.last_sample_state ? 1 : 0;
  ss.filter_state = static_cast<float>(speaker.filter_state);

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
  speaker.current_state = (ss.state != 0);
  speaker.next_sample_cycle = ss.next_sample_cycle;
  speaker.last_sample_state = (ss.last_sample_state != 0);
  speaker.filter_state = ss.filter_state;

  if (!std::isfinite(speaker.filter_state)) {
    speaker.filter_state = 0.0;
  }
  if (!std::isfinite(speaker.next_sample_cycle) ||
      speaker.next_sample_cycle < 0.0) {
    speaker.next_sample_cycle = static_cast<double>(speaker.last_update_cycle);
  }

  speaker.previous_input = speaker.last_sample_state ? 1.0 : -1.0;
  speaker.event_count = 0;

  return peripheral_ok;
}

auto speaker_query(void* instance, uint32_t cmd_id, void* out, size_t* out_size)
    -> PeripheralStatus_t {
  (void)instance;
  if (out_size == nullptr) {
    return peripheral_error;
  }

  if (cmd_id == PERIPHERAL_QUERY_AUDIO_INFO) {
    return query_audio_info(out, out_size);
  }

  return peripheral_incompatible;
}

static const Peripheral_t g_speaker_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.speaker",
    .name = "Speaker",
    .description = "Built-in Apple II speaker",
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

// peripheral_register and ActivePeripheral_t::api still take a mutable
// Peripheral_t*, so the immutable descriptor is cast the same way
// PERIPHERAL_REGISTER casts it.
auto speaker_get_descriptor() -> Peripheral_t* {
  return const_cast<Peripheral_t*>(&g_speaker_peripheral);
}

PERIPHERAL_REGISTER(g_speaker_peripheral)
