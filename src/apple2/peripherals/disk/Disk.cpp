// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//             cppcoreguidelines-owning-memory,
//             cppcoreguidelines-pro-bounds-pointer-arithmetic,
//             cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays,
//             cppcoreguidelines-pro-bounds-constant-array-index,
//             cppcoreguidelines-pro-type-reinterpret-cast,
//             cppcoreguidelines-pro-type-const-cast,
//             bugprone-easily-swappable-parameters,
//             modernize-make-unique)
// Justification: This module implements low-level Disk II hardware emulation
// using procedural C-style patterns for performance and ABI compatibility.
// Pointer arithmetic and C-style arrays are required for bitstream manipulation
// and save-state structure stability. easily-swappable-parameters is mandated
// by the project-wide Peripheral ABI signatures. modernize-make-unique is
// suppressed to maintain C++11 compatibility.

#include "apple2/peripherals/disk/Disk.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "apple2/Memory.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "apple2/peripherals/disk/formats/DoDriver.h"
#include "apple2/peripherals/disk/formats/IieDriver.h"
#include "apple2/peripherals/disk/formats/Nb2Driver.h"
#include "apple2/peripherals/disk/formats/NibDriver.h"
#include "apple2/peripherals/disk/formats/PoDriver.h"
#include "apple2/peripherals/disk/formats/Woz2Driver.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Peripheral.h"
#include "core/Util_Text.h"

namespace {

constexpr uint32_t spinup_ticks = 20000;
constexpr uint32_t write_light_ticks = 20000;

constexpr uint8_t latch_bit = 0x80;
constexpr uint8_t floating_bus = 0xFF;

constexpr uint8_t io_addr_mask = 0x0F;
constexpr uint8_t io_addr_hi_mask = 0xFF;

constexpr uint8_t io_stepper_0 = 0x0;
constexpr uint8_t io_stepper_1 = 0x1;
constexpr uint8_t io_stepper_2 = 0x2;
constexpr uint8_t io_stepper_3 = 0x3;
constexpr uint8_t io_stepper_4 = 0x4;
constexpr uint8_t io_stepper_5 = 0x5;
constexpr uint8_t io_stepper_6 = 0x6;
constexpr uint8_t io_stepper_7 = 0x7;
constexpr uint8_t io_motor_off = 0x8;
constexpr uint8_t io_motor_on = 0x9;
constexpr uint8_t io_drive_1 = 0xA;
constexpr uint8_t io_drive_2 = 0xB;
constexpr uint8_t io_read_write = 0xC;
constexpr uint8_t io_shift_reg = 0xD;
constexpr uint8_t io_read_mode = 0xE;
constexpr uint8_t io_write_mode = 0xF;
constexpr uint8_t io_stepper_alt = 0xE0;

constexpr int disk_state_version = 1;

struct Disk_t {
  char full_path[max_disk_full_path_len + 1];
  char image_name[max_disk_image_name_len + 1];
  int track = 0;
  int phase = 0;
  uint32_t current_byte_pos = 0;
  bool user_write_protected = false;
  bool is_os_read_only = false;
  bool is_data_loaded = false;
  bool is_dirty = false;
  uint32_t spinning_ticks = 0;
  uint32_t write_light_ticks = 0;
  int nibble_count = 0;
  std::unique_ptr<uint8_t[]> track_buffer;
  DiskFormatDriver_t* driver = nullptr;
  void* driver_instance = nullptr;
  DiskError_e last_error = disk_err_none;

  Disk_t() = default;
  ~Disk_t() = default;

  Disk_t(const Disk_t&) = delete;
  auto operator=(const Disk_t&) -> Disk_t& = delete;
  Disk_t(Disk_t&&) = default;
  auto operator=(Disk_t&&) -> Disk_t& = default;
};

struct DiskPeripheral_t {
  std::array<Disk_t, disk_drive_count> drives{};
  uint16_t active_drive_index = 0;

  uint8_t io_latch = 0;
  uint16_t stepper_phase_mask = 0;
  bool is_motor_on = false;
  bool is_write_mode = false;

  bool was_accessed_this_tick = false;
  uint32_t spin_cycle_accumulator = 0;
  uint32_t rotation_cycle_accumulator = 0;

  bool is_speed_enhanced = true;
  HostInterface_t* host = nullptr;
  int slot = 0;

