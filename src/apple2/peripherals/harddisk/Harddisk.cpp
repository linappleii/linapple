// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-owning-memory, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-type-const-cast, bugprone-easily-swappable-parameters, modernize-make-unique, google-runtime-int)
// Justification: This module
// implements low-level hardware emulation using procedural C-style patterns for
// performance and ABI compatibility. Pointer arithmetic and C-style arrays are
// required for block buffer manipulation and ROM data.
// easily-swappable-parameters is mandated by the project-wide Peripheral ABI
// signatures. modernize-make-unique is suppressed to maintain C++11
// compatibility. google-runtime-int is required for fseek offsets.

#include "apple2/peripherals/harddisk/Harddisk.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>

#include "apple2/Memory.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/HarddiskLoader.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

#ifndef VERSIONSTRING
#define VERSIONSTRING "3.1.0"
#endif

namespace {

namespace physical {
constexpr int block_size = 512;
constexpr int rom_size = 256;
constexpr int default_slot = 7;
constexpr int path_max = 512;
constexpr uint8_t unit_num_drive_bit = 7;
}  // namespace physical

namespace regs {
constexpr uint8_t cmd_exec = 0xF0;
constexpr uint8_t error = 0xF1;
constexpr uint8_t command = 0xF2;
constexpr uint8_t unit = 0xF3;
constexpr uint8_t memblock_lo = 0xF4;
constexpr uint8_t memblock_hi = 0xF5;
constexpr uint8_t diskblock_lo = 0xF6;
constexpr uint8_t diskblock_hi = 0xF7;
constexpr uint8_t buffer = 0xF8;

constexpr uint16_t io_addr_base = 0xC0F0;
constexpr uint16_t io_addr_end = 0xC0F8;
constexpr uint8_t io_addr_hi_mask = 0xFF;
}  // namespace regs

enum HarddiskIOCommand_e {
  hd_io_cmd_status = 0x00,
  hd_io_cmd_read = 0x01,
  hd_io_cmd_write = 0x02,
  hd_io_cmd_format = 0x03
};

namespace status {
constexpr uint8_t ok = 0x00;
constexpr uint8_t unknown_error = 0x03;
constexpr uint8_t io_error = 0x08;
}  // namespace status

const std::array<uint8_t, physical::rom_size> harddisk_rom = {
    {0xA9, 0x20, 0xA9, 0x00, 0xA9, 0x03, 0xA9, 0x3C, 0xA9, 0x00, 0x8D, 0xF2,
     0xC0, 0xA9, 0x70, 0x8D, 0xF3, 0xC0, 0xAD, 0xF0, 0xC0, 0x48, 0xAD, 0xF1,
     0xC0, 0x18, 0xC9, 0x01, 0xD0, 0x01, 0x38, 0x68, 0x90, 0x03, 0x4C, 0x00,
     0xC6, 0xA9, 0x70, 0x85, 0x43, 0xA9, 0x00, 0x85, 0x44, 0x85, 0x46, 0x85,
     0x47, 0xA9, 0x08, 0x85, 0x45, 0xA9, 0x01, 0x85, 0x42, 0x20, 0x46, 0xC7,
     0x90, 0x03, 0x4C, 0x00, 0xC6, 0xA2, 0x70, 0x4C, 0x01, 0x08, 0x18, 0xA5,
     0x42, 0x8D, 0xF2, 0xC0, 0xA5, 0x43, 0x8D, 0xF3, 0xC0, 0xA5, 0x44, 0x8D,
     0xF4, 0xC0, 0xA5, 0x45, 0x8D, 0xF5, 0xC0, 0xA5, 0x46, 0x8D, 0xF6, 0xC0,
     0xA5, 0x47, 0x8D, 0xF7, 0xC0, 0xAD, 0xF0, 0xC0, 0x48, 0xA5, 0x42, 0xC9,
     0x01, 0xD0, 0x03, 0x20, 0x7D, 0xC7, 0xAD, 0xF1, 0xC0, 0x18, 0xC9, 0x01,
     0xD0, 0x01, 0x38, 0x68, 0x60, 0x98, 0x48, 0xA0, 0x00, 0xAD, 0xF8, 0xC0,
     0x91, 0x44, 0xC8, 0xD0, 0xF8, 0xE6, 0x45, 0xA0, 0x00, 0xAD, 0xF8, 0xC0,
     0x91, 0x44, 0xC8, 0xD0, 0xF8, 0x68, 0xA8, 0x60, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0xFF, 0x7F, 0xD7, 0x46}};

struct HarddiskDrive_t {
  char image_name[harddisk_status_name_max]{};
  char full_path[harddisk_status_path_max]{};
  uint8_t error_code = 0;
  uint16_t memory_address = 0;
  uint16_t disk_block = 0;
  uint16_t buffer_ptr = 0;
  bool is_loaded = false;
  HarddiskFormatDriver_t* driver = nullptr;
  void* driver_instance = nullptr;
  bool os_readonly = false;
  bool user_write_protected = false;
  HarddiskError_e last_error = harddisk_err_none;
  std::array<uint8_t, physical::block_size> data_buffer{};

