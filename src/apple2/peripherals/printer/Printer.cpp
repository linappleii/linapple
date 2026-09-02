// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-owning-memory)
#include "apple2/peripherals/printer/Printer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "core/Peripheral.h"

namespace {

constexpr size_t slot_rom_size = 0x100;
constexpr uint8_t status_offline = 0xFF;
constexpr uint8_t transmit_success = 0;

const std::array<uint8_t, slot_rom_size> Parallel_bin = {
    {0x18, 0xB0, 0x38, 0x48, 0x8A, 0x48, 0x98, 0x48, 0x08, 0x78, 0x20, 0x58,
     0xFF, 0xBA, 0x68, 0x68, 0x68, 0x68, 0xA8, 0xCA, 0x9A, 0x68, 0x28, 0xAA,
     0x90, 0x38, 0xBD, 0xB8, 0x05, 0x10, 0x19, 0x98, 0x29, 0x7F, 0x49, 0x30,
     0xC9, 0x0A, 0x90, 0x3B, 0xC9, 0x78, 0xB0, 0x29, 0x49, 0x3D, 0xF0, 0x21,
     0x98, 0x29, 0x9F, 0x9D, 0x38, 0x06, 0x90, 0x7E, 0xBD, 0xB8, 0x06, 0x30,
     0x14, 0xA5, 0x24, 0xDD, 0x38, 0x07, 0xB0, 0x0D, 0xC9, 0x11, 0xB0, 0x09,
     0x09, 0xF0, 0x3D, 0x38, 0x07, 0x65, 0x24, 0x85, 0x24, 0x4A, 0x38, 0xB0,
     0x6D, 0x18, 0x6A, 0x3D, 0xB8, 0x06, 0x90, 0x02, 0x49, 0x81, 0x9D, 0xB8,
     0x06, 0xD0, 0x53, 0xA0, 0x0A, 0x7D, 0x38, 0x05, 0x88, 0xD0, 0xFA, 0x9D,
     0xB8, 0x04, 0x9D, 0x38, 0x05, 0x38, 0xB0, 0x43, 0xC5, 0x24, 0x90, 0x3A,
     0x68, 0xA8, 0x68, 0xAA, 0x68, 0x4C, 0xF0, 0xFD, 0x90, 0xFE, 0xB0, 0xFE,
     0x99, 0x80, 0xC0, 0x90, 0x37, 0x49, 0x07, 0xA8, 0x49, 0x0A, 0x0A, 0xD0,
     0x06, 0xB8, 0x85, 0x24, 0x9D, 0x38, 0x07, 0xBD, 0xB8, 0x06, 0x4A, 0x70,
     0x02, 0xB0, 0x23, 0x0A, 0x0A, 0xA9, 0x27, 0xB0, 0xCF, 0xBD, 0x38, 0x07,
     0xFD, 0xB8, 0x04, 0xC9, 0xF8, 0x90, 0x03, 0x69, 0x27, 0xAC, 0xA9, 0x00,
     0x85, 0x24, 0x18, 0x7E, 0xB8, 0x05, 0x68, 0xA8, 0x68, 0xAA, 0x68, 0x60,
     0x90, 0x27, 0xB0, 0x00, 0x10, 0x11, 0xA9, 0x89, 0x9D, 0x38, 0x06, 0x9D,
     0xB8, 0x06, 0xA9, 0x28, 0x9D, 0xB8, 0x04, 0xA9, 0x02, 0x85, 0x36, 0x98,
     0x5D, 0x38, 0x06, 0x0A, 0xF0, 0x90, 0x5E, 0xB8, 0x05, 0x98, 0x48, 0x8A,
     0x0A, 0x0A, 0x0A, 0x0A, 0xA8, 0xBD, 0x38, 0x07, 0xC5, 0x24, 0x68, 0xB0,
     0x05, 0x48, 0x29, 0x80, 0x09, 0x20, 0x2C, 0x58, 0xFF, 0xF0, 0x03, 0xFE,
     0x38, 0x07, 0x70, 0x84}};

struct PrinterPeripheral_t {
  HostInterface_t* host = nullptr;
  int slot = 0;
};

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
// Justification: Parameters are part of the PeripheralIOHandler ABI.
auto print_status(void* instance, uint16_t program_counter,
                  uint16_t memory_address, uint8_t is_write, uint8_t data_value,
                  uint32_t remaining_cycles) -> uint8_t {
  (void)program_counter;
  (void)memory_address;
  (void)data_value;
  (void)remaining_cycles;

  if (instance == nullptr || is_write != 0) {
    return status_offline;
  }
  auto* printer_peripheral = static_cast<PrinterPeripheral_t*>(instance);
  if (printer_peripheral->host != nullptr &&
      printer_peripheral->host->PrinterGetStatus != nullptr) {
    return printer_peripheral->host->PrinterGetStatus(printer_peripheral);
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
  if (printer_peripheral->host != nullptr &&
      printer_peripheral->host->PrinterPutChar != nullptr) {
    printer_peripheral->host->PrinterPutChar(printer_peripheral, data_value);
  }
  return transmit_success;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

static auto printer_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }

  auto printer_peripheral =
      std::unique_ptr<PrinterPeripheral_t>(new PrinterPeripheral_t());
  printer_peripheral->host = host;
  printer_peripheral->slot = slot;

  std::array<uint8_t, slot_rom_size> slot_rom_data{};
  const size_t bytes_to_copy = std::min(Parallel_bin.size(), slot_rom_size);

  std::copy_n(Parallel_bin.begin(), bytes_to_copy, slot_rom_data.begin());

  host->RegisterCxROM(slot, slot_rom_data.data());
  host->RegisterIO(slot, print_status, print_transmit, nullptr, nullptr);

  return printer_peripheral.release();
}

static auto printer_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* printer_peripheral = static_cast<PrinterPeripheral_t*>(instance);
  (void)printer_peripheral;
}

static auto printer_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  std::unique_ptr<PrinterPeripheral_t> printer_peripheral(
      static_cast<PrinterPeripheral_t*>(instance));
}

static auto printer_abi_think(void* instance, uint32_t elapsed_cycles) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* printer_peripheral = static_cast<PrinterPeripheral_t*>(instance);
  (void)printer_peripheral;
  (void)elapsed_cycles;
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
    .save_state = nullptr,
    .load_state = nullptr,
    .command = nullptr,
    .query = nullptr};

auto printer_get_descriptor() -> Peripheral_t* { return &g_printer_peripheral; }

PERIPHERAL_REGISTER(g_printer_peripheral)
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-owning-memory)
