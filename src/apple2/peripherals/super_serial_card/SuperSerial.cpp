// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/super_serial_card/SuperSerial.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "EmbeddedRoms.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"

namespace {

constexpr int min_slot = 1;
constexpr int max_slot = 7;
constexpr size_t fifo_size = 9;
constexpr uint16_t io_register_mask = 0x0F;

// SW1 OFF OFF OFF ON ON ON ON and SW2 ON ON ON ON OFF ON OFF: communications
// mode at 9600 baud, 8 data bits, no parity, one stop bit, no line feed after
// a carriage return, DCD from DB-25 pin 8 and the ACIA's interrupt line
// reaching the slot. Neither manual states a factory setting; this is the
// 1981 manual's communications example (Table 3-1) at the card's top rate.
constexpr uint8_t default_switches_1 = 0x78;
constexpr uint8_t default_switches_2 = 0x2F;
constexpr uint8_t switch_bit_7 = 0x80;

static_assert(sizeof(SuperSerialSaveState_t) == 56,
              "the serial card's state frame is part of the plugin ABI");
static_assert(offsetof(SuperSerialSaveState_t, version) == 0,
              "the frame header is version then size");
static_assert(offsetof(SuperSerialSaveState_t, struct_size) == 4,
              "the frame header is version then size");
static_assert(offsetof(SuperSerialSaveState_t, rx_count) == 8,
              "the dead fields keep their place so every frame written loads");
static_assert(offsetof(SuperSerialSaveState_t, control_byte) == 12,
              "the control register sits where every frame written has it");
static_assert(offsetof(SuperSerialSaveState_t, command_byte) == 13,
              "the command register sits where every frame written has it");
static_assert(offsetof(SuperSerialSaveState_t, is_irq_pending) == 14,
              "the IRQ latch sits where every frame written has it");
static_assert(offsetof(SuperSerialSaveState_t, is_rx_irq_enabled) == 15,
              "the dead fields keep their place so every frame written loads");
static_assert(offsetof(SuperSerialSaveState_t, is_tx_irq_enabled) == 16,
              "the dead fields keep their place so every frame written loads");
static_assert(offsetof(SuperSerialSaveState_t, was_tx_written) == 17,
              "the dead fields keep their place so every frame written loads");
static_assert(offsetof(SuperSerialSaveState_t, rx_buffer) == 18,
              "the dead fields keep their place so every frame written loads");
static_assert(offsetof(SuperSerialSaveState_t, status_latches) == 27,
              "the latch byte is the one byte older frames held in reserve");
static_assert(offsetof(SuperSerialSaveState_t, config) == 28,
              "the dead fields keep their place so every frame written loads");
static_assert(sizeof(SuperSerialSaveState_t::config) == 24,
              "the dead fields keep their size so every frame written loads");
static_assert(
    offsetof(SuperSerialSaveState_t, receive_data) == 52,
    "the data bytes take the four bytes older frames held in reserve");
static_assert(
    offsetof(SuperSerialSaveState_t, transmit_data) == 53,
    "the data bytes take the four bytes older frames held in reserve");
static_assert(
    offsetof(SuperSerialSaveState_t, shift_data) == 54,
    "the data bytes take the four bytes older frames held in reserve");
static_assert(offsetof(SuperSerialSaveState_t, reserved1) == 55,
              "one reserved byte remains");

// SY6551 control register bits 3-0 (SY6551 Fig. 6, p. 3-175). Code 0 is the
// 16x external clock, which the card does not provide (1981 manual p. 54).
constexpr std::array<uint32_t, 16> baud_table = {{0, 50, 75, 110, 135, 150, 300,
                                                  600, 1200, 1800, 2400, 3600,
                                                  4800, 7200, 9600, 19200}};

struct SuperSerialCard_t {
  HostInterface_t* host = nullptr;
  int slot = 0;
  SuperSerialSwitches_t switches{default_switches_1, default_switches_2};