  DiskPeripheral_t() = default;
};

const std::array<uint8_t, 256> disk2_rom = {
    {0xA2, 0x20, 0xA0, 0x00, 0xA2, 0x03, 0x86, 0x3C, 0x8A, 0x0A, 0x24, 0x3C,
     0xF0, 0x10, 0x05, 0x3C, 0x49, 0xFF, 0x29, 0x7E, 0xB0, 0x08, 0x4A, 0xD0,
     0xFB, 0x98, 0x9D, 0x56, 0x03, 0xC8, 0xE8, 0x10, 0xE5, 0x20, 0x58, 0xFF,
     0xBA, 0xBD, 0x00, 0x01, 0x0A, 0x0A, 0x0A, 0x0A, 0x85, 0x2B, 0xAA, 0xBD,
     0x8E, 0xC0, 0xBD, 0x8C, 0xC0, 0xBD, 0x8A, 0xC0, 0xBD, 0x89, 0xC0, 0xA0,
     0x50, 0xBD, 0x80, 0xC0, 0x98, 0x29, 0x03, 0x0A, 0x05, 0x2B, 0xAA, 0xBD,
     0x81, 0xC0, 0xA9, 0x56, 0x20, 0xA8, 0xFC, 0x88, 0x10, 0xEB, 0x85, 0x26,
     0x85, 0x3D, 0x85, 0x41, 0xA9, 0x08, 0x85, 0x27, 0x18, 0x08, 0xBD, 0x8C,
     0xC0, 0x10, 0xFB, 0x49, 0xD5, 0xD0, 0xF7, 0xBD, 0x8C, 0xC0, 0x10, 0xFB,
     0xC9, 0xAA, 0xD0, 0xF3, 0xEA, 0xBD, 0x8C, 0xC0, 0x10, 0xFB, 0xC9, 0x96,
     0xF0, 0x09, 0x28, 0x90, 0xDF, 0x49, 0xAD, 0xF0, 0x25, 0xD0, 0xD9, 0xA0,
     0x03, 0x85, 0x40, 0xBD, 0x8C, 0xC0, 0x10, 0xFB, 0x2A, 0x85, 0x3C, 0xBD,
     0x8C, 0xC0, 0x10, 0xFB, 0x25, 0x3C, 0x88, 0xD0, 0xEC, 0x28, 0xC5, 0x3D,
     0xD0, 0xBE, 0xA5, 0x40, 0xC5, 0x41, 0xD0, 0xB8, 0xB0, 0xB7, 0xA0, 0x56,
     0x84, 0x3C, 0xBC, 0x8C, 0xC0, 0x10, 0xFB, 0x59, 0xD6, 0x02, 0xA4, 0x3C,
     0x88, 0x99, 0x00, 0x03, 0xD0, 0xEE, 0x84, 0x3C, 0xBC, 0x8C, 0xC0, 0x10,
     0xFB, 0x59, 0xD6, 0x02, 0xA4, 0x3C, 0x91, 0x26, 0xC8, 0xD0, 0xEF, 0xBC,
     0x8C, 0xC0, 0x10, 0xFB, 0x59, 0xD6, 0x02, 0xD0, 0x87, 0xA0, 0x00, 0xA2,
     0x56, 0xCA, 0x30, 0xFB, 0xB1, 0x26, 0x5E, 0x00, 0x03, 0x2A, 0x5E, 0x00,
     0x03, 0x2A, 0x91, 0x26, 0xC8, 0xD0, 0xEE, 0xE6, 0x27, 0xE6, 0x3D, 0xA5,
     0x3D, 0xCD, 0x00, 0x08, 0xA6, 0x2B, 0x90, 0xDB, 0x4C, 0x01, 0x08, 0x00,
     0x00, 0x00, 0x00, 0x00}};

// Why: Maintained for binary compatibility with legacy save-states.
// Plan to remove in a future version in favor of a modern serialization format.
#pragma pack(push, 1)
struct DiskStateHeader_t {
  uint32_t version;
  uint32_t size;
};

struct DiskDriveState_t {
  char full_path[max_disk_full_path_len + 1];
  int32_t track;
  int32_t phase;
  int32_t current_byte_pos;
  uint8_t user_write_protected;
  uint8_t is_os_read_only;
  uint8_t is_data_loaded;
  uint8_t is_dirty;
  uint32_t spinning_ticks;
  uint32_t write_light_ticks;
  int32_t nibble_count;
  uint8_t track_buffer[nibbles_per_track];
};

struct DiskSavedState_t {
  DiskStateHeader_t header;
  DiskDriveState_t drives[disk_drive_count];
  uint16_t stepper_phase_mask;
  uint16_t active_drive_index;
  uint8_t was_accessed_this_tick;
  uint8_t is_speed_enhanced;
  uint8_t io_latch;
  uint8_t is_motor_on;
  uint8_t is_write_mode;
};
#pragma pack(pop)

auto is_drive_valid(int drive_index) -> bool {
  return (drive_index >= 0 && drive_index < disk_drive_count);
}

// Why: Implements multi-layered write protection:
// 1. User manual toggle (the "notch" on a physical disk).
// 2. OS file system permissions.
// 3. Hardware format capabilities (some formats are read-only).
auto is_disk_write_protected(DiskPeripheral_t* disk_peripheral, int drive_index)
    -> bool {
  if (!is_drive_valid(drive_index)) {
    return false;
  }
  auto* disk_ptr =
      &disk_peripheral->drives.at(static_cast<size_t>(drive_index));

  if (disk_ptr->user_write_protected || disk_ptr->is_os_read_only) {
    return true;
  }

  if (disk_ptr->driver != nullptr) {
    const bool can_write =
        (disk_ptr->driver->capabilities & disk_driver_cap_write) != 0;
    if (!can_write) {
      return true;
    }
    if (disk_ptr->driver->is_write_protected != nullptr) {
      return disk_ptr->driver->is_write_protected(disk_ptr->driver_instance);
    }
  }

  return false;
}

auto write_track_to_driver(DiskPeripheral_t* disk_peripheral, int drive_index)
    -> void {
  if (!is_drive_valid(drive_index)) {
    return;
  }
  auto* disk_ptr =
      &disk_peripheral->drives.at(static_cast<size_t>(drive_index));

  if (disk_ptr->track >= tracks_per_disk) {
    return;
  }

  if (is_disk_write_protected(disk_peripheral, drive_index)) {
    return;
  }

  if (disk_ptr->track_buffer != nullptr && disk_ptr->driver != nullptr &&
      disk_ptr->driver->write_track != nullptr) {
    disk_ptr->driver->write_track(disk_ptr->driver_instance, disk_ptr->track,
                                  disk_ptr->phase, disk_ptr->track_buffer.get(),
                                  disk_ptr->nibble_count);
  }

  disk_ptr->is_dirty = false;
}

auto read_track_from_driver(DiskPeripheral_t* disk_peripheral, int drive_index)
    -> void {
  if (!is_drive_valid(drive_index)) {
    return;
  }

  auto* disk_ptr =
      &disk_peripheral->drives.at(static_cast<size_t>(drive_index));

  if (disk_ptr->track >= tracks_per_disk) {
    disk_ptr->is_data_loaded = false;
    return;
  }

  if (disk_ptr->track_buffer == nullptr) {
    disk_ptr->track_buffer.reset(new uint8_t[nibbles_per_track]());
  }

  if (disk_ptr->track_buffer != nullptr && disk_ptr->driver != nullptr &&
      disk_ptr->driver->read_track != nullptr) {
    disk_ptr->driver->read_track(disk_ptr->driver_instance, disk_ptr->track,
                                 disk_ptr->phase, disk_ptr->track_buffer.get(),
                                 &disk_ptr->nibble_count);

    disk_ptr->current_byte_pos = 0;
    disk_ptr->is_data_loaded = (disk_ptr->nibble_count != 0);
  }
}

auto eject_disk_from_drive(DiskPeripheral_t* disk_peripheral, int drive_index)
    -> void {
  if (!is_drive_valid(drive_index)) {
    return;
  }
  auto* disk_ptr =
      &disk_peripheral->drives.at(static_cast<size_t>(drive_index));

  if (disk_ptr->driver != nullptr) {
    if (disk_ptr->track_buffer != nullptr && disk_ptr->is_dirty) {
      write_track_to_driver(disk_peripheral, drive_index);
    }

    if (disk_ptr->driver->close != nullptr) {
      disk_ptr->driver->close(disk_ptr->driver_instance);
    }

    if (disk_peripheral->host != nullptr) {
      const char* key =
          (drive_index == 0) ? REGVALUE_DISK_IMAGE1 : REGVALUE_DISK_IMAGE2;
      disk_peripheral->host->SetConfig("Slots", key, "");
      disk_peripheral->host->NotifyStatusChanged(disk_peripheral->slot);
    }
  }

  *disk_ptr = Disk_t();
}

auto update_disk_metadata(Disk_t* disk_ptr, const char* image_path) -> void {
  if (disk_ptr == nullptr || image_path == nullptr) {
    return;
  }

  char image_title[max_disk_full_path_len + 1];
  const char* start_pos = image_path;

  const char* last_sep = strrchr(start_pos, '/');
  if (last_sep != nullptr) {
    start_pos = last_sep + 1;
  }
  Util_SafeStrCpy(image_title, start_pos, max_disk_full_path_len);

  bool found_lower = false;
  int title_length = 0;
  while (image_title[title_length] != '\0' && !found_lower) {
    if (is_char_lower(image_title[title_length])) {
      found_lower = true;
    } else {
      title_length++;
    }
  }

  constexpr int min_title_len_for_format = 3;
  if (!found_lower && title_length >= min_title_len_for_format) {
    for (char* p = image_title + 1; *p != '\0'; ++p) {
      *p = static_cast<char>(tolower(static_cast<uint8_t>(*p)));
    }
  }

  Util_SafeStrCpy(disk_ptr->full_path, image_path, max_disk_full_path_len);

  char* extension_dot = strrchr(image_title, '.');
  if (extension_dot != nullptr && extension_dot > image_title) {
    *extension_dot = '\0';
  }

  Util_SafeStrCpy(disk_ptr->image_name, image_title, max_disk_image_name_len);
}

auto sync_drive_motor_state(DiskPeripheral_t* disk_peripheral) -> void {
  if (disk_peripheral == nullptr) {
    return;
  }
  auto* disk_ptr = &disk_peripheral->drives.at(
      static_cast<size_t>(disk_peripheral->active_drive_index));
  const bool was_spinning = (disk_ptr->spinning_ticks > 0);
  if (disk_peripheral->is_motor_on) {
    disk_ptr->spinning_ticks = spinup_ticks;
  }
  const bool now_spinning = (disk_ptr->spinning_ticks > 0);

  if (was_spinning != now_spinning) {
    if (disk_peripheral->host != nullptr) {
      disk_peripheral->host->NotifyActivityChanged(disk_peripheral->slot,
                                                   now_spinning);
      disk_peripheral->host->NotifyStatusChanged(disk_peripheral->slot);
    }
  }
}

auto insert_disk_into_drive(DiskPeripheral_t* disk_peripheral, int drive_index,
                            const char* image_path, bool write_protected,
                            bool create_if_necessary) -> DiskError_e {
  if (!is_drive_valid(drive_index)) {
    return disk_err_io;
  }
  auto* disk_ptr =
      &disk_peripheral->drives.at(static_cast<size_t>(drive_index));

  if (disk_ptr->driver != nullptr) {
    eject_disk_from_drive(disk_peripheral, drive_index);
  }
  *disk_ptr = Disk_t();

  disk_ptr->user_write_protected = write_protected;
  const DiskError_e error =
      disk_loader_open(image_path, create_if_necessary,
                       static_cast<uint8_t>(disk_peripheral->is_speed_enhanced),
                       &disk_ptr->is_os_read_only,
                       const_cast<DiskFormatDriver_t**>(&disk_ptr->driver),
                       &disk_ptr->driver_instance);

  disk_ptr->last_error = error;

  if (error == disk_err_none) {
    update_disk_metadata(disk_ptr, image_path);

    char full_title[max_disk_image_name_len + 64];
    snprintf(full_title, sizeof(full_title), "%s - %s", g_pAppTitle,
             disk_ptr->image_name);
    linapple_update_title(full_title);

    if (disk_peripheral->host != nullptr) {
      const char* key =
          (drive_index == 0) ? REGVALUE_DISK_IMAGE1 : REGVALUE_DISK_IMAGE2;
      disk_peripheral->host->SetConfig("Slots", key, image_path);
    }
  }

  if (disk_peripheral->host != nullptr) {
    disk_peripheral->host->NotifyStatusChanged(disk_peripheral->slot);
  }

  return error;
}

auto sync_driver_options(DiskPeripheral_t* disk_peripheral) -> void {
  if (disk_peripheral == nullptr) {
    return;
  }

  for (int i = 0; i < disk_drive_count; ++i) {
    auto* disk_ptr = &disk_peripheral->drives.at(static_cast<size_t>(i));
    if (disk_ptr->driver != nullptr && disk_ptr->driver->command != nullptr) {
      const uint8_t enhanced_flag = disk_peripheral->is_speed_enhanced ? 1 : 0;
      disk_ptr->driver->command(disk_ptr->driver_instance,
                                disk_driver_cmd_set_enhanced_speed,
                                &enhanced_flag, sizeof(uint8_t));
    }
  }
}

auto disk_io_control_motor(void* instance, uint16_t, uint16_t memory_address,
                           uint8_t, uint8_t, uint32_t) -> uint8_t {
  if (instance == nullptr) {
    return MemReturnRandomData(floating_bus);
  }

  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);

