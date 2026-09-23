// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/clock/ClockCard.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/clock/ClockCardCommands.h"

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;

namespace {

// LinApple's own firmware, written for the 2008 SourceForge patch #1 (GPL);
// it is not an image of any real card's ROM. It follows the clock-card
// protocol of the ProDOS 8 Technical Reference Manual, section 6.1
// "Clock/Calendar Routines", which is how the kernel's built-in ThunderClock
// driver finds and drives it without a driver on the disk:
//   - the ID bytes $Cn00/$Cn02/$Cn04/$Cn06 must read $08/$28/$58/$70;
//   - the READ entry at $Cn08 leaves "mo,da,dt,hr,mn" in the GETLN input
//     buffer at $0200 as high-ASCII digits and commas (month, weekday with
//     Sunday = 0, day, hour, minute; the year comes from a table inside the
//     kernel). The driver parses digits and commas, so this firmware ends
//     the five fields with $80 where a real ThunderClock sends six (with
//     seconds) ending in a carriage return;
//   - the WRITE entry at $Cn0B, which the driver calls with A = $A3 ("#") to
//     select the numeric format on a real card, is a bare RTS here.
// The read routine learns its own slot with JSR $FF58 (a known RTS in the
// monitor) and the return address it leaves on the stack, then reads $C0nF,
// the strobe that latches the host's time into the ten BCD digits at
// $C0n0..$C0n9: month, an always-zero pair, weekday, day, hour, minute.
// The $B0 $CC at $Cn5D is the carry leg of the branch at $Cn03 and stays.
const std::array<uint8_t, 256> clock_rom = {{
    0x08, 0x90, 0x28, 0xb0, 0x58, 0x00, 0x70, 0x00, 0xea, 0xea, 0xa9, 0x60,
    0x08, 0x78, 0x20, 0x58, 0xff, 0xba, 0xbd, 0x00, 0x01, 0x28, 0x0a, 0x0a,
    0x0a, 0x0a, 0xa8, 0xb9, 0x8f, 0xc0, 0xa2, 0x00, 0xf0, 0x0b, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x60, 0xb9, 0x80, 0xc0,
    0xc8, 0x09, 0xb0, 0x9d, 0x00, 0x02, 0xe8, 0xb9, 0x80, 0xc0, 0xc8, 0x09,
    0xb0, 0x9d, 0x00, 0x02, 0xe8, 0xa9, 0xac, 0x9d, 0x00, 0x02, 0xe8, 0x98,
    0x29, 0x0f, 0xc9, 0x0a, 0x90, 0xdf, 0xa9, 0x80, 0x9d, 0xff, 0x01, 0x60,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb0, 0xcc,
}};

constexpr size_t latch_count = 10;
constexpr size_t latch_month = 0;
constexpr size_t latch_weekday = 2;
constexpr size_t latch_day = 4;
constexpr size_t latch_hour = 6;
constexpr size_t latch_minute = 8;

constexpr uint16_t io_register_mask = 0x0F;
constexpr uint8_t strobe_register = 0x0F;

constexpr int radix = 10;
constexpr uint8_t bcd_digit_max = 9;
constexpr int month_max = 12;
constexpr int weekday_max = 6;
constexpr int day_max = 31;
constexpr int hour_max = 23;
constexpr int minute_max = 59;

struct ClockCard_t {
  std::array<uint8_t, latch_count> latches{};
  HostInterface_t* host = nullptr;
  int slot = 0;
  bool use_fixed_epoch = false;
  uint64_t fixed_epoch = 0;
};

auto set_latch_pair(ClockCard_t* card, size_t index, int value) -> void {
  constexpr size_t index_mask = 0x0E;
  constexpr int value_max = 100;

  const size_t base_index = index & index_mask;
  if (base_index + 1 >= latch_count) {
    return;
  }

  const int clamped_value = std::abs(value) % value_max;
  const auto digits = std::div(clamped_value, radix);

  card->latches.at(base_index) = static_cast<uint8_t>(digits.quot);
  card->latches.at(base_index + 1) = static_cast<uint8_t>(digits.rem);
}

auto update_latches(ClockCard_t* card) -> void {
  time_t now = 0;
  if (card->use_fixed_epoch) {
    now = static_cast<time_t>(card->fixed_epoch);
  } else if (time(&now) == static_cast<time_t>(-1)) {
    return;
  }

  struct tm local_time{};
  if (localtime_r(&now, &local_time) == nullptr) {
    return;
  }

  set_latch_pair(card, latch_month, local_time.tm_mon + 1);
  set_latch_pair(card, latch_weekday, local_time.tm_wday);
  set_latch_pair(card, latch_day, local_time.tm_mday);
  set_latch_pair(card, latch_hour, local_time.tm_hour);
  set_latch_pair(card, latch_minute, local_time.tm_min);
}

auto pair_value(const uint8_t* latches, size_t index) -> int {
  return (latches[index] * radix) + latches[index + 1];
}

auto clockcard_io_read(void* instance, uint16_t program_counter,
                       uint16_t memory_address, uint8_t is_write,
                       uint8_t data_value, uint32_t remaining_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)is_write;
  (void)data_value;
  if (instance == nullptr) {
    return mem_read_floating_bus(remaining_cycles);
  }
  auto* card = static_cast<ClockCard_t*>(instance);

