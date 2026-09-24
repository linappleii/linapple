// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/clock/ClockCard.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/clock/ClockCardCommands.h"

namespace {

// ProDOS 8 ThunderClock firmware emulation ($Cn00 ID, $Cn08 READ, $Cn0B WRITE).
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

constexpr size_t latch_count = CLOCKCARD_LATCH_COUNT;
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
  bool reported_missing_time = false;
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

// Latch local time fields from host platform interface.
auto update_latches(ClockCard_t* card) -> void {
  HostLocalTime_t now{};
  if (!card->host->GetLocalTime(&now)) {
    if (!card->reported_missing_time && card->host->Log != nullptr) {
      card->host->Log(card, log_warn,
                      "Clock card in slot %d: the host has no local time\n",
                      card->slot);
      card->reported_missing_time = true;
    }
    return;
  }

  set_latch_pair(card, latch_month, now.month);
  set_latch_pair(card, latch_weekday, now.weekday);
  set_latch_pair(card, latch_day, now.day_of_month);
  set_latch_pair(card, latch_hour, now.hour);
  set_latch_pair(card, latch_minute, now.minute);
}

auto pair_value(const uint8_t* latches, size_t index) -> int {
  return (latches[index] * radix) + latches[index + 1];
}

// Only the ten latch registers drive the bus. The strobe and the unmapped
// offsets leave it floating, so the read returns whatever the video scanner
// is fetching on that cycle, which only the host knows.
auto clockcard_io_read(void* instance, uint16_t program_counter,
                       uint16_t memory_address, uint8_t is_write,
                       uint8_t data_value, uint32_t executed_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)is_write;
  (void)data_value;
  if (instance == nullptr) {
    return 0;
  }
  auto* card = static_cast<ClockCard_t*>(instance);

  const size_t register_offset = memory_address & io_register_mask;
  if (register_offset < latch_count) {
    return card->latches.at(register_offset);
  }
  if (register_offset == strobe_register) {
    update_latches(card);
  }
  return card->host->ReadFloatingBus(executed_cycles);
}

auto missing_host_member(const HostInterface_t* host) -> const char* {
  if (host->RegisterIO == nullptr) {
    return "RegisterIO";
  }
  if (host->RegisterCxROM == nullptr) {
    return "RegisterCxROM";
  }
  if (host->GetLocalTime == nullptr) {
    return "GetLocalTime";
  }
  if (host->ReadFloatingBus == nullptr) {
    return "ReadFloatingBus";
  }
  return nullptr;
}

auto clockcard_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }
  const char* missing = missing_host_member(host);
  if (missing != nullptr) {
    if (host->Log != nullptr) {
      host->Log(nullptr, log_error,
                "Clock card in slot %d: the host offers no %s\n", slot,
                missing);
    }
    return nullptr;
  }

  auto card = std::unique_ptr<ClockCard_t>(new (std::nothrow) ClockCard_t());
  if (!card) {
    return nullptr;
  }
  card->slot = slot;
  card->host = host;

  host->RegisterCxROM(slot, clock_rom.data());
  host->RegisterIO(slot, clockcard_io_read, nullptr, nullptr, nullptr);

  return card.release();
}

// Retain latched time across RESET.
auto clockcard_abi_reset(void* instance) -> void { (void)instance; }

auto clockcard_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }

  std::unique_ptr<ClockCard_t> card(static_cast<ClockCard_t*>(instance));
}

// Clock card has no mutable commands or query endpoints.
auto clockcard_abi_command(void* instance, uint32_t cmd_id, const void* data,
                           size_t size) -> PeripheralStatus_t {
  (void)cmd_id;
  (void)data;
  (void)size;
  if (instance == nullptr) {
    return peripheral_error;
  }
  return peripheral_incompatible;
}

auto clockcard_abi_query(void* instance, uint32_t query_id, void* out,
                         size_t* size) -> PeripheralStatus_t {
  (void)instance;
  (void)query_id;
  (void)out;
  if (size == nullptr) {
    return peripheral_error;
  }
  return peripheral_incompatible;
}

static_assert(sizeof(ClockCardSaveState_t) == 32,
              "the clock card's state frame is part of the plugin ABI");
static_assert(offsetof(ClockCardSaveState_t, version) == 0,
              "the frame header is version then size");
static_assert(offsetof(ClockCardSaveState_t, struct_size) == 4,
              "the frame header is version then size");
static_assert(offsetof(ClockCardSaveState_t, fixed_epoch) == 8,
              "the pin fields keep their place so every frame written loads");
static_assert(offsetof(ClockCardSaveState_t, latches) == 16,
              "the latches sit where every frame written has them");
static_assert(offsetof(ClockCardSaveState_t, use_fixed_epoch) == 26,
              "the latches fill bytes 16 through 25");
static_assert(offsetof(ClockCardSaveState_t, reserved) == 27,
              "the pin flag is the byte after the latches");

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
  ClockCardSaveState_t state{};
  state.version = CLOCKCARD_STATE_VERSION;
  state.struct_size = static_cast<uint32_t>(required_size);
  std::copy(card->latches.begin(), card->latches.end(), state.latches);
  std::memcpy(state_buffer, &state, required_size);

  *buffer_size = required_size;
  return peripheral_ok;
}

auto latches_form_a_calendar(const uint8_t* latches) -> bool {
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

// Load state bounded by recorded struct_size.
auto clockcard_abi_load_state(void* instance, const void* state_buffer,
                              size_t buffer_size) -> PeripheralStatus_t {
  constexpr size_t header_size = offsetof(ClockCardSaveState_t, fixed_epoch);
  if (instance == nullptr || state_buffer == nullptr ||
      buffer_size < header_size) {
    return peripheral_error;
  }

  ClockCardSaveState_t state{};
  std::memcpy(&state, state_buffer, header_size);
  if (state.struct_size != sizeof(state) || buffer_size < state.struct_size) {
    return peripheral_error;
  }
  if (state.version != CLOCKCARD_STATE_VERSION) {
    return peripheral_error;
  }

  std::memcpy(&state, state_buffer, state.struct_size);
  if (!latches_form_a_calendar(state.latches)) {
    return peripheral_error;
  }

  auto* card = static_cast<ClockCard_t*>(instance);
  std::copy_n(state.latches, latch_count, card->latches.begin());
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

// Peripheral registry requires non-const pointer.
auto clockcard_get_descriptor() -> Peripheral_t* {
  return const_cast<Peripheral_t*>(&g_clockcard_peripheral);
}

PERIPHERAL_REGISTER(g_clockcard_peripheral)
