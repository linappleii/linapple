// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/clock/Clock.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>

#include "apple2/peripherals/clock/ClockCommands.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;

namespace {

/*
I/O map: (please add an offset of Slot#*16 to the address below)

        C080	(r)   latched month (tens)
        C081	(r)   latched month (units)
        C082	(r)   always zero
        C083	(r)   latched day-of-week
        C084	(r)   latched day-of-month (tens)
        C085	(r)   latched day-of-month (units)
        C086	(r)   latched hour (tens)
        C087	(r)   latched hour (units)
        C088	(r)   latched minute (tens)
        C089	(r)   latched minute (units)
        C08F	(r)   get system clock and update the latched values
*/

/*
  Briefly speaking, ProDOS detects the clock card if it finds that
    $Cx00 => $08
    $Cx02 => $28
    $Cx04 => $58
    $Cx06 => $70

  When ProDOS needs to get the time, it calls $Cx0B with A=0xA3 ("#")
  (which this ROM ignores) and then calls $Cx08.
  Upon returning from the latter, it expects the line buffer (beginning
  at $0200) to contain an ASCII (with 8-th bit on) like:
    01,02,03,04,05
  terminated by a trailing $80.
  The five 2-digit numbers, separated by commas, are:
    month (1--12), day-of-week (0=Sun - 6=Sat), day-of-month,
    hour (24hr clock), minute

  You can easily test this interface by enterint the monitor (CALL -151)
  and then type (the first "*" is the prompt character):

  *                    Cx08G 200.20F

  where x is the slot number of this clock card.
  Please leave at least 16 blanks before the character "C" so that
  the command line buffer won't be trashed by the ROM routine before
  it is processed.
*/

static const std::array<uint8_t, 256> Clock_ROM = {{
    0x08, 0x90, 0x28, 0xb0, 0x58, 0x00, 0x70, 0x00, 0xea, 0xea, 0xa9, 0x60,
    0x08, 0x78, 0x20, 0x58, 0xff, 0xba, 0xbd, 0x00, 0x01, 0x28, 0x0a, 0x0a,
    0x0a, 0x0a, 0xa8, 0xb9, 0x8f, 0xc0, 0xa2, 0x00, 0xf0, 0x0b, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x60, 0xb9, 0x80, 0xc0,
    0xc8, 0x09, 0xb0, 0x9d, 0x00, 0x02, 0xe8, 0xb9, 0x80, 0xc0, 0xc8, 0x09,
    0xb0, 0x9d, 0x00, 0x02, 0xe8, 0xa9, 0xac, 0x9d, 0x00, 0x02, 0xe8, 0x98,
    0x29, 0x0f, 0xc9, 0x0a, 0x90, 0xdf, 0xa9, 0x80, 0x9d, 0xff, 0x01, 0x60,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb0, 0xcc,
}};

constexpr size_t CLOCK_LATCHES_COUNT = 10;
constexpr int LATCH_MONTH = 0;
constexpr int LATCH_WEEKDAY = 2;
constexpr int LATCH_DAY = 4;
constexpr int LATCH_HOUR = 6;
constexpr int LATCH_MINUTE = 8;

constexpr uint16_t IO_ADDR_MASK = 0x0F;
constexpr uint8_t LATCH_UPDATE_REG = 0x0F;

struct ClockPeripheral_t {
  std::array<uint8_t, CLOCK_LATCHES_COUNT> latches{};
  HostInterface_t* host = nullptr;
  int slot = 0;
  bool use_fixed_epoch = false;
  uint64_t fixed_epoch = 0;
};

static auto set_latch_pair(ClockPeripheral_t* clock_peripheral, size_t index,
                           int value) -> void {
  constexpr size_t index_mask = 0x0E;
  constexpr int radix = 10;
  constexpr int value_max = 100;

  const size_t base_index = index & index_mask;
  if (base_index + 1 >= CLOCK_LATCHES_COUNT) {
    return;
  }

  const int clamped_value = std::abs(value) % value_max;
  const auto digits = std::div(clamped_value, radix);

  clock_peripheral->latches.at(base_index) = static_cast<uint8_t>(digits.quot);
  clock_peripheral->latches.at(base_index + 1) =
      static_cast<uint8_t>(digits.rem);
}

static auto update_latches(ClockPeripheral_t* clock_peripheral) -> void {
  time_t now = 0;
  if (clock_peripheral->use_fixed_epoch) {
    now = static_cast<time_t>(clock_peripheral->fixed_epoch);
  } else if (time(&now) == static_cast<time_t>(-1)) {
    return;
  }

  struct tm local_time{};
  if (localtime_r(&now, &local_time) == nullptr) {
    return;
  }

  const int month = local_time.tm_mon + 1;
  const int weekday = local_time.tm_wday;
  const int day = local_time.tm_mday;
  const int hour = local_time.tm_hour;
  const int minute = local_time.tm_min;

  set_latch_pair(clock_peripheral, LATCH_MONTH, month);
  set_latch_pair(clock_peripheral, LATCH_WEEKDAY, weekday);
  set_latch_pair(clock_peripheral, LATCH_DAY, day);
  set_latch_pair(clock_peripheral, LATCH_HOUR, hour);
  set_latch_pair(clock_peripheral, LATCH_MINUTE, minute);
}

static auto clock_io_read(void* instance, uint16_t program_counter,
                          uint16_t memory_address, uint8_t is_write,
                          uint8_t data_value, uint32_t remaining_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)is_write;
  (void)data_value;
  if (instance == nullptr) {
    return mem_read_floating_bus(remaining_cycles);
  }
  auto* clock_peripheral = static_cast<ClockPeripheral_t*>(instance);