  HarddiskDrive_t() = default;
  ~HarddiskDrive_t() = default;

  HarddiskDrive_t(const HarddiskDrive_t&) = delete;
  auto operator=(const HarddiskDrive_t&) -> HarddiskDrive_t& = delete;
  HarddiskDrive_t(HarddiskDrive_t&&) = default;
  auto operator=(HarddiskDrive_t&&) -> HarddiskDrive_t& = default;
};

struct HarddiskPeripheral_t {
  std::array<HarddiskDrive_t, harddisk_drive_count> drives{};
  uint8_t unit_num = 0;
  uint8_t command_reg = 0;
  bool rom_active = false;
  bool is_enabled = false;
  uint32_t slot = 0;
  int activity_status = harddisk_status_off;
  HostInterface_t* host = nullptr;

  HarddiskPeripheral_t() = default;
  ~HarddiskPeripheral_t() = default;

  HarddiskPeripheral_t(const HarddiskPeripheral_t&) = delete;
  auto operator=(const HarddiskPeripheral_t&) -> HarddiskPeripheral_t& = delete;
  HarddiskPeripheral_t(HarddiskPeripheral_t&&) = default;
  auto operator=(HarddiskPeripheral_t&&) -> HarddiskPeripheral_t& = default;
};

auto is_drive_valid(int drive_index) -> bool {
  return (drive_index >= 0 && drive_index < harddisk_drive_count);
}

// Why: Extracts the base filename and full path from an image string,
// providing a clean title for the UI while ensuring paths are stored.
auto update_image_metadata(HarddiskDrive_t* drive_ptr, const char* path)
    -> void {
  if (drive_ptr == nullptr || path == nullptr) {
    return;
  }

  char title[harddisk_status_path_max];
  const char* start_pos = path;

  const char* last_sep = strrchr(start_pos, file_separator);
  if (last_sep != nullptr) {
    start_pos = last_sep + 1;
  }
  util_safe_strcpy(title, start_pos, harddisk_status_path_max);

  bool found_lower = false;
  int title_length = 0;
  while (title[title_length] != '\0' && !found_lower) {
    if (is_char_lower(title[title_length])) {
      found_lower = true;
    } else {
      title_length++;
    }
  }

  constexpr int min_title_len_for_format = 3;
  if (!found_lower && title_length >= min_title_len_for_format) {
    for (char* p = title + 1; *p != '\0'; ++p) {
      *p = static_cast<char>(tolower(static_cast<uint8_t>(*p)));
    }
  }

  util_safe_strcpy(drive_ptr->full_path, path, harddisk_status_path_max);

  char* extension_dot = strrchr(title, '.');
  if (extension_dot != nullptr && extension_dot > title) {
    *extension_dot = '\0';
  }

  util_safe_strcpy(drive_ptr->image_name, title, harddisk_status_name_max);
}

// Why: Orchestrates the safe ejection of a hard disk image.
auto eject_harddisk_from_drive(HarddiskPeripheral_t* peripheral_ptr,
                               int drive_index) -> void {
  if (peripheral_ptr == nullptr || !is_drive_valid(drive_index)) {
    return;
  }
  auto& drive = peripheral_ptr->drives.at(static_cast<size_t>(drive_index));

  if (drive.driver != nullptr && drive.driver_instance != nullptr &&
      drive.driver->close != nullptr) {
    drive.driver->close(drive.driver_instance);
  }

  drive = HarddiskDrive_t();
}

// Why: Master orchestration for inserting a hard disk image. Handles ejection
// of existing disks, loader dispatch, metadata parsing, and UI notification.
auto insert_harddisk_into_drive(HarddiskPeripheral_t* peripheral_ptr,
                                int drive_index, const char* path,
                                bool write_protected) -> HarddiskError_e {
  if (peripheral_ptr == nullptr || !is_drive_valid(drive_index) ||
      path == nullptr) {
    return harddisk_err_io;
  }
  auto& drive = peripheral_ptr->drives.at(static_cast<size_t>(drive_index));

  if (drive.is_loaded) {
    eject_harddisk_from_drive(peripheral_ptr, drive_index);
  }

  drive.user_write_protected = write_protected;
  const HarddiskError_e error = harddisk_loader_open(
      path, &drive.os_readonly, &drive.driver, &drive.driver_instance);

  drive.last_error = error;

  if (error != harddisk_err_none) {
    if (peripheral_ptr->host != nullptr) {
      peripheral_ptr->host->NotifyStatusChanged(
          static_cast<int>(peripheral_ptr->slot));
    }
    return error;
  }

  drive.is_loaded = true;
  update_image_metadata(&drive, path);

  if (peripheral_ptr->host != nullptr) {
    const char* key = (drive_index == harddisk_drive_0) ? "Harddisk Image 1"
                                                        : "Harddisk Image 2";
    peripheral_ptr->host->SetConfig("Preferences", key, path);
    peripheral_ptr->host->NotifyStatusChanged(
        static_cast<int>(peripheral_ptr->slot));
  }

  return harddisk_err_none;
}

// Why: Specialized sub-handler for the SmartPort Command execution strobe.
// Maps high-level block I/O commands to the active driver instance.
auto execute_harddisk_io_command(HarddiskPeripheral_t* peripheral_ptr,
                                 HarddiskDrive_t& active_drive) -> uint8_t {
  if (peripheral_ptr == nullptr) {
    return status::unknown_error;
  }

  if (!active_drive.is_loaded) {
    peripheral_ptr->activity_status = harddisk_status_off;
    active_drive.error_code = 1;
    return status::unknown_error;
  }

  uint8_t io_status = status::ok;
  switch (static_cast<HarddiskIOCommand_e>(peripheral_ptr->command_reg)) {
    case hd_io_cmd_status:
      if (active_drive.driver->get_total_blocks(active_drive.driver_instance) ==
          0) {
        active_drive.error_code = 1;
        io_status = status::io_error;
      }
      break;

    case hd_io_cmd_read:
      peripheral_ptr->activity_status = harddisk_status_read;
      active_drive.buffer_ptr = 0;
      if (active_drive.driver != nullptr &&
          active_drive.driver->read_block != nullptr &&
          active_drive.driver->read_block(
              active_drive.driver_instance, active_drive.disk_block,
              active_drive.data_buffer.data()) == harddisk_err_none) {
        active_drive.error_code = 0;
        io_status = status::ok;
      } else {
        active_drive.error_code = 1;
        io_status = status::io_error;
      }
      break;

    case hd_io_cmd_write:
      peripheral_ptr->activity_status = harddisk_status_write;
      active_drive.buffer_ptr = 0;
      // Safety: Verify memory address before copying
      if (static_cast<uint32_t>(active_drive.memory_address) +
              physical::block_size >
          0x10000) {
        active_drive.error_code = 1;
        io_status = status::io_error;
        break;
      }
      std::copy_n(mem + active_drive.memory_address, physical::block_size,
                  active_drive.data_buffer.data());
      if (active_drive.driver != nullptr &&
          active_drive.driver->write_block != nullptr &&
          active_drive.driver->write_block(
              active_drive.driver_instance, active_drive.disk_block,
              active_drive.data_buffer.data()) == harddisk_err_none) {
        active_drive.error_code = 0;
        io_status = status::ok;
      } else {
        active_drive.error_code = 1;
        io_status = status::io_error;
      }
      break;

    case hd_io_cmd_format:
      peripheral_ptr->activity_status = harddisk_status_write;
      break;

    default:
      break;
  }
  return io_status;
}

auto harddisk_io_handler(void* instance_handle, uint16_t program_counter,
                         uint16_t address, uint8_t is_write, uint8_t data_value,
                         uint32_t remaining_cycles) -> uint8_t {
  if (instance_handle == nullptr) {
    return status::unknown_error;
  }
  auto* peripheral_ptr = static_cast<HarddiskPeripheral_t*>(instance_handle);

  uint8_t result = status::ok;
  const uint16_t addr = address & regs::io_addr_hi_mask;

  if (!peripheral_ptr->rom_active || !peripheral_ptr->is_enabled) {
    return io_null(program_counter, addr, is_write, data_value,
                   remaining_cycles);
  }

  const size_t drive_idx = static_cast<size_t>(
      (peripheral_ptr->unit_num >> physical::unit_num_drive_bit) & 1);
  auto& active_drive = peripheral_ptr->drives.at(drive_idx);

  if (is_write == 0) {  // Read
    switch (addr) {
      case regs::cmd_exec:
        result = execute_harddisk_io_command(peripheral_ptr, active_drive);
        break;

      case regs::error:
        result = active_drive.error_code;
        break;
      case regs::command:
        result = peripheral_ptr->command_reg;
        break;
      case regs::unit:
        result = peripheral_ptr->unit_num;
        break;
      case regs::memblock_lo:
        result = static_cast<uint8_t>(active_drive.memory_address & 0xFF);
        break;
      case regs::memblock_hi:
        result = static_cast<uint8_t>(active_drive.memory_address >> 8);
        break;
      case regs::diskblock_lo:
        result = static_cast<uint8_t>(active_drive.disk_block & 0xFF);
        break;
      case regs::diskblock_hi:
        result = static_cast<uint8_t>(active_drive.disk_block >> 8);
        break;
      case regs::buffer:
        if (active_drive.buffer_ptr < active_drive.data_buffer.size()) {
          result = active_drive.data_buffer.at(active_drive.buffer_ptr);
          active_drive.buffer_ptr++;
        } else {
          result = 0x00;
        }
        break;
      default:
        return io_null(program_counter, addr, is_write, data_value,
                       remaining_cycles);
    }
  } else {  // Write
    switch (addr) {
      case regs::command:
        peripheral_ptr->command_reg = data_value;
        break;
      case regs::unit:
        peripheral_ptr->unit_num = data_value;
        break;
      case regs::memblock_lo:
        active_drive.memory_address = static_cast<uint16_t>(
            (active_drive.memory_address & 0xFF00) | data_value);
        break;
      case regs::memblock_hi:
        active_drive.memory_address = static_cast<uint16_t>(
            (active_drive.memory_address & 0x00FF) | (data_value << 8));
        break;
      case regs::diskblock_lo:
        active_drive.disk_block = static_cast<uint16_t>(
            (active_drive.disk_block & 0xFF00) | data_value);
        break;
      case regs::diskblock_hi:
        active_drive.disk_block = static_cast<uint16_t>(
            (active_drive.disk_block & 0x00FF) | (data_value << 8));
        break;
      case regs::buffer:
        if (active_drive.buffer_ptr < active_drive.data_buffer.size()) {
          active_drive.data_buffer.at(active_drive.buffer_ptr) = data_value;
          active_drive.buffer_ptr++;
        }
        break;
      default:
        return io_null(program_counter, addr, is_write, data_value,
                       remaining_cycles);
    }
  }

  if (peripheral_ptr->host != nullptr) {
    peripheral_ptr->host->NotifyStatusChanged(
        static_cast<int>(peripheral_ptr->slot));
  }
  return result;
}

// --- ABI Implementation ---

auto harddisk_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }
  auto peripheral_ptr =
      std::unique_ptr<HarddiskPeripheral_t>(new HarddiskPeripheral_t());
  peripheral_ptr->host = host;
  peripheral_ptr->slot = static_cast<uint32_t>(slot);
  peripheral_ptr->is_enabled = true;

  harddisk_loader_init();

  std::array<uint8_t, physical::rom_size> slot_rom{};
  std::copy(harddisk_rom.begin(), harddisk_rom.end(), slot_rom.begin());
  host->RegisterCxROM(slot, slot_rom.data());
  peripheral_ptr->rom_active = true;

  for (uint16_t addr = regs::io_addr_base; addr <= regs::io_addr_end; ++addr) {
    host->RegisterDirectIO(peripheral_ptr.get(), addr, harddisk_io_handler,
                           harddisk_io_handler);
  }

  char path[physical::path_max];
  if (host->GetConfig("Preferences", "Harddisk Image 1", path, sizeof(path)) &&
      path[0] != '\0') {
    insert_harddisk_into_drive(peripheral_ptr.get(), harddisk_drive_0, path,
                               false);
  }
  if (host->GetConfig("Preferences", "Harddisk Image 2", path, sizeof(path)) &&
      path[0] != '\0') {
    insert_harddisk_into_drive(peripheral_ptr.get(), harddisk_drive_1, path,
                               false);
  }

  return peripheral_ptr.release();
}