  uint8_t control_byte = 0;
  uint8_t command_byte = 0;

  std::array<uint8_t, fifo_size> rx_buffer{};
  uint32_t rx_count = 0;

  bool is_tx_irq_enabled = false;
  bool is_rx_irq_enabled = false;
  bool was_tx_written = false;
  bool is_irq_pending = false;
};

auto release_irq(SuperSerialCard_t* card) -> void {
  if (!card->is_irq_pending) {
    return;
  }
  card->is_irq_pending = false;
  card->host->AssertIrq(card->slot, false);
}

auto notify_host_state(SuperSerialCard_t* card) -> void {
  if (card->host->SerialUpdateState == nullptr) {
    return;
  }
  const uint32_t baud = baud_table.at(card->control_byte & 0x0F);
  const uint32_t bits = 8 - ((card->control_byte >> 5) & 0x03);

  int stop = 0;
  if ((card->control_byte & 0x80) != 0) {
    stop = (bits == 5 && (card->command_byte & 0x20) == 0) ? 1 : 2;
  }

  int parity = 0;
  if ((card->command_byte & 0x20) != 0) {
    parity = 1 + ((card->command_byte >> 6) & 0x03);
  }

  card->host->SerialUpdateState(card, baud, bits, parity, stop);
}

auto reset_registers(SuperSerialCard_t* card) -> void {
  release_irq(card);
  card->control_byte = 0;
  card->command_byte = 0;
  card->rx_count = 0;
  card->is_tx_irq_enabled = false;
  card->is_rx_irq_enabled = false;
  card->was_tx_written = false;
  card->rx_buffer.fill(0);
}

auto super_serial_io_read(void* instance, uint16_t program_counter,
                          uint16_t memory_address, uint8_t is_write,
                          uint8_t data_value, uint32_t executed_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)data_value;
  (void)is_write;
  if (instance == nullptr) {
    return 0;
  }
  auto* card = static_cast<SuperSerialCard_t*>(instance);

  switch (memory_address & io_register_mask) {
    case 8: {
      uint8_t byte = 0;
      if (card->rx_count > 0) {
        byte = card->rx_buffer.at(0);
        std::copy(card->rx_buffer.begin() + 1, card->rx_buffer.end(),
                  card->rx_buffer.begin());
        card->rx_count--;
      }
      release_irq(card);
      return byte;
    }
    case 9: {
      uint8_t status = 0x10;
      if (card->rx_count > 0) {
        status |= 0x08;
      }
      if (card->is_irq_pending) {
        status |= 0x80;
      }
      return status;
    }
    case 10:
      return card->command_byte;
    case 11:
      return card->control_byte;
    default:
      break;
  }

  return card->host->ReadFloatingBus(executed_cycles);
}

auto super_serial_io_write(void* instance, uint16_t program_counter,
                           uint16_t memory_address, uint8_t is_write,
                           uint8_t data_value, uint32_t executed_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)executed_cycles;
  (void)is_write;
  if (instance == nullptr) {
    return 0;
  }
  auto* card = static_cast<SuperSerialCard_t*>(instance);

  switch (memory_address & io_register_mask) {
    case 8:
      if (card->host->SerialTransmitByte != nullptr) {
        card->host->SerialTransmitByte(card, data_value);
      }
      card->was_tx_written = true;
      break;
    case 9:
      reset_registers(card);
      break;
    case 10:
      card->command_byte = data_value;
      card->is_rx_irq_enabled = ((data_value & 0x02) == 0);
      card->is_tx_irq_enabled = ((data_value & 0x0C) == 0x04);
      if (!card->is_rx_irq_enabled) {
        release_irq(card);
      }
      notify_host_state(card);
      break;
    case 11:
      card->control_byte = data_value;
      notify_host_state(card);
      break;
    default:
      break;
  }
  return 0;
}