  disk_peripheral->is_motor_on = (memory_address & 0x01) != 0;

  sync_drive_motor_state(disk_peripheral);

  return MemReturnRandomData(floating_bus);
}

// Why: Emulates the physical movement of the disk head via the stepper motor.
// Handles phase-to-track mapping and ensures dirty tracks are flushed to the
// format driver before the head leaves the current cylinder.
auto step_drive_head(DiskPeripheral_t* disk_peripheral, int phase_delta)
    -> void {
  if (disk_peripheral == nullptr) {
    return;
  }

  auto* disk_ptr = &disk_peripheral->drives.at(
      static_cast<size_t>(disk_peripheral->active_drive_index));
  const int old_phase = disk_ptr->phase;

  disk_ptr->phase =
      std::max(0, std::min(max_disk_phases - 1, disk_ptr->phase + phase_delta));
  disk_ptr->track =
      std::min(tracks_per_disk - 1, disk_ptr->phase / phases_per_track);

  if (disk_ptr->phase != old_phase) {
    if (disk_ptr->track_buffer != nullptr && disk_ptr->is_dirty) {
      write_track_to_driver(disk_peripheral,
                            disk_peripheral->active_drive_index);
    }
    disk_ptr->is_data_loaded = false;
  }
}

// Why: Emulates the physical magnetic stepper motor phases ($C0n0-$C0n7).
// The 6502 code manually energizes/de-energizes four physical magnets
// to 'pull' the head to the next or previous phase.
auto disk_io_control_stepper(void* instance, uint16_t, uint16_t memory_address,
                             uint8_t, uint8_t, uint32_t) -> uint8_t {
  if (instance == nullptr) {
    return MemReturnRandomData(floating_bus);
  }

  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);
  auto* disk_ptr = &disk_peripheral->drives.at(
      static_cast<size_t>(disk_peripheral->active_drive_index));

  const int strobe_phase = (memory_address >> 1) & 0x03;
  const uint16_t strobe_bit = static_cast<uint16_t>(1 << strobe_phase);

  if ((memory_address & 0x01) != 0) {
    disk_peripheral->stepper_phase_mask |= strobe_bit;
  } else {
    disk_peripheral->stepper_phase_mask &= static_cast<uint16_t>(~strobe_bit);
  }

  int step_delta = 0;
  if ((disk_peripheral->stepper_phase_mask &
       (1 << ((disk_ptr->phase + 1) & 3))) != 0) {
    step_delta += 1;
  }
  if ((disk_peripheral->stepper_phase_mask &
       (1 << ((disk_ptr->phase + 3) & 3))) != 0) {
    step_delta -= 1;
  }

  if (step_delta != 0) {
    step_drive_head(disk_peripheral, step_delta);
  }

  return (memory_address == io_stepper_alt) ? floating_bus
                                            : MemReturnRandomData(floating_bus);
}