auto harddisk_abi_reset(void* instance_handle) -> void {
  if (instance_handle == nullptr) {
    return;
  }
  auto* peripheral_ptr = static_cast<HarddiskPeripheral_t*>(instance_handle);
  peripheral_ptr->activity_status = harddisk_status_off;
}

auto harddisk_abi_shutdown(void* instance_handle) -> void {
  if (instance_handle == nullptr) {
    return;
  }
  auto* peripheral_ptr = static_cast<HarddiskPeripheral_t*>(instance_handle);
  for (int i = 0; i < harddisk_drive_count; ++i) {
    eject_harddisk_from_drive(peripheral_ptr, i);
  }
  harddisk_loader_shutdown();
  const std::unique_ptr<HarddiskPeripheral_t> cleanup_guard(peripheral_ptr);
}

auto harddisk_abi_command(void* instance_handle, uint32_t cmd_id,
                          const void* payload, size_t payload_size)
    -> PeripheralStatus_t {
  if (instance_handle == nullptr) {
    return peripheral_error;
  }
  auto* peripheral_ptr = static_cast<HarddiskPeripheral_t*>(instance_handle);

  switch (static_cast<HarddiskCmd_e>(cmd_id)) {
    case harddisk_cmd_insert: {
      if (payload == nullptr || payload_size < sizeof(HarddiskInsertCmd_t)) {
        return peripheral_error;
      }
      const auto* cmd_ptr = static_cast<const HarddiskInsertCmd_t*>(payload);
      if (!is_drive_valid(cmd_ptr->drive)) {
        return peripheral_error;
      }
      insert_harddisk_into_drive(peripheral_ptr, cmd_ptr->drive, cmd_ptr->path,
                                 cmd_ptr->write_protected != 0);
      return peripheral_ok;
    }
    case harddisk_cmd_eject: {
      if (payload == nullptr || payload_size < sizeof(HarddiskEjectCmd_t)) {
        return peripheral_error;
      }
      const auto* cmd_ptr = static_cast<const HarddiskEjectCmd_t*>(payload);
      if (!is_drive_valid(cmd_ptr->drive)) {
        return peripheral_error;
      }
      eject_harddisk_from_drive(peripheral_ptr, cmd_ptr->drive);
      if (peripheral_ptr->host != nullptr) {
        const char* key = (cmd_ptr->drive == harddisk_drive_0)
                              ? "Harddisk Image 1"
                              : "Harddisk Image 2";
        peripheral_ptr->host->SetConfig("Preferences", key, "");
        peripheral_ptr->host->NotifyStatusChanged(
            static_cast<int>(peripheral_ptr->slot));
      }
      return peripheral_ok;
    }
    case harddisk_cmd_set_protect: {
      if (payload == nullptr ||
          payload_size < sizeof(HarddiskSetProtectCmd_t)) {
        return peripheral_error;
      }
      const auto* cmd_ptr =
          static_cast<const HarddiskSetProtectCmd_t*>(payload);
      if (!is_drive_valid(cmd_ptr->drive)) {
        return peripheral_error;
      }
      peripheral_ptr->drives.at(static_cast<size_t>(cmd_ptr->drive))
          .user_write_protected = (cmd_ptr->write_protected != 0);
      if (peripheral_ptr->host != nullptr) {
        peripheral_ptr->host->NotifyStatusChanged(
            static_cast<int>(peripheral_ptr->slot));
      }
      return peripheral_ok;
    }
    case harddisk_cmd_reset_status: {
      peripheral_ptr->activity_status = harddisk_status_off;
      return peripheral_ok;
    }
    default:
      break;
  }
  return peripheral_incompatible;
}