  const uint16_t register_offset = memory_address & IO_ADDR_MASK;
  if (register_offset < CLOCK_LATCHES_COUNT) {
    return clock_peripheral->latches.at(register_offset);
  }
  if (register_offset == LATCH_UPDATE_REG) {
    update_latches(clock_peripheral);
    return mem_read_floating_bus(remaining_cycles);
  }

  return mem_read_floating_bus(remaining_cycles);
}

static auto clock_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }

  auto clock_peripheral =
      std::unique_ptr<ClockPeripheral_t>(new ClockPeripheral_t());
  clock_peripheral->slot = slot;
  clock_peripheral->host = host;

  if (host->RegisterCxROM != nullptr) {
    host->RegisterCxROM(slot, const_cast<uint8_t*>(Clock_ROM.data()));
  }
  if (host->RegisterIO != nullptr) {
    host->RegisterIO(slot, clock_io_read, nullptr, nullptr, nullptr);
  }

  return clock_peripheral.release();
}

static auto clock_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* clock_peripheral = static_cast<ClockPeripheral_t*>(instance);
  clock_peripheral->latches.fill(0);
}

static auto clock_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }

  std::unique_ptr<ClockPeripheral_t> clock_peripheral(
      static_cast<ClockPeripheral_t*>(instance));
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
// Justification: ABI-required function signature.
static auto clock_abi_command(void* instance, uint32_t cmd_id, const void* data,
                              size_t size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }

  auto* clock_peripheral = static_cast<ClockPeripheral_t*>(instance);

  switch (cmd_id) {
    case clock_cmd_set_epoch: {
      if (data == nullptr) {
        return peripheral_error;
      }
      if (size == sizeof(ClockSetEpochPayload_t)) {
        const auto* payload = static_cast<const ClockSetEpochPayload_t*>(data);
        clock_peripheral->fixed_epoch = payload->epoch;
        clock_peripheral->use_fixed_epoch = true;
        return peripheral_ok;
      }
      if (size == sizeof(uint64_t)) {
        clock_peripheral->fixed_epoch = *static_cast<const uint64_t*>(data);
        clock_peripheral->use_fixed_epoch = true;
        return peripheral_ok;
      }
      if (size == sizeof(uint32_t)) {
        clock_peripheral->fixed_epoch = *static_cast<const uint32_t*>(data);
        clock_peripheral->use_fixed_epoch = true;
        return peripheral_ok;
      }
      return peripheral_error;
    }
    case clock_cmd_clear_epoch: {
      clock_peripheral->use_fixed_epoch = false;
      clock_peripheral->fixed_epoch = 0;
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
// Justification: ABI-required function signature.
static auto clock_abi_query(void* instance, uint32_t query_id, void* out,
                            size_t* size) -> PeripheralStatus_t {
  if (size == nullptr) {
    return peripheral_error;
  }

  switch (query_id) {
    case clock_query_get_epoch: {
      constexpr size_t required_size = sizeof(ClockEpochQuery_t);
      if (out == nullptr) {
        *size = required_size;
        return peripheral_ok;
      }
      if (instance == nullptr || *size < required_size) {
        return peripheral_error;
      }
      const auto* clock_peripheral =
          static_cast<const ClockPeripheral_t*>(instance);
      auto* query = static_cast<ClockEpochQuery_t*>(out);
      query->epoch = clock_peripheral->fixed_epoch;
      query->is_fixed = clock_peripheral->use_fixed_epoch ? 1 : 0;
      *size = required_size;
      return peripheral_ok;
    }
    case clock_query_get_time: {
      constexpr size_t required_size = sizeof(ClockTimeQuery_t);
      if (out == nullptr) {
        *size = required_size;
        return peripheral_ok;
      }
      if (instance == nullptr || *size < required_size) {
        return peripheral_error;
      }
      const auto* clock_peripheral =
          static_cast<const ClockPeripheral_t*>(instance);
      auto* query = static_cast<ClockTimeQuery_t*>(out);
      constexpr int radix = 10;
      query->month = static_cast<uint8_t>(
          (clock_peripheral->latches.at(LATCH_MONTH) * radix) +
          clock_peripheral->latches.at(LATCH_MONTH + 1));
      query->day_of_week = clock_peripheral->latches.at(LATCH_WEEKDAY + 1);
      query->day = static_cast<uint8_t>(
          (clock_peripheral->latches.at(LATCH_DAY) * radix) +
          clock_peripheral->latches.at(LATCH_DAY + 1));
      query->hour = static_cast<uint8_t>(
          (clock_peripheral->latches.at(LATCH_HOUR) * radix) +
          clock_peripheral->latches.at(LATCH_HOUR + 1));
      query->minute = static_cast<uint8_t>(
          (clock_peripheral->latches.at(LATCH_MINUTE) * radix) +
          clock_peripheral->latches.at(LATCH_MINUTE + 1));
      *size = required_size;
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
// Justification: ABI-required function signature.
static auto clock_abi_save_state(void* instance, void* state_buffer,
                                 size_t* buffer_size) -> PeripheralStatus_t {
  if (buffer_size == nullptr) {
    return peripheral_error;
  }

  constexpr size_t required_size = sizeof(ClockSaveState_t);
  if (state_buffer == nullptr) {
    *buffer_size = required_size;
    return peripheral_ok;
  }

  if (instance == nullptr || *buffer_size < required_size) {
    return peripheral_error;
  }

  const auto* clock_peripheral =
      static_cast<const ClockPeripheral_t*>(instance);
  auto* state = static_cast<ClockSaveState_t*>(state_buffer);
  std::memset(state, 0, sizeof(ClockSaveState_t));
  state->version = CLOCK_STATE_VERSION;
  state->struct_size = static_cast<uint32_t>(required_size);
  state->fixed_epoch = clock_peripheral->fixed_epoch;
  std::copy(clock_peripheral->latches.begin(), clock_peripheral->latches.end(),
            state->latches);
  state->use_fixed_epoch = clock_peripheral->use_fixed_epoch ? 1 : 0;

  *buffer_size = required_size;
  return peripheral_ok;
}

static auto clock_abi_load_state(void* instance, const void* state_buffer,
                                 size_t buffer_size) -> PeripheralStatus_t {
  if (instance == nullptr || state_buffer == nullptr ||
      buffer_size != sizeof(ClockSaveState_t)) {
    return peripheral_error;
  }

  const auto* state = static_cast<const ClockSaveState_t*>(state_buffer);
  if (state->version != CLOCK_STATE_VERSION ||
      state->struct_size != sizeof(ClockSaveState_t)) {
    return peripheral_error;
  }

  for (size_t i = 0; i < CLOCK_LATCHES_COUNT; ++i) {
    if (state->latches[i] > 9) {
      return peripheral_error;
    }
  }
  if (state->latches[2] != 0) {
    return peripheral_error;
  }

  constexpr int radix = 10;
  const int month =
      (state->latches[LATCH_MONTH] * radix) + state->latches[LATCH_MONTH + 1];
  if (month > 12) {
    return peripheral_error;
  }

  const int weekday = state->latches[LATCH_WEEKDAY + 1];
  if (weekday > 6) {
    return peripheral_error;
  }

  const int day =
      (state->latches[LATCH_DAY] * radix) + state->latches[LATCH_DAY + 1];
  if (day > 31) {
    return peripheral_error;
  }

  const int hour =
      (state->latches[LATCH_HOUR] * radix) + state->latches[LATCH_HOUR + 1];
  if (hour > 23) {
    return peripheral_error;
  }

  const int minute =
      (state->latches[LATCH_MINUTE] * radix) + state->latches[LATCH_MINUTE + 1];
  if (minute > 59) {
    return peripheral_error;
  }

  auto* clock_peripheral = static_cast<ClockPeripheral_t*>(instance);
  std::copy_n(state->latches, CLOCK_LATCHES_COUNT,
              clock_peripheral->latches.begin());
  clock_peripheral->use_fixed_epoch = (state->use_fixed_epoch != 0);
  clock_peripheral->fixed_epoch = state->fixed_epoch;

  return peripheral_ok;
}

}  // namespace

static Peripheral_t g_clock_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.clock",
    .name = "Clock Card",
    .description = "ProDOS compatible Clock",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = -1,
    .init = clock_abi_init,
    .reset = clock_abi_reset,
    .shutdown = clock_abi_shutdown,
    .think = nullptr,
    .on_vblank = nullptr,
    .save_state = clock_abi_save_state,
    .load_state = clock_abi_load_state,
    .command = clock_abi_command,
    .query = clock_abi_query};

auto clock_get_descriptor() -> Peripheral_t* { return &g_clock_peripheral; }

PERIPHERAL_REGISTER(g_clock_peripheral)