auto disk_io_enable_drive(void* instance, uint16_t, uint16_t memory_address,
                          uint8_t, uint8_t, uint32_t) -> uint8_t {
  if (instance == nullptr) {
    return MemReturnRandomData(floating_bus);
  }

  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);

  disk_peripheral->active_drive_index =
      static_cast<uint16_t>(memory_address & 0x01);

  auto& inactive_drive =
      disk_peripheral->drives.at(!disk_peripheral->active_drive_index);
  inactive_drive.spinning_ticks = 0;
  inactive_drive.write_light_ticks = 0;

  sync_drive_motor_state(disk_peripheral);

  return MemReturnRandomData(floating_bus);
}

auto disk_io_read_write(void* instance, uint16_t, uint16_t, uint8_t, uint8_t,
                        uint32_t) -> uint8_t {
  if (instance == nullptr) {
    return MemReturnRandomData(floating_bus);
  }

  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);
  auto* disk_ptr = &disk_peripheral->drives.at(
      static_cast<size_t>(disk_peripheral->active_drive_index));

  disk_peripheral->was_accessed_this_tick = true;

  if (!disk_ptr->is_data_loaded && disk_ptr->driver != nullptr) {
    read_track_from_driver(disk_peripheral,
                           disk_peripheral->active_drive_index);
  }

  if (!disk_ptr->is_data_loaded) {
    return MemReturnRandomData(floating_bus);
  }

  uint8_t data_byte = 0;
  const bool is_protected = is_disk_write_protected(
      disk_peripheral, disk_peripheral->active_drive_index);

  if (disk_peripheral->is_write_mode) {
    if (!is_protected && (disk_peripheral->io_latch & latch_bit) != 0) {
      disk_ptr->track_buffer[disk_ptr->current_byte_pos] =
          disk_peripheral->io_latch;
      disk_ptr->is_dirty = true;
    }
    data_byte = 0;
  } else {
    data_byte = disk_ptr->track_buffer[disk_ptr->current_byte_pos];
  }

  if (++disk_ptr->current_byte_pos >=
      static_cast<uint32_t>(disk_ptr->nibble_count)) {
    disk_ptr->current_byte_pos = 0;
  }

  return data_byte;
}