// Without the slot line the firmware's interrupt switch has nothing to
// forward, without the ROMs PR#n never reaches the firmware, without the bus
// and the clock the ACIA cannot answer an undecoded read or pace a character,
// and without the sink the line has no far end: better no card than a
// phantom one, and the log says which member was missing. Log itself is the
// one refusal nothing can report.
auto missing_host_member(const HostInterface_t* host) -> const char* {
  if (host->AssertIrq == nullptr) {
    return "AssertIrq";
  }
  if (host->RegisterIO == nullptr) {
    return "RegisterIO";
  }
  if (host->RegisterCxROM == nullptr) {
    return "RegisterCxROM";
  }
  if (host->RegisterExpansionROM == nullptr) {
    return "RegisterExpansionROM";
  }
  if (host->GetCycles == nullptr) {
    return "GetCycles";
  }
  if (host->GetClockHz == nullptr) {
    return "GetClockHz";
  }
  if (host->ReadFloatingBus == nullptr) {
    return "ReadFloatingBus";
  }
  if (host->SinkOpen == nullptr) {
    return "SinkOpen";
  }
  if (host->SinkWrite == nullptr) {
    return "SinkWrite";
  }
  if (host->SinkClose == nullptr) {
    return "SinkClose";
  }
  if (host->SinkRead == nullptr) {
    return "SinkRead";
  }
  if (host->SinkSetLine == nullptr) {
    return "SinkSetLine";
  }
  if (host->SinkGetLines == nullptr) {
    return "SinkGetLines";
  }
  if (host->ScheduleEvent == nullptr) {
    return "ScheduleEvent";
  }
  return nullptr;
}

auto super_serial_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr || host->Log == nullptr) {
    return nullptr;
  }
  const char* missing = missing_host_member(host);
  if (missing != nullptr) {
    host->Log(nullptr, log_error,
              "Super Serial Card in slot %d: the host offers no %s\n", slot,
              missing);
    return nullptr;
  }
  if (slot < min_slot || slot > max_slot) {
    host->Log(nullptr, log_error,
              "Super Serial Card in slot %d: an expansion card sits in slots "
              "1 to 7\n",
              slot);
    return nullptr;
  }

  auto card = std::unique_ptr<SuperSerialCard_t>(new (std::nothrow)
                                                     SuperSerialCard_t());
  if (!card) {
    return nullptr;
  }
  card->host = host;
  card->slot = slot;

#if ENABLE_ROM_SSC
  host->RegisterCxROM(slot, g_rom_ssc);
#endif
  host->RegisterIO(slot, super_serial_io_read, super_serial_io_write, nullptr,
                   nullptr);

  return card.release();
}

auto super_serial_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  reset_registers(static_cast<SuperSerialCard_t*>(instance));
}

auto super_serial_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  std::unique_ptr<SuperSerialCard_t> card(
      static_cast<SuperSerialCard_t*>(instance));
  release_irq(card.get());
}

auto super_serial_abi_command(void* instance, uint32_t command_id,
                              const void* payload, size_t payload_size)
    -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  if (!peripheral_cmd_is_mine(command_id, PERIPHERAL_SUBSYSTEM_SERIAL)) {
    return peripheral_incompatible;
  }
  if (command_id != SUPER_SERIAL_CMD_SET_SWITCHES) {
    return peripheral_incompatible;
  }
  if (payload == nullptr || payload_size != sizeof(SuperSerialSwitches_t)) {
    return peripheral_error;
  }
  SuperSerialSwitches_t switches{};
  std::memcpy(&switches, payload, sizeof(switches));
  if ((switches.sw1 & switch_bit_7) != 0 ||
      (switches.sw2 & switch_bit_7) != 0) {
    return peripheral_error;
  }
  auto* card = static_cast<SuperSerialCard_t*>(instance);
  card->switches = switches;
  return peripheral_ok;
}

// The card answers no query: its switches are the frontend's to know and its
// registers are read at $C0n8-$C0nB.
auto super_serial_abi_query(void* instance, uint32_t query_id, void* output,
                            size_t* output_size) -> PeripheralStatus_t {
  (void)instance;
  (void)query_id;
  (void)output;
  if (output_size == nullptr) {
    return peripheral_error;
  }
  return peripheral_incompatible;
}