auto harddisk_abi_query(void* instance_handle, uint32_t cmd_id, void* data,
                        size_t* size) -> PeripheralStatus_t {
  if (instance_handle == nullptr || size == nullptr) {
    return peripheral_error;
  }

  if (static_cast<HarddiskCmd_e>(cmd_id) ==
      harddisk_cmd_get_supported_extensions) {
    if (data == nullptr || *size == 0) {
      *size = 256;
      return peripheral_ok;
    }
    harddisk_loader_get_supported_extensions(static_cast<char*>(data), *size);
    *size = strlen(static_cast<const char*>(data)) + 1;
    return peripheral_ok;
  }

  if (static_cast<HarddiskCmd_e>(cmd_id) != harddisk_cmd_get_status) {
    return peripheral_incompatible;
  }

  constexpr size_t required_size = sizeof(HarddiskStatus_t);
  if (data == nullptr || *size < required_size) {
    *size = required_size;
    return peripheral_error;
  }

  auto* peripheral_ptr = static_cast<HarddiskPeripheral_t*>(instance_handle);
  auto* status_ptr = static_cast<HarddiskStatus_t*>(data);
  std::fill_n(reinterpret_cast<uint8_t*>(status_ptr), required_size, 0);

  for (int i = 0; i < harddisk_drive_count; ++i) {
    auto& d = peripheral_ptr->drives.at(static_cast<size_t>(i));
    const bool is_protected = (d.user_write_protected || d.os_readonly);

    if (i == harddisk_drive_0) {
      status_ptr->drive0_last_error = static_cast<int32_t>(d.last_error);
      status_ptr->drive0_loaded = d.is_loaded ? 1 : 0;
      status_ptr->drive0_write_protected = is_protected ? 1 : 0;
      util_safe_strcpy(status_ptr->drive0_name, d.image_name,
                       harddisk_status_name_max);
      util_safe_strcpy(status_ptr->drive0_full_path, d.full_path,
                       harddisk_status_path_max);
    } else {
      status_ptr->drive1_last_error = static_cast<int32_t>(d.last_error);
      status_ptr->drive1_loaded = d.is_loaded ? 1 : 0;
      status_ptr->drive1_write_protected = is_protected ? 1 : 0;
      util_safe_strcpy(status_ptr->drive1_name, d.image_name,
                       harddisk_status_name_max);
      util_safe_strcpy(status_ptr->drive1_full_path, d.full_path,
                       harddisk_status_path_max);
    }
  }

  status_ptr->activity_status =
      static_cast<uint8_t>(peripheral_ptr->activity_status);
  *size = required_size;

  return peripheral_ok;
}
}  // namespace

static Peripheral_t g_harddisk_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.harddisk",
    .name = "Harddisk",
    .description = "SmartPort hard disk controller emulation",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = physical::default_slot,
    .init = harddisk_abi_init,
    .reset = harddisk_abi_reset,
    .shutdown = harddisk_abi_shutdown,
    .think = nullptr,
    .on_vblank = nullptr,
    .save_state = nullptr,
    .load_state = nullptr,
    .command = harddisk_abi_command,
    .query = harddisk_abi_query};

extern "C" auto harddisk_get_descriptor() -> Peripheral_t* {
  return &g_harddisk_peripheral;
}

PERIPHERAL_REGISTER(g_harddisk_peripheral)
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-owning-memory, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-type-const-cast, bugprone-easily-swappable-parameters, modernize-make-unique, google-runtime-int)