auto disk_io_set_latch(void* instance, uint16_t, uint16_t, uint8_t is_write,
                       uint8_t data_value, uint32_t) -> uint8_t {
  if (instance == nullptr) {
    return MemReturnRandomData(floating_bus);
  }

  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);

  if (is_write != 0) {
    disk_peripheral->io_latch = data_value;
  }

  return disk_peripheral->io_latch;
}

auto disk_io_set_read_mode(void* instance, uint16_t, uint16_t, uint8_t, uint8_t,
                           uint32_t) -> uint8_t {
  if (instance == nullptr) {
    return MemReturnRandomData(floating_bus);
  }

  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);

  disk_peripheral->is_write_mode = false;

  const bool is_protected = is_disk_write_protected(
      disk_peripheral, disk_peripheral->active_drive_index);

  return is_protected ? latch_bit : 0x00;
}

auto disk_io_set_write_mode(void* instance, uint16_t, uint16_t, uint8_t,
                            uint8_t, uint32_t) -> uint8_t {
  if (instance == nullptr) {
    return MemReturnRandomData(floating_bus);
  }

  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);

  disk_peripheral->is_write_mode = true;

  auto& active_drive = disk_peripheral->drives.at(
      static_cast<size_t>(disk_peripheral->active_drive_index));

  const bool was_already_writing = (active_drive.write_light_ticks > 0);
  active_drive.write_light_ticks = write_light_ticks;

  if (!was_already_writing && disk_peripheral->host != nullptr) {
    disk_peripheral->host->NotifyStatusChanged(disk_peripheral->slot);
  }

  return MemReturnRandomData(floating_bus);
}

auto update_drive_physics(DiskPeripheral_t* disk_peripheral, Disk_t* disk_ptr,
                          uint32_t spin_ticks, uint32_t rotation_ticks)
    -> void {
  if (disk_ptr->spinning_ticks > 0 && !disk_peripheral->is_motor_on) {
    if (spin_ticks >= disk_ptr->spinning_ticks) {
      disk_ptr->spinning_ticks = 0;
      if (disk_peripheral->host != nullptr) {
        disk_peripheral->host->NotifyActivityChanged(disk_peripheral->slot,
                                                     false);
        disk_peripheral->host->NotifyStatusChanged(disk_peripheral->slot);
      }
    } else {
      disk_ptr->spinning_ticks -= spin_ticks;
    }
  }

  const bool is_active_drive =
      (&disk_peripheral->drives.at(disk_peripheral->active_drive_index) ==
       disk_ptr);

  if (disk_peripheral->is_write_mode && is_active_drive &&
      disk_ptr->spinning_ticks > 0) {
    disk_ptr->write_light_ticks = write_light_ticks;
  } else if (disk_ptr->write_light_ticks > 0) {
    if (spin_ticks >= disk_ptr->write_light_ticks) {
      disk_ptr->write_light_ticks = 0;
      if (disk_peripheral->host != nullptr) {
        disk_peripheral->host->NotifyStatusChanged(disk_peripheral->slot);
      }
    } else {
      disk_ptr->write_light_ticks -= spin_ticks;
    }
  }

  if (disk_peripheral->is_speed_enhanced ||
      disk_peripheral->was_accessed_this_tick ||
      disk_ptr->spinning_ticks == 0) {
    return;
  }

  if (disk_peripheral->host != nullptr) {
    disk_peripheral->host->RequestPreciseTiming();
  }

  disk_ptr->current_byte_pos += rotation_ticks;
  if (disk_ptr->current_byte_pos >=
      static_cast<uint32_t>(disk_ptr->nibble_count)) {
    disk_ptr->current_byte_pos %=
        (disk_ptr->nibble_count != 0
             ? static_cast<uint32_t>(disk_ptr->nibble_count)
             : 1);
  }
}

auto update_physical_disk_state(DiskPeripheral_t* disk_peripheral,
                                uint32_t elapsed_cycles) -> void {
  disk_peripheral->spin_cycle_accumulator += elapsed_cycles;
  const uint32_t spin_ticks = disk_peripheral->spin_cycle_accumulator >> 6;
  disk_peripheral->spin_cycle_accumulator &= 63;

  disk_peripheral->rotation_cycle_accumulator += elapsed_cycles;
  const uint32_t rotation_ticks =
      disk_peripheral->rotation_cycle_accumulator >> 5;
  disk_peripheral->rotation_cycle_accumulator &= 31;

  for (int i = 0; i < disk_drive_count; ++i) {
    update_drive_physics(disk_peripheral,
                         &disk_peripheral->drives.at(static_cast<size_t>(i)),
                         spin_ticks, rotation_ticks);
  }
  disk_peripheral->was_accessed_this_tick = false;
}

auto swap_drives(DiskPeripheral_t* disk_peripheral) -> bool {
  if (disk_peripheral == nullptr) {
    return false;
  }

  if (disk_peripheral->drives.at(0).spinning_ticks > 0 ||
      disk_peripheral->drives.at(1).spinning_ticks > 0) {
    return false;
  }

  std::swap(disk_peripheral->drives.at(0), disk_peripheral->drives.at(1));

  char full_title[max_disk_image_name_len + 64];
  snprintf(full_title, sizeof(full_title), "%s - %s", g_pAppTitle,
           disk_peripheral->drives.at(0).image_name);
  linapple_update_title(full_title);

  if (disk_peripheral->host != nullptr) {
    disk_peripheral->host->NotifyStatusChanged(disk_peripheral->slot);
  }

  return true;
}

auto initialize_peripheral(DiskPeripheral_t* disk_peripheral) -> void {
  if (disk_peripheral == nullptr) {
    return;
  }

  for (int i = 0; i < disk_drive_count; ++i) {
    eject_disk_from_drive(disk_peripheral, i);
  }

  auto* host = disk_peripheral->host;
  const int slot = disk_peripheral->slot;
  const bool is_speed_enhanced = disk_peripheral->is_speed_enhanced;

  *disk_peripheral = DiskPeripheral_t();

  disk_peripheral->host = host;
  disk_peripheral->slot = slot;
  disk_peripheral->is_speed_enhanced = is_speed_enhanced;
}