  const size_t register_offset = memory_address & io_register_mask;
  if (register_offset < latch_count) {
    return card->latches.at(register_offset);
  }
  if (register_offset == strobe_register) {
    update_latches(card);
    return mem_read_floating_bus(remaining_cycles);
  }

  return mem_read_floating_bus(remaining_cycles);
}

auto clockcard_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }

  auto card = std::unique_ptr<ClockCard_t>(new ClockCard_t());
  card->slot = slot;
  card->host = host;

  if (host->RegisterCxROM != nullptr) {
    host->RegisterCxROM(slot, clock_rom.data());
  }
  if (host->RegisterIO != nullptr) {
    host->RegisterIO(slot, clockcard_io_read, nullptr, nullptr, nullptr);
  }

  return card.release();
}

auto clockcard_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* card = static_cast<ClockCard_t*>(instance);
  card->latches.fill(0);
}

auto clockcard_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }

  std::unique_ptr<ClockCard_t> card(static_cast<ClockCard_t*>(instance));
}

auto clockcard_abi_command(void* instance, uint32_t cmd_id, const void* data,
                           size_t size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }

  auto* card = static_cast<ClockCard_t*>(instance);

  if (!peripheral_cmd_is_mine(cmd_id, PERIPHERAL_SUBSYSTEM_CLOCK)) {
    return peripheral_incompatible;  // another peripheral in the slot owns it
  }

  switch (cmd_id) {
    case clockcard_cmd_set_epoch: {
      if (data == nullptr) {
        return peripheral_error;
      }
      if (size == sizeof(ClockCardSetEpochPayload_t)) {
        const auto* payload =
            static_cast<const ClockCardSetEpochPayload_t*>(data);
        card->fixed_epoch = payload->epoch;
        card->use_fixed_epoch = true;
        return peripheral_ok;
      }
      if (size == sizeof(uint64_t)) {
        card->fixed_epoch = *static_cast<const uint64_t*>(data);
        card->use_fixed_epoch = true;
        return peripheral_ok;
      }
      if (size == sizeof(uint32_t)) {
        card->fixed_epoch = *static_cast<const uint32_t*>(data);
        card->use_fixed_epoch = true;
        return peripheral_ok;
      }
      return peripheral_error;
    }
    case clockcard_cmd_clear_epoch: {
      card->use_fixed_epoch = false;
      card->fixed_epoch = 0;
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

auto clockcard_abi_query(void* instance, uint32_t query_id, void* out,
                         size_t* size) -> PeripheralStatus_t {
  if (size == nullptr) {
    return peripheral_error;
  }

  if (!peripheral_cmd_is_mine(query_id, PERIPHERAL_SUBSYSTEM_CLOCK)) {
    return peripheral_incompatible;  // another peripheral in the slot owns it
  }

  switch (query_id) {
    case clockcard_query_epoch: {
      constexpr size_t required_size = sizeof(ClockCardEpochQuery_t);
      if (out == nullptr) {
        *size = required_size;
        return peripheral_ok;
      }
      if (instance == nullptr || *size < required_size) {
        return peripheral_error;
      }
      const auto* card = static_cast<const ClockCard_t*>(instance);
      auto* query = static_cast<ClockCardEpochQuery_t*>(out);
      query->epoch = card->fixed_epoch;
      query->is_fixed = card->use_fixed_epoch ? 1 : 0;
      *size = required_size;
      return peripheral_ok;
    }
    case clockcard_query_time: {
      constexpr size_t required_size = sizeof(ClockCardTimeQuery_t);
      if (out == nullptr) {
        *size = required_size;
        return peripheral_ok;
      }
      if (instance == nullptr || *size < required_size) {
        return peripheral_error;
      }
      const auto* card = static_cast<const ClockCard_t*>(instance);
      const uint8_t* latches = card->latches.data();
      auto* query = static_cast<ClockCardTimeQuery_t*>(out);
      query->month = static_cast<uint8_t>(pair_value(latches, latch_month));
      query->day_of_week = latches[latch_weekday + 1];
      query->day = static_cast<uint8_t>(pair_value(latches, latch_day));
      query->hour = static_cast<uint8_t>(pair_value(latches, latch_hour));
      query->minute = static_cast<uint8_t>(pair_value(latches, latch_minute));
      *size = required_size;
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

auto clockcard_abi_save_state(void* instance, void* state_buffer,
                              size_t* buffer_size) -> PeripheralStatus_t {
  if (buffer_size == nullptr) {
    return peripheral_error;
  }

  constexpr size_t required_size = sizeof(ClockCardSaveState_t);
  if (state_buffer == nullptr) {
    *buffer_size = required_size;
    return peripheral_ok;
  }

  if (instance == nullptr || *buffer_size < required_size) {
    return peripheral_error;
  }

  const auto* card = static_cast<const ClockCard_t*>(instance);
  auto* state = static_cast<ClockCardSaveState_t*>(state_buffer);
  std::memset(state, 0, sizeof(ClockCardSaveState_t));
  state->version = CLOCKCARD_STATE_VERSION;
  state->struct_size = static_cast<uint32_t>(required_size);
  state->fixed_epoch = card->fixed_epoch;
  std::copy(card->latches.begin(), card->latches.end(), state->latches);
  state->use_fixed_epoch = card->use_fixed_epoch ? 1 : 0;

  *buffer_size = required_size;
  return peripheral_ok;
}

auto latches_are_a_calendar_time(const uint8_t* latches) -> bool {
  for (size_t i = 0; i < latch_count; ++i) {
    if (latches[i] > bcd_digit_max) {
      return false;
    }
  }
  return latches[latch_weekday] == 0 &&
         pair_value(latches, latch_month) <= month_max &&
         latches[latch_weekday + 1] <= weekday_max &&
         pair_value(latches, latch_day) <= day_max &&
         pair_value(latches, latch_hour) <= hour_max &&
         pair_value(latches, latch_minute) <= minute_max;
}

auto clockcard_abi_load_state(void* instance, const void* state_buffer,
                              size_t buffer_size) -> PeripheralStatus_t {
  if (instance == nullptr || state_buffer == nullptr ||
      buffer_size != sizeof(ClockCardSaveState_t)) {
    return peripheral_error;
  }

  const auto* state = static_cast<const ClockCardSaveState_t*>(state_buffer);
  if (state->version != CLOCKCARD_STATE_VERSION ||
      state->struct_size != sizeof(ClockCardSaveState_t)) {
    return peripheral_error;
  }
  if (!latches_are_a_calendar_time(state->latches)) {
    return peripheral_error;
  }

  auto* card = static_cast<ClockCard_t*>(instance);
  std::copy_n(state->latches, latch_count, card->latches.begin());
  card->use_fixed_epoch = (state->use_fixed_epoch != 0);
  card->fixed_epoch = state->fixed_epoch;

  return peripheral_ok;
}

}  // namespace

static const Peripheral_t g_clockcard_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.clock",
    .name = "Clock Card",
    .description = "ThunderClock-compatible ProDOS clock",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = -1,
    .init = clockcard_abi_init,
    .reset = clockcard_abi_reset,
    .shutdown = clockcard_abi_shutdown,
    .think = nullptr,
    .on_vblank = nullptr,
    .save_state = clockcard_abi_save_state,
    .load_state = clockcard_abi_load_state,
    .command = clockcard_abi_command,
    .query = clockcard_abi_query};

// peripheral_register and ActivePeripheral_t::api still take a mutable
// Peripheral_t*, so the immutable descriptor is cast the same way
// PERIPHERAL_REGISTER casts it.
auto clockcard_get_descriptor() -> Peripheral_t* {
  return const_cast<Peripheral_t*>(&g_clockcard_peripheral);
}

PERIPHERAL_REGISTER(g_clockcard_peripheral)
