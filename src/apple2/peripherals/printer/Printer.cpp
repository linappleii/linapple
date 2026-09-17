// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/printer/Printer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "EmbeddedRoms.h"
#include "apple2/peripherals/printer/PrinterCommands.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;

namespace {

constexpr size_t slot_rom_size = 0x100;
constexpr uint8_t status_offline = 0xFF;
constexpr uint8_t status_busy = 0x80;
constexpr uint8_t transmit_success = 0;
constexpr uint32_t printer_activity_duration_ticks = 10;

struct PrinterPeripheral_t {
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

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
// Justification: Parameters are part of the PeripheralIOHandler ABI.
auto print_status(void* instance, uint16_t program_counter,
                  uint16_t memory_address, uint8_t is_write, uint8_t data_value,
                  uint32_t remaining_cycles) -> uint8_t {
  (void)program_counter;
  (void)memory_address;
  (void)data_value;

  if (instance == nullptr || is_write != 0) {
    return mem_read_floating_bus(remaining_cycles);
  }
  auto* printer_peripheral = static_cast<PrinterPeripheral_t*>(instance);
  if (!printer_peripheral->is_online) {
    return status_offline;
  }
  if (printer_peripheral->is_busy) {
    return status_busy;
  }
  if (printer_peripheral->host != nullptr &&
      printer_peripheral->host->PrinterGetStatus != nullptr) {
    const uint8_t st =
        printer_peripheral->host->PrinterGetStatus(printer_peripheral);
    printer_peripheral->status_latch = st;
    return st;
  }
  return status_offline;
}

auto print_transmit(void* instance, uint16_t program_counter,
                    uint16_t memory_address, uint8_t is_write,
                    uint8_t data_value, uint32_t remaining_cycles) -> uint8_t {
  (void)program_counter;
  (void)memory_address;
  (void)remaining_cycles;

  if (instance == nullptr || is_write == 0) {
    return transmit_success;
  }
  auto* printer_peripheral = static_cast<PrinterPeripheral_t*>(instance);
  printer_peripheral->data_latch = data_value;
  printer_peripheral->total_chars_printed++;

  if (printer_peripheral->busy_cycles > 0) {
    printer_peripheral->is_busy = true;
  }
  printer_peripheral->activity_ticks = printer_activity_duration_ticks;

  if (printer_peripheral->host != nullptr) {
    if (printer_peripheral->host->NotifyActivityChanged != nullptr) {
      printer_peripheral->host->NotifyActivityChanged(printer_peripheral->slot,
                                                      true);
    }
    if (printer_peripheral->host->PrinterPutChar != nullptr) {
      printer_peripheral->host->PrinterPutChar(printer_peripheral, data_value);
    }
  }
  return transmit_success;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// NOLINTBEGIN(cppcoreguidelines-owning-memory)
// Justification: Raw pointer release is required by Peripheral_t ABI lifecycle.
static auto printer_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }

  auto printer_peripheral =
      std::unique_ptr<PrinterPeripheral_t>(new PrinterPeripheral_t());
  printer_peripheral->host = host;
  printer_peripheral->slot = slot;

#if ENABLE_ROM_PRINTER
  host->RegisterCxROM(slot, const_cast<uint8_t*>(g_rom_parallel));
#endif
  host->RegisterIO(slot, print_status, print_transmit, nullptr, nullptr);

  return printer_peripheral.release();
}

static auto printer_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* printer_peripheral = static_cast<PrinterPeripheral_t*>(instance);
  printer_peripheral->data_latch = 0;
  printer_peripheral->status_latch = 0;
  printer_peripheral->is_online = true;
  printer_peripheral->is_busy = false;
  printer_peripheral->busy_cycles = 0;
  printer_peripheral->activity_ticks = 0;
  if (printer_peripheral->host != nullptr &&
      printer_peripheral->host->NotifyActivityChanged != nullptr) {
    printer_peripheral->host->NotifyActivityChanged(printer_peripheral->slot,
                                                    false);
  }
}

static auto printer_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  std::unique_ptr<PrinterPeripheral_t> printer_peripheral(
      static_cast<PrinterPeripheral_t*>(instance));
}
// NOLINTEND(cppcoreguidelines-owning-memory)