auto get_peripheral_status(DiskPeripheral_t* disk_peripheral,
                           DiskStatus_t* status) -> void {
  if (disk_peripheral == nullptr || status == nullptr) {
    return;
  }

  {
    auto& drive = disk_peripheral->drives.at(0);
    status->drive0_last_error = static_cast<int32_t>(drive.last_error);
    status->drive0_loaded = (drive.driver != nullptr) ? 1 : 0;
    status->drive0_spinning = (drive.spinning_ticks > 0) ? 1 : 0;
    status->drive0_writing = (drive.write_light_ticks > 0) ? 1 : 0;
    status->drive0_write_protected =
        is_disk_write_protected(disk_peripheral, 0) ? 1 : 0;
    Util_SafeStrCpy(status->drive0_name, drive.image_name,
                    disk_status_name_max);
    Util_SafeStrCpy(status->drive0_full_path, drive.full_path,
                    disk_status_path_max);
  }

  {
    auto& drive = disk_peripheral->drives.at(1);
    status->drive1_last_error = static_cast<int32_t>(drive.last_error);
    status->drive1_loaded = (drive.driver != nullptr) ? 1 : 0;
    status->drive1_spinning = (drive.spinning_ticks > 0) ? 1 : 0;
    status->drive1_writing = (drive.write_light_ticks > 0) ? 1 : 0;
    status->drive1_write_protected =
        is_disk_write_protected(disk_peripheral, 1) ? 1 : 0;
    Util_SafeStrCpy(status->drive1_name, drive.image_name,
                    disk_status_name_max);
    Util_SafeStrCpy(status->drive1_full_path, drive.full_path,
                    disk_status_path_max);
  }
}

auto disk_io_read(void* instance, uint16_t program_counter,
                  uint16_t memory_address, uint8_t is_write, uint8_t,
                  uint32_t remaining_cycles) -> uint8_t {
  if (instance == nullptr || is_write != 0) {
    return MemReturnRandomData(floating_bus);
  }
  const uint16_t addr = memory_address & io_addr_hi_mask;
  const uint8_t is_write_op = 0;

  switch (addr & io_addr_mask) {
    case io_stepper_0:
    case io_stepper_1:
    case io_stepper_2:
    case io_stepper_3:
    case io_stepper_4:
    case io_stepper_5:
    case io_stepper_6:
    case io_stepper_7:
      return disk_io_control_stepper(instance, program_counter, addr,
                                     is_write_op, 0, remaining_cycles);
    case io_motor_off:
    case io_motor_on:
      return disk_io_control_motor(instance, program_counter, addr, is_write_op,
                                   0, remaining_cycles);
    case io_drive_1:
    case io_drive_2:
      return disk_io_enable_drive(instance, program_counter, addr, is_write_op,
                                  0, remaining_cycles);
    case io_read_write:
      return disk_io_read_write(instance, program_counter, addr, is_write_op, 0,
                                remaining_cycles);
    case io_shift_reg:
      return disk_io_set_latch(instance, program_counter, addr, is_write_op, 0,
                               remaining_cycles);
    case io_read_mode:
      return disk_io_set_read_mode(instance, program_counter, addr, is_write_op,
                                   0, remaining_cycles);
    case io_write_mode:
      return disk_io_set_write_mode(instance, program_counter, addr,
                                    is_write_op, 0, remaining_cycles);
    default:
      break;
  }

  return MemReturnRandomData(floating_bus);
}

auto disk_io_write(void* instance, uint16_t program_counter,
                   uint16_t memory_address, uint8_t is_write,
                   uint8_t data_value, uint32_t remaining_cycles) -> uint8_t {
  if (instance == nullptr || is_write == 0) {
    return 0;
  }
  const uint16_t addr = memory_address & io_addr_hi_mask;
  const uint8_t is_write_op = 1;

  switch (addr & io_addr_mask) {
    case io_stepper_0:
    case io_stepper_1:
    case io_stepper_2:
    case io_stepper_3:
    case io_stepper_4:
    case io_stepper_5:
    case io_stepper_6:
    case io_stepper_7:
      return disk_io_control_stepper(instance, program_counter, addr,
                                     is_write_op, data_value, remaining_cycles);
    case io_motor_off:
    case io_motor_on:
      return disk_io_control_motor(instance, program_counter, addr, is_write_op,
                                   data_value, remaining_cycles);
    case io_drive_1:
    case io_drive_2:
      return disk_io_enable_drive(instance, program_counter, addr, is_write_op,
                                  data_value, remaining_cycles);
    case io_read_write:
      return disk_io_read_write(instance, program_counter, addr, is_write_op,
                                data_value, remaining_cycles);
    case io_shift_reg:
      return disk_io_set_latch(instance, program_counter, addr, is_write_op,
                               data_value, remaining_cycles);
    case io_read_mode:
      return disk_io_set_read_mode(instance, program_counter, addr, is_write_op,
                                   data_value, remaining_cycles);
    case io_write_mode:
      return disk_io_set_write_mode(instance, program_counter, addr,
                                    is_write_op, data_value, remaining_cycles);
    default:
      break;
  }

  return 0;
}

auto cmd_handle_insert(DiskPeripheral_t* dp, const void* data, size_t size)
    -> PeripheralStatus_t {
  if (data == nullptr || size < sizeof(DiskInsertCmd_t)) {
    return peripheral_error;
  }
  const auto* c = static_cast<const DiskInsertCmd_t*>(data);
  if (!is_drive_valid(c->drive)) {
    return peripheral_error;
  }
  insert_disk_into_drive(dp, c->drive, c->path, c->write_protected != 0,
                         c->create_if_necessary != 0);
  return peripheral_ok;
}

