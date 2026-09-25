// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/printer/Printer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "EmbeddedRoms.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/printer/PrinterCommands.h"

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;

namespace {

constexpr uint8_t status_offline = 0xFF;
constexpr uint8_t status_busy = 0x80;
constexpr uint8_t transmit_success = 0;
constexpr uint32_t printer_activity_duration_ticks = 10;

struct PrinterCard_t {
  HostInterface_t* host = nullptr;
  int slot = 0;
  uint64_t total_chars_printed = 0;
  uint32_t busy_cycles = 0;
  uint32_t activity_ticks = 0;
  uint8_t data_latch = 0;
  uint8_t status_latch = 0;
  bool is_online = true;
  bool is_busy = false;
};

auto printer_io_read(void* instance, uint16_t program_counter,
                     uint16_t memory_address, uint8_t is_write,
                     uint8_t data_value, uint32_t executed_cycles) -> uint8_t {
  (void)program_counter;
  (void)memory_address;
  (void)data_value;

  if (instance == nullptr || is_write != 0) {
    return mem_read_floating_bus(executed_cycles);
  }
  auto* card = static_cast<PrinterCard_t*>(instance);
  if (!card->is_online) {
    return status_offline;
  }
  if (card->is_busy) {
    return status_busy;
  }
  if (card->host != nullptr && card->host->PrinterGetStatus != nullptr) {
    const uint8_t status = card->host->PrinterGetStatus(card);
    card->status_latch = status;
    return status;
  }
  return status_offline;
}

auto printer_io_write(void* instance, uint16_t program_counter,
                      uint16_t memory_address, uint8_t is_write,
                      uint8_t data_value, uint32_t executed_cycles) -> uint8_t {
  (void)program_counter;
  (void)memory_address;
  (void)executed_cycles;

  if (instance == nullptr || is_write == 0) {
    return transmit_success;
  }
  auto* card = static_cast<PrinterCard_t*>(instance);
  card->data_latch = data_value;
  card->total_chars_printed++;

  if (card->busy_cycles > 0) {
    card->is_busy = true;
  }
  card->activity_ticks = printer_activity_duration_ticks;

  if (card->host != nullptr) {
    if (card->host->NotifyActivityChanged != nullptr) {
      card->host->NotifyActivityChanged(card->slot, true);
    }
    if (card->host->PrinterPutChar != nullptr) {
      card->host->PrinterPutChar(card, data_value);
    }
  }
  return transmit_success;
}

auto printer_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }

  auto card =
      std::unique_ptr<PrinterCard_t>(new (std::nothrow) PrinterCard_t());
  if (!card) {
    return nullptr;
  }
  card->host = host;
  card->slot = slot;

#if ENABLE_ROM_PRINTER
  // Apple's PROM 341-0005 "Printer Card I Firmware P1-2" (Woz 11/1/77,
  // revised 3/17/78), the firmware of the A2B0002, listed in Appendix A of
  // the 1982 Apple II Parallel Interface Card manual.
  host->RegisterCxROM(slot, g_rom_parallel);
#endif
  host->RegisterIO(slot, printer_io_read, printer_io_write, nullptr, nullptr);

  return card.release();
}

auto printer_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* card = static_cast<PrinterCard_t*>(instance);
  card->data_latch = 0;
  card->status_latch = 0;
  card->is_online = true;
  card->is_busy = false;
  card->busy_cycles = 0;
  card->activity_ticks = 0;
  if (card->host != nullptr && card->host->NotifyActivityChanged != nullptr) {
    card->host->NotifyActivityChanged(card->slot, false);
  }
}

auto printer_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  std::unique_ptr<PrinterCard_t> card(static_cast<PrinterCard_t*>(instance));
}

auto printer_abi_think(void* instance, uint32_t elapsed_cycles) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* card = static_cast<PrinterCard_t*>(instance);
  if (card->busy_cycles > 0) {
    if (elapsed_cycles >= card->busy_cycles) {
      card->busy_cycles = 0;
      card->is_busy = false;
    } else {
      card->busy_cycles -= elapsed_cycles;
    }
  }
  if (card->activity_ticks > 0) {
    card->activity_ticks--;
    if (card->activity_ticks == 0 && card->host != nullptr &&
        card->host->NotifyActivityChanged != nullptr) {
      card->host->NotifyActivityChanged(card->slot, false);
    }
  }
}