static auto printer_abi_think(void* instance, uint32_t elapsed_cycles) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* printer_peripheral = static_cast<PrinterPeripheral_t*>(instance);
  if (printer_peripheral->busy_cycles > 0) {
    if (elapsed_cycles >= printer_peripheral->busy_cycles) {
      printer_peripheral->busy_cycles = 0;
      printer_peripheral->is_busy = false;
    } else {
      printer_peripheral->busy_cycles -= elapsed_cycles;
    }
  }
  if (printer_peripheral->activity_ticks > 0) {
    printer_peripheral->activity_ticks--;
    if (printer_peripheral->activity_ticks == 0 &&
        printer_peripheral->host != nullptr &&
        printer_peripheral->host->NotifyActivityChanged != nullptr) {
      printer_peripheral->host->NotifyActivityChanged(printer_peripheral->slot,
                                                      false);
    }
  }
}

static auto printer_abi_save_state(void* instance, void* state_buffer,
                                   size_t* buffer_size) -> PeripheralStatus_t {
  if (buffer_size == nullptr) {
    return peripheral_error;
  }
  constexpr size_t required_size = sizeof(SsCardPrinter_t);
  if (state_buffer == nullptr) {
    *buffer_size = required_size;
    return peripheral_ok;
  }
  if (instance == nullptr || *buffer_size < required_size) {
    return peripheral_error;
  }

  const auto* printer_peripheral =
      static_cast<const PrinterPeripheral_t*>(instance);
  auto* state = static_cast<SsCardPrinter_t*>(state_buffer);
  std::memset(state, 0, sizeof(SsCardPrinter_t));
  state->version = PRINTER_STATE_VERSION;
  state->struct_size = static_cast<uint32_t>(required_size);
  state->total_chars_printed = printer_peripheral->total_chars_printed;
  state->busy_cycles = printer_peripheral->busy_cycles;
  state->data_latch = printer_peripheral->data_latch;
  state->status_latch = printer_peripheral->status_latch;
  state->is_online = printer_peripheral->is_online ? 1 : 0;
  state->is_busy = printer_peripheral->is_busy ? 1 : 0;

  *buffer_size = required_size;
  return peripheral_ok;
}

static auto printer_abi_load_state(void* instance, const void* state_buffer,
                                   size_t buffer_size) -> PeripheralStatus_t {
  if (instance == nullptr || state_buffer == nullptr ||
      buffer_size != sizeof(SsCardPrinter_t)) {
    return peripheral_error;
  }

  const auto* state = static_cast<const SsCardPrinter_t*>(state_buffer);
  if (state->version != PRINTER_STATE_VERSION ||
      state->struct_size != sizeof(SsCardPrinter_t)) {
    return peripheral_error;
  }

  auto* printer_peripheral = static_cast<PrinterPeripheral_t*>(instance);
  printer_peripheral->total_chars_printed = state->total_chars_printed;
  printer_peripheral->busy_cycles = state->busy_cycles;
  printer_peripheral->data_latch = state->data_latch;
  printer_peripheral->status_latch = state->status_latch;
  printer_peripheral->is_online = (state->is_online != 0);
  printer_peripheral->is_busy = (state->is_busy != 0);

  return peripheral_ok;
}

static auto printer_abi_command(void* instance, uint32_t command_id,
                                const void* payload, size_t payload_size)
    -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  auto* printer_peripheral = static_cast<PrinterPeripheral_t*>(instance);

  switch (command_id) {
    case PRINTER_CMD_SET_ONLINE: {
      if (payload == nullptr || payload_size < sizeof(PrinterOnlineCmd_t)) {
        return peripheral_error;
      }
      const auto* cmd = static_cast<const PrinterOnlineCmd_t*>(payload);
      printer_peripheral->is_online = (cmd->online != 0);
      return peripheral_ok;
    }
    case PRINTER_CMD_RESET_STATS: {
      printer_peripheral->total_chars_printed = 0;
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

static auto printer_abi_query(void* instance, uint32_t query_id, void* output,
                              size_t* output_size) -> PeripheralStatus_t {
  if (output_size == nullptr) {
    return peripheral_error;
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
      const auto* printer_peripheral =
          static_cast<const PrinterPeripheral_t*>(instance);
      auto* query_out = static_cast<PrinterStatusQuery_t*>(output);
      std::memset(query_out, 0, sizeof(PrinterStatusQuery_t));
      query_out->total_chars_printed = printer_peripheral->total_chars_printed;
      query_out->is_online = printer_peripheral->is_online ? 1 : 0;
      query_out->is_busy = printer_peripheral->is_busy ? 1 : 0;
      query_out->last_char = printer_peripheral->data_latch;
      *output_size = required_size;
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

}  // namespace

static Peripheral_t g_printer_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.printer",
    .name = "Parallel Printer",
    .description = "Standard parallel printer interface emulation",
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

auto printer_get_descriptor() -> Peripheral_t* { return &g_printer_peripheral; }

PERIPHERAL_REGISTER(g_printer_peripheral)