auto cmd_handle_eject(DiskPeripheral_t* dp, const void* data, size_t size)
    -> PeripheralStatus_t {
  if (data == nullptr || size < sizeof(DiskEjectCmd_t)) {
    return peripheral_error;
  }
  const auto* c = static_cast<const DiskEjectCmd_t*>(data);
  if (!is_drive_valid(c->drive)) {
    return peripheral_error;
  }
  eject_disk_from_drive(dp, c->drive);
  return peripheral_ok;
}

auto cmd_handle_set_protect(DiskPeripheral_t* dp, const void* data, size_t size)
    -> PeripheralStatus_t {
  if (data == nullptr || size < sizeof(DiskSetProtectCmd_t)) {
    return peripheral_error;
  }
  const auto* c = static_cast<const DiskSetProtectCmd_t*>(data);
  if (!is_drive_valid(c->drive)) {
    return peripheral_error;
  }
  dp->drives.at(static_cast<size_t>(c->drive)).user_write_protected =
      (c->write_protected != 0);
  if (dp->host != nullptr) {
    dp->host->NotifyStatusChanged(dp->slot);
  }
  return peripheral_ok;
}

auto cmd_handle_set_speed(DiskPeripheral_t* dp, const void* data, size_t size)
    -> PeripheralStatus_t {
  if (data == nullptr || size < sizeof(uint8_t)) {
    return peripheral_error;
  }
  dp->is_speed_enhanced = (*static_cast<const uint8_t*>(data) != 0);
  sync_driver_options(dp);
  return peripheral_ok;
}

auto disk_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }
  auto dp = std::unique_ptr<DiskPeripheral_t>(new DiskPeripheral_t());
  dp->host = host;
  dp->slot = slot;

  disk_loader_init();
  disk_loader_register(const_cast<DiskFormatDriver_t*>(&g_woz2_driver));
  disk_loader_register(const_cast<DiskFormatDriver_t*>(&g_iie_driver));
  disk_loader_register(const_cast<DiskFormatDriver_t*>(&g_nib_driver));
  disk_loader_register(const_cast<DiskFormatDriver_t*>(&g_nb2_driver));
  disk_loader_register(const_cast<DiskFormatDriver_t*>(&g_do_driver));
  disk_loader_register(const_cast<DiskFormatDriver_t*>(&g_po_driver));

  char enh[16] = {0};
  host->GetConfig("Slots", "Enhance Disk Speed", enh, sizeof(enh));
  dp->is_speed_enhanced = (enh[0] != '0');

  initialize_peripheral(dp.get());

  char p1[path_max_len] = {0};
  char p2[path_max_len] = {0};
  host->GetConfig("Slots", REGVALUE_DISK_IMAGE1, p1, sizeof(p1));
  host->GetConfig("Slots", REGVALUE_DISK_IMAGE2, p2, sizeof(p2));

  if (p1[0] != '\0') {
    insert_disk_into_drive(dp.get(), 0, p1, false, false);
  }
  if (p2[0] != '\0') {
    insert_disk_into_drive(dp.get(), 1, p2, false, false);
  }

  host->RegisterCxROM(slot, const_cast<uint8_t*>(disk2_rom.data()));
  host->RegisterIO(slot, disk_io_read, disk_io_write, nullptr, nullptr);

  return dp.release();
}

auto disk_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  initialize_peripheral(static_cast<DiskPeripheral_t*>(instance));
}

auto disk_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* dp = static_cast<DiskPeripheral_t*>(instance);
  disk_loader_shutdown();
  for (int i = 0; i < disk_drive_count; ++i) {
    eject_disk_from_drive(dp, i);
  }
  const std::unique_ptr<DiskPeripheral_t> cleanup(dp);
}

auto disk_abi_think(void* instance, uint32_t elapsed_cycles) -> void {
  if (instance == nullptr || elapsed_cycles == 0) {
    return;
  }
  update_physical_disk_state(static_cast<DiskPeripheral_t*>(instance),
                             elapsed_cycles);
}

auto disk_abi_command(void* instance, uint32_t cmd, const void* data,
                      size_t size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  auto* dp = static_cast<DiskPeripheral_t*>(instance);
  switch (static_cast<DiskCmd_e>(cmd)) {
    case disk_cmd_insert:
      return cmd_handle_insert(dp, data, size);
    case disk_cmd_eject:
      return cmd_handle_eject(dp, data, size);
    case disk_cmd_swap_drives:
      return swap_drives(dp) ? peripheral_ok : peripheral_error;
    case disk_cmd_boot:
      // Physical Reality: Booting starts the spindle.
      dp->is_motor_on = true;
      sync_drive_motor_state(dp);
      return peripheral_ok;
    case disk_cmd_set_protect:
      return cmd_handle_set_protect(dp, data, size);
    case disk_driver_cmd_set_enhanced_speed:
      return cmd_handle_set_speed(dp, data, size);
    default:
      break;
  }
  return peripheral_incompatible;
}

auto disk_abi_query(void* instance, uint32_t cmd, void* data, size_t* size)
    -> PeripheralStatus_t {
  if (instance == nullptr || size == nullptr) {
    return peripheral_error;
  }
  auto* dp = static_cast<DiskPeripheral_t*>(instance);

  switch (static_cast<DiskCmd_e>(cmd)) {
    case disk_cmd_get_status: {
      const size_t required_size = sizeof(DiskStatus_t);
      if (data == nullptr) {
        *size = required_size;
        return peripheral_ok;
      }
      if (*size < required_size) {
        *size = required_size;
        return peripheral_error;
      }
      get_peripheral_status(dp, static_cast<DiskStatus_t*>(data));
      *size = required_size;
      return peripheral_ok;
    }
    default:
      break;
  }
  return peripheral_incompatible;
}