auto super_serial_abi_save_state(void* instance, void* state_buffer,
                                 size_t* buffer_size) -> PeripheralStatus_t {
  if (buffer_size == nullptr) {
    return peripheral_error;
  }
  constexpr size_t required_size = sizeof(SuperSerialSaveState_t);
  if (state_buffer == nullptr) {
    *buffer_size = required_size;
    return peripheral_ok;
  }
  if (instance == nullptr || *buffer_size < required_size) {
    return peripheral_error;
  }

  const auto* card = static_cast<const SuperSerialCard_t*>(instance);
  SuperSerialSaveState_t state{};
  state.version = SUPER_SERIAL_STATE_VERSION;
  state.struct_size = static_cast<uint32_t>(required_size);
  state.rx_count = card->rx_count;
  state.control_byte = card->control_byte;
  state.command_byte = card->command_byte;
  state.is_irq_pending = card->is_irq_pending ? 1 : 0;
  state.is_rx_irq_enabled = card->is_rx_irq_enabled ? 1 : 0;
  state.is_tx_irq_enabled = card->is_tx_irq_enabled ? 1 : 0;
  state.was_tx_written = card->was_tx_written ? 1 : 0;
  std::copy(card->rx_buffer.begin(), card->rx_buffer.end(), state.rx_buffer);
  std::memcpy(state_buffer, &state, required_size);

  *buffer_size = required_size;
  return peripheral_ok;
}

auto super_serial_abi_load_state(void* instance, const void* state_buffer,
                                 size_t buffer_size) -> PeripheralStatus_t {
  constexpr size_t required_size = sizeof(SuperSerialSaveState_t);
  if (instance == nullptr || state_buffer == nullptr ||
      buffer_size != required_size) {
    return peripheral_error;
  }
  SuperSerialSaveState_t state{};
  std::memcpy(&state, state_buffer, required_size);
  if (state.version != SUPER_SERIAL_STATE_VERSION ||
      state.struct_size != required_size) {
    return peripheral_error;
  }

  auto* card = static_cast<SuperSerialCard_t*>(instance);
  card->control_byte = state.control_byte;
  card->command_byte = state.command_byte;
  card->rx_count = std::min(state.rx_count, static_cast<uint32_t>(fifo_size));
  card->is_irq_pending = (state.is_irq_pending != 0);
  card->is_rx_irq_enabled = (state.is_rx_irq_enabled != 0);
  card->is_tx_irq_enabled = (state.is_tx_irq_enabled != 0);
  card->was_tx_written = (state.was_tx_written != 0);
  std::copy_n(state.rx_buffer, fifo_size, card->rx_buffer.begin());

  card->host->AssertIrq(card->slot, card->is_irq_pending);
  notify_host_state(card);
  return peripheral_ok;
}

}  // namespace

static const Peripheral_t super_serial_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.ssc",
    .name = "Super Serial Card",
    .description =
        "Apple II Super Serial Card (Installation and Operating Manual, 1981)",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = 2,
    .init = super_serial_abi_init,
    .reset = super_serial_abi_reset,
    .shutdown = super_serial_abi_shutdown,
    .think = nullptr,
    .on_vblank = nullptr,
    .save_state = super_serial_abi_save_state,
    .load_state = super_serial_abi_load_state,
    .command = super_serial_abi_command,
    .query = super_serial_abi_query};

// peripheral_register and ActivePeripheral_t::api take a mutable
// Peripheral_t*, so the immutable descriptor is cast the same way
// PERIPHERAL_REGISTER casts it.
auto super_serial_get_descriptor() -> Peripheral_t* {
  return const_cast<Peripheral_t*>(&super_serial_peripheral);
}

PERIPHERAL_REGISTER(super_serial_peripheral)
