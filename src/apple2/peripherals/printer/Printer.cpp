// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/printer/Printer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/printer/PrinterCommands.h"

namespace {

// Apple's PROM 341-0005 "Printer Card I Firmware P1-2" (Woz 11/1/77, revised
// 3/17/78 by Huston and Sander), the firmware of the Apple Parallel Printer
// Interface Card A2B0002, as listed in Appendix A of the 1982 Apple II
// Parallel Interface Card manual (A2L0045), whose SW6-off firmware is this
// same image. res/roms/Parallel.rom holds the same 256 bytes and is what the
// build checks this transcription against. The two entry points are $Cn00
// (initialise the screen holes, then print) and $Cn02 (print); the only card
// access in the image is the STA $C080,Y at $Cn84, and the bytes at $Cn80
// (90 FE) and $Cn82 (B0 FE) are the "wait for ready" images that the card
// presents in place of $CnC0 and $CnC2 while the printer has not acknowledged.
const std::array<uint8_t, 256> printer_rom = {{
    0x18, 0xb0, 0x38, 0x48, 0x8a, 0x48, 0x98, 0x48, 0x08, 0x78, 0x20, 0x58,
    0xff, 0xba, 0x68, 0x68, 0x68, 0x68, 0xa8, 0xca, 0x9a, 0x68, 0x28, 0xaa,
    0x90, 0x38, 0xbd, 0xb8, 0x05, 0x10, 0x19, 0x98, 0x29, 0x7f, 0x49, 0x30,
    0xc9, 0x0a, 0x90, 0x3b, 0xc9, 0x78, 0xb0, 0x29, 0x49, 0x3d, 0xf0, 0x21,
    0x98, 0x29, 0x9f, 0x9d, 0x38, 0x06, 0x90, 0x7e, 0xbd, 0xb8, 0x06, 0x30,
    0x14, 0xa5, 0x24, 0xdd, 0x38, 0x07, 0xb0, 0x0d, 0xc9, 0x11, 0xb0, 0x09,
    0x09, 0xf0, 0x3d, 0x38, 0x07, 0x65, 0x24, 0x85, 0x24, 0x4a, 0x38, 0xb0,
    0x6d, 0x18, 0x6a, 0x3d, 0xb8, 0x06, 0x90, 0x02, 0x49, 0x81, 0x9d, 0xb8,
    0x06, 0xd0, 0x53, 0xa0, 0x0a, 0x7d, 0x38, 0x05, 0x88, 0xd0, 0xfa, 0x9d,
    0xb8, 0x04, 0x9d, 0x38, 0x05, 0x38, 0xb0, 0x43, 0xc5, 0x24, 0x90, 0x3a,
    0x68, 0xa8, 0x68, 0xaa, 0x68, 0x4c, 0xf0, 0xfd, 0x90, 0xfe, 0xb0, 0xfe,
    0x99, 0x80, 0xc0, 0x90, 0x37, 0x49, 0x07, 0xa8, 0x49, 0x0a, 0x0a, 0xd0,
    0x06, 0xb8, 0x85, 0x24, 0x9d, 0x38, 0x07, 0xbd, 0xb8, 0x06, 0x4a, 0x70,
    0x02, 0xb0, 0x23, 0x0a, 0x0a, 0xa9, 0x27, 0xb0, 0xcf, 0xbd, 0x38, 0x07,
    0xfd, 0xb8, 0x04, 0xc9, 0xf8, 0x90, 0x03, 0x69, 0x27, 0xac, 0xa9, 0x00,
    0x85, 0x24, 0x18, 0x7e, 0xb8, 0x05, 0x68, 0xa8, 0x68, 0xaa, 0x68, 0x60,
    0x90, 0x27, 0xb0, 0x00, 0x10, 0x11, 0xa9, 0x89, 0x9d, 0x38, 0x06, 0x9d,
    0xb8, 0x06, 0xa9, 0x28, 0x9d, 0xb8, 0x04, 0xa9, 0x02, 0x85, 0x36, 0x98,
    0x5d, 0x38, 0x06, 0x0a, 0xf0, 0x90, 0x5e, 0xb8, 0x05, 0x98, 0x48, 0x8a,
    0x0a, 0x0a, 0x0a, 0x0a, 0xa8, 0xbd, 0x38, 0x07, 0xc5, 0x24, 0x68, 0xb0,
    0x05, 0x48, 0x29, 0x80, 0x09, 0x20, 0x2c, 0x58, 0xff, 0xf0, 0x03, 0xfe,
    0x38, 0x07, 0x70, 0x84,
}};

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

  if (instance == nullptr) {
    return 0;
  }
  auto* card = static_cast<PrinterCard_t*>(instance);
  if (is_write != 0) {
    return card->host->ReadFloatingBus(executed_cycles);
  }
  if (!card->is_online) {
    return status_offline;
  }
  if (card->is_busy) {
    return status_busy;
  }
  if (card->host->PrinterGetStatus != nullptr) {
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

  if (card->host->NotifyActivityChanged != nullptr) {
    card->host->NotifyActivityChanged(card->slot, true);
  }
  if (card->host->PrinterPutChar != nullptr) {
    card->host->PrinterPutChar(card, data_value);
  }
  return transmit_success;
}

// Without a ROM PR#n never reaches the firmware, without I/O the firmware's
// store reaches nothing, and without the bus a read of the card has no byte
// to answer with: better no card than a phantom one, and the log says which
// member was missing.
auto missing_host_member(const HostInterface_t* host) -> const char* {
  if (host->RegisterIO == nullptr) {
    return "RegisterIO";
  }
  if (host->RegisterCxROM == nullptr) {
    return "RegisterCxROM";
  }
  if (host->ReadFloatingBus == nullptr) {
    return "ReadFloatingBus";
  }
  return nullptr;
}

auto printer_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }
  const char* missing = missing_host_member(host);
  if (missing != nullptr) {
    if (host->Log != nullptr) {
      host->Log(nullptr, log_error,
                "Printer card in slot %d: the host offers no %s\n", slot,
                missing);
    }
    return nullptr;
  }

  auto card =
      std::unique_ptr<PrinterCard_t>(new (std::nothrow) PrinterCard_t());
  if (!card) {
    return nullptr;
  }
  card->host = host;
  card->slot = slot;

  host->RegisterCxROM(slot, printer_rom.data());
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
  if (card->host->NotifyActivityChanged != nullptr) {
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
    if (card->activity_ticks == 0 &&
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