auto disk_abi_save_state(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus_t {
  if (instance == nullptr || size == nullptr) {
    return peripheral_error;
  }
  const size_t required_size = sizeof(DiskSavedState_t);
  if (buffer == nullptr) {
    *size = required_size;
    return peripheral_ok;
  }
  if (*size < required_size) {
    return peripheral_error;
  }

  auto* dp = static_cast<DiskPeripheral_t*>(instance);
  auto* s = static_cast<DiskSavedState_t*>(buffer);
  std::fill_n(reinterpret_cast<uint8_t*>(s), sizeof(DiskSavedState_t), 0);

  s->header.version = static_cast<uint32_t>(disk_state_version);
  s->header.size = sizeof(DiskSavedState_t);

  for (int i = 0; i < disk_drive_count; ++i) {
    auto& d = dp->drives.at(static_cast<size_t>(i));
    auto& ds = s->drives[i];
    Util_SafeStrCpy(ds.full_path, d.full_path, max_disk_full_path_len + 1);
    ds.track = d.track;
    ds.phase = d.phase;
    ds.current_byte_pos = static_cast<int32_t>(d.current_byte_pos);
    ds.user_write_protected = d.user_write_protected ? 1 : 0;
    ds.is_os_read_only = d.is_os_read_only ? 1 : 0;
    ds.is_data_loaded = d.is_data_loaded ? 1 : 0;
    ds.is_dirty = d.is_dirty ? 1 : 0;
    ds.spinning_ticks = d.spinning_ticks;
    ds.write_light_ticks = d.write_light_ticks;
    ds.nibble_count = d.nibble_count;
    if (d.track_buffer) {
      std::copy_n(d.track_buffer.get(), nibbles_per_track, ds.track_buffer);
    }
  }
  s->stepper_phase_mask = dp->stepper_phase_mask;
  s->active_drive_index = dp->active_drive_index;
  s->was_accessed_this_tick =
      static_cast<uint8_t>(dp->was_accessed_this_tick ? 1 : 0);
  s->is_speed_enhanced = static_cast<uint8_t>(dp->is_speed_enhanced ? 1 : 0);
  s->io_latch = dp->io_latch;
  s->is_motor_on = static_cast<uint8_t>(dp->is_motor_on ? 1 : 0);
  s->is_write_mode = static_cast<uint8_t>(dp->is_write_mode ? 1 : 0);

  return peripheral_ok;
}

auto disk_abi_load_state(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus_t {
  const size_t required_size = sizeof(DiskSavedState_t);
  if (instance == nullptr || buffer == nullptr || size < required_size) {
    return peripheral_error;
  }
  auto* dp = static_cast<DiskPeripheral_t*>(instance);
  const auto* s = static_cast<const DiskSavedState_t*>(buffer);

  if (s->header.version != static_cast<uint32_t>(disk_state_version)) {
    return peripheral_error;
  }

  dp->stepper_phase_mask = s->stepper_phase_mask;
  dp->active_drive_index = s->active_drive_index;
  dp->was_accessed_this_tick = (s->was_accessed_this_tick != 0);
  dp->is_speed_enhanced = (s->is_speed_enhanced != 0);
  dp->io_latch = s->io_latch;
  dp->is_motor_on = (s->is_motor_on != 0);
  dp->is_write_mode = (s->is_write_mode != 0);

  for (int i = 0; i < disk_drive_count; ++i) {
    const auto& ds = s->drives[i];
    eject_disk_from_drive(dp, i);
    if (insert_disk_into_drive(dp, i, ds.full_path,
                               ds.user_write_protected != 0,
                               false) == disk_err_none) {
      auto& d = dp->drives.at(static_cast<size_t>(i));
      d.track = ds.track;
      d.phase = ds.phase;
      d.current_byte_pos = static_cast<uint32_t>(ds.current_byte_pos);
      d.is_os_read_only = (ds.is_os_read_only != 0);
      d.is_data_loaded = (ds.is_data_loaded != 0);
      d.is_dirty = (ds.is_dirty != 0);
      d.spinning_ticks = ds.spinning_ticks;
      d.write_light_ticks = ds.write_light_ticks;
      d.nibble_count = ds.nibble_count;
      if (d.is_data_loaded) {
        if (!d.track_buffer) {
          d.track_buffer.reset(new uint8_t[nibbles_per_track]());
        }
        std::copy_n(ds.track_buffer, nibbles_per_track, d.track_buffer.get());
      }
    }
  }
  if (dp->host) {
    dp->host->NotifyStatusChanged(dp->slot);
  }
  sync_driver_options(dp);
  return peripheral_ok;
}

}  // namespace

static Peripheral_t g_disk_peripheral = {
    .AbiVersion_t = LINAPPLE_ABI_VERSION,
    .id = "linapple.disk_II",
    .name = "Disk II",
    .description = "Apple II floppy disk controller emulation",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = disk_default_slot,
    .init = disk_abi_init,
    .reset = disk_abi_reset,
    .shutdown = disk_abi_shutdown,
    .think = disk_abi_think,
    .on_vblank = nullptr,
    .save_state = disk_abi_save_state,
    .load_state = disk_abi_load_state,
    .command = disk_abi_command,
    .query = disk_abi_query};

auto disk_get_descriptor() -> Peripheral_t* { return &g_disk_peripheral; }

PERIPHERAL_REGISTER(g_disk_peripheral)
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//           cppcoreguidelines-owning-memory,
//           cppcoreguidelines-pro-bounds-pointer-arithmetic,
//           cppcoreguidelines-avoid-c-arrays,
//           modernize-avoid-c-arrays,
//           cppcoreguidelines-pro-bounds-constant-array-index,
//           cppcoreguidelines-pro-type-reinterpret-cast,
//           cppcoreguidelines-pro-type-const-cast,
//           bugprone-easily-swappable-parameters,
//           modernize-make-unique)