auto printer_abi_save_state(void* instance, void* state_buffer,
                            size_t* buffer_size) -> PeripheralStatus_t {
  if (buffer_size == nullptr) {
    return peripheral_error;
  }
  constexpr size_t required_size = sizeof(PrinterSaveState_t);
  if (state_buffer == nullptr) {
    *buffer_size = required_size;
    return peripheral_ok;
  }
  if (instance == nullptr || *buffer_size < required_size) {
    return peripheral_error;
  }

  const auto* card = static_cast<const PrinterCard_t*>(instance);
  PrinterSaveState_t state{};
  state.version = PRINTER_STATE_VERSION;
  state.struct_size = static_cast<uint32_t>(required_size);
  state.total_chars_printed = card->total_chars_printed;
  state.busy_cycles = card->busy_cycles;
  state.data_latch = card->data_latch;
  state.status_latch = card->status_latch;
  state.is_online = card->is_online ? 1 : 0;
  state.is_busy = card->is_busy ? 1 : 0;
  std::memcpy(state_buffer, &state, required_size);

  *buffer_size = required_size;
  return peripheral_ok;
}

auto printer_abi_load_state(void* instance, const void* state_buffer,
                            size_t buffer_size) -> PeripheralStatus_t {
  if (instance == nullptr || state_buffer == nullptr ||
      buffer_size != sizeof(PrinterSaveState_t)) {
    return peripheral_error;
  }

  PrinterSaveState_t state{};
  std::memcpy(&state, state_buffer, sizeof(state));
  if (state.version != PRINTER_STATE_VERSION ||
      state.struct_size != sizeof(PrinterSaveState_t)) {
    return peripheral_error;
  }

  auto* card = static_cast<PrinterCard_t*>(instance);
  card->total_chars_printed = state.total_chars_printed;
  card->busy_cycles = state.busy_cycles;
  card->data_latch = state.data_latch;
  card->status_latch = state.status_latch;
  card->is_online = (state.is_online != 0);
  card->is_busy = (state.is_busy != 0);

  return peripheral_ok;
}

auto printer_abi_command(void* instance, uint32_t command_id,
                         const void* payload, size_t payload_size)
    -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  auto* card = static_cast<PrinterCard_t*>(instance);

  if (!peripheral_cmd_is_mine(command_id, PERIPHERAL_SUBSYSTEM_PRINTER)) {
    return peripheral_incompatible;  // another peripheral in the slot owns it
  }

  switch (command_id) {
    case PRINTER_CMD_SET_ONLINE: {
      if (payload == nullptr || payload_size != sizeof(PrinterOnlineCmd_t)) {
        return peripheral_error;
      }
      PrinterOnlineCmd_t cmd{};
      std::memcpy(&cmd, payload, sizeof(cmd));
      card->is_online = (cmd.online != 0);
      return peripheral_ok;
    }
    case PRINTER_CMD_RESET_STATS: {
      card->total_chars_printed = 0;
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

auto printer_abi_query(void* instance, uint32_t query_id, void* output,
                       size_t* output_size) -> PeripheralStatus_t {
  if (output_size == nullptr) {
    return peripheral_error;
  }

  if (!peripheral_cmd_is_mine(query_id, PERIPHERAL_SUBSYSTEM_PRINTER)) {
    return peripheral_incompatible;  // another peripheral in the slot owns it
  }

  switch (query_id) {
    case PRINTER_QUERY_STATUS: {
      constexpr size_t required_size = sizeof(PrinterStatusQuery_t);
      if (output == nullptr) {
        *output_size = required_size;
        return peripheral_ok;
      }
      if (*output_size < required_size) {
        *output_size = required_size;
        return peripheral_error;
      }
      if (instance == nullptr) {
        return peripheral_error;
      }
      const auto* card = static_cast<const PrinterCard_t*>(instance);
      PrinterStatusQuery_t query_out{};
      query_out.total_chars_printed = card->total_chars_printed;
      query_out.is_online = card->is_online ? 1 : 0;
      query_out.is_busy = card->is_busy ? 1 : 0;
      query_out.last_char = card->data_latch;
      std::memcpy(output, &query_out, required_size);
      *output_size = required_size;
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

}  // namespace

static const Peripheral_t g_printer_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.printer",
    .name = "Parallel Printer",
    .description = "Apple Parallel Printer Interface Card (A2B0002)",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = 1,
    .init = printer_abi_init,
    .reset = printer_abi_reset,
    .shutdown = printer_abi_shutdown,
    .think = printer_abi_think,
    .on_vblank = nullptr,
    .save_state = printer_abi_save_state,
    .load_state = printer_abi_load_state,
    .command = printer_abi_command,
    .query = printer_abi_query};

// peripheral_register and ActivePeripheral_t::api take a mutable
// Peripheral_t*, so the immutable descriptor is cast the same way
// PERIPHERAL_REGISTER casts it.
auto printer_get_descriptor() -> Peripheral_t* {
  return const_cast<Peripheral_t*>(&g_printer_peripheral);
}

PERIPHERAL_REGISTER(g_printer_peripheral)
