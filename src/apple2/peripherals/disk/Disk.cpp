// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-owning-memory, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-type-const-cast, bugprone-easily-swappable-parameters, modernize-make-unique)
// Justification: This module implements low-level Disk
// II hardware emulation using procedural C-style patterns for performance and
// ABI compatibility. Pointer arithmetic and C-style arrays are required for
// bitstream manipulation and save-state structure stability.
// easily-swappable-parameters is mandated by the project-wide Peripheral ABI
// signatures. modernize-make-unique is suppressed to maintain C++11
// compatibility.

#include "apple2/peripherals/disk/Disk.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "EmbeddedRoms.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
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

auto mem_return_random_data(uint8_t highbit) -> uint8_t;

namespace {

namespace config {
constexpr const char* disk_image1_key = "Disk Image 1";
constexpr const char* disk_image2_key = "Disk Image 2";
constexpr size_t path_max_len = 260;
constexpr size_t min_title_len_for_format = 3;
}  // namespace config

namespace physical {
constexpr uint32_t spinup_ticks = 20000;
constexpr uint32_t write_light_ticks = 20000;
constexpr uint8_t latch_bit = 0x80;
constexpr uint8_t floating_bus = 0xFF;
constexpr uint32_t spin_cycle_shift = 6;
constexpr uint32_t spin_cycle_mask = (1U << spin_cycle_shift) - 1;
constexpr uint32_t rotation_cycle_shift = 5;
constexpr uint32_t rotation_cycle_mask = (1U << rotation_cycle_shift) - 1;
}  // namespace physical

namespace regs {
constexpr uint8_t addr_mask = 0x0F;
constexpr uint8_t addr_hi_mask = 0xFF;
constexpr uint8_t stepper_alt = 0xE0;
constexpr uint16_t phase_mask = 0x0F;

// Disk II Controller Softswitches ($C0n0 - $C0nF)
constexpr uint8_t stepper_0 = 0x0;  // Phase 0 Off
constexpr uint8_t stepper_1 = 0x1;  // Phase 0 On
constexpr uint8_t stepper_2 = 0x2;  // Phase 1 Off
constexpr uint8_t stepper_3 = 0x3;  // Phase 1 On
constexpr uint8_t stepper_4 = 0x4;  // Phase 2 Off
constexpr uint8_t stepper_5 = 0x5;  // Phase 2 On
constexpr uint8_t stepper_6 = 0x6;  // Phase 3 Off
constexpr uint8_t stepper_7 = 0x7;  // Phase 3 On
constexpr uint8_t motor_off = 0x8;
constexpr uint8_t motor_on = 0x9;
constexpr uint8_t drive_1 = 0xA;
constexpr uint8_t drive_2 = 0xB;
constexpr uint8_t read_write = 0xC;  // Q6 Strobe Data
constexpr uint8_t shift_reg = 0xD;   // Q6 Shift
constexpr uint8_t read_mode = 0xE;   // Q7 Read
constexpr uint8_t write_mode = 0xF;  // Q7 Write
}  // namespace regs

struct DiskImageMetadata_t {
  std::string full_path;
  std::string display_name;
};

auto disk_image_metadata_copy_to_buffer(const std::string& src, char* dest,
                                        size_t capacity) -> void {
  if (dest == nullptr || capacity == 0) {
    return;
  }
  const size_t copy_len = std::min(src.size(), capacity - 1);
  std::memcpy(dest, src.data(), copy_len);
  dest[copy_len] = '\0';
}

auto disk_image_metadata_export_name(const DiskImageMetadata_t& meta,
                                     char* dest, size_t capacity) -> void {
  disk_image_metadata_copy_to_buffer(meta.display_name, dest, capacity);
}

auto disk_image_metadata_export_path(const DiskImageMetadata_t& meta,
                                     char* dest, size_t capacity) -> void {
  disk_image_metadata_copy_to_buffer(meta.full_path, dest, capacity);
}

struct Disk_t {
  DiskImageMetadata_t metadata{};
  int32_t track = 0;
  int32_t phase = 0;
  uint32_t current_byte_pos = 0;
  bool is_user_write_protected = false;
  bool is_os_read_only = false;
  bool is_data_loaded = false;
  bool is_dirty = false;
  uint32_t spinning_ticks = 0;
  uint32_t write_light_ticks = 0;
  uint32_t nibble_count = 0;
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
  // Attached physical drives
  std::array<Disk_t, disk_drive_count> drives{};
  uint16_t active_drive_index = 0;

  // Controller hardware registers and softswitch flip-flops
  uint8_t io_latch = 0;
  uint16_t stepper_phase_mask = 0;
  bool is_motor_on = false;
  bool is_write_mode = false;

  // Rotational and timing simulation state
  bool was_accessed_this_tick = false;
  uint32_t spin_cycle_accumulator = 0;
  uint32_t rotation_cycle_accumulator = 0;

  // Emulator configuration and host bridge
  bool is_speed_enhanced = true;
  HostInterface_t* host = nullptr;
  int slot = 0;

  DiskPeripheral_t() = default;
};

auto is_drive_valid(int drive_index) -> bool {
  return (drive_index >= 0 && drive_index < disk_drive_count);
}

auto get_active_drive(DiskPeripheral_t* dp) -> Disk_t& {
  return dp->drives.at(static_cast<size_t>(dp->active_drive_index));
}

auto notify_status_changed(const DiskPeripheral_t* dp) -> void {
  if (dp != nullptr && dp->host != nullptr &&
      dp->host->NotifyStatusChanged != nullptr) {
    dp->host->NotifyStatusChanged(dp->slot);
  }
}

auto notify_activity_changed(const DiskPeripheral_t* dp, bool active) -> void {
  if (dp != nullptr && dp->host != nullptr &&
      dp->host->NotifyActivityChanged != nullptr) {
    dp->host->NotifyActivityChanged(dp->slot, active);
  }
}

// Why: Implements multi-layered write protection:
// 1. User manual toggle (the "notch" on a physical disk).
// 2. OS file system permissions.
// 3. Hardware format capabilities (some formats are read-only).
// 4. Format-driver specific runtime protection (e.g. internal container flags).
auto is_disk_write_protected(const DiskPeripheral_t* disk_peripheral,
                             int drive_index) -> bool {
  if (disk_peripheral == nullptr || !is_drive_valid(drive_index)) {
    return false;
  }

  const auto& disk =
      disk_peripheral->drives.at(static_cast<size_t>(drive_index));

  if (disk.is_user_write_protected || disk.is_os_read_only) {
    return true;
  }

  if (disk.driver == nullptr || disk.driver_instance == nullptr) {
    return false;
  }

  const bool can_write =
      (disk.driver->capabilities & disk_driver_cap_write) != 0;
  if (!can_write) {
    return true;
  }

  if (disk.driver->is_write_protected != nullptr) {
    return disk.driver->is_write_protected(disk.driver_instance);
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

  if (disk_ptr->track < 0 || disk_ptr->track >= tracks_per_disk) {
    return;
  }

  if (is_disk_write_protected(disk_peripheral, drive_index)) {
    return;
  }

  if (disk_ptr->track_buffer != nullptr && disk_ptr->driver != nullptr &&
      disk_ptr->driver->write_track != nullptr) {
    disk_ptr->driver->write_track(disk_ptr->driver_instance, disk_ptr->track,
                                  disk_ptr->phase, disk_ptr->track_buffer.get(),
                                  static_cast<int>(disk_ptr->nibble_count));
    disk_ptr->is_dirty = false;
  }
}

auto read_track_from_driver(DiskPeripheral_t* disk_peripheral, int drive_index)
    -> void {
  if (!is_drive_valid(drive_index)) {
    return;
  }

  auto* disk_ptr =
      &disk_peripheral->drives.at(static_cast<size_t>(drive_index));

  if (disk_ptr->track < 0 || disk_ptr->track >= tracks_per_disk) {
    disk_ptr->is_data_loaded = false;
    return;
  }

  if (disk_ptr->track_buffer == nullptr) {
    disk_ptr->track_buffer.reset(new uint8_t[nibbles_per_track]());
  }

  if (disk_ptr->track_buffer != nullptr && disk_ptr->driver != nullptr &&
      disk_ptr->driver->read_track != nullptr) {
    int loaded_nibbles = 0;
    disk_ptr->driver->read_track(disk_ptr->driver_instance, disk_ptr->track,
                                 disk_ptr->phase, disk_ptr->track_buffer.get(),
                                 &loaded_nibbles);

    disk_ptr->current_byte_pos = 0;
    disk_ptr->nibble_count =
        (loaded_nibbles > 0) ? static_cast<uint32_t>(loaded_nibbles) : 0;
    disk_ptr->is_data_loaded = (disk_ptr->nibble_count != 0);
  }
}

auto close_format_driver(Disk_t* disk_ptr) -> void {
  if (disk_ptr == nullptr || disk_ptr->driver == nullptr) {
    return;
  }
  if (disk_ptr->driver->close != nullptr &&
      disk_ptr->driver_instance != nullptr) {
    disk_ptr->driver->close(disk_ptr->driver_instance);
  }
  disk_ptr->driver = nullptr;
  disk_ptr->driver_instance = nullptr;
}

auto eject_disk_from_drive(DiskPeripheral_t* disk_peripheral, int drive_index)
    -> void {
  if (disk_peripheral == nullptr || !is_drive_valid(drive_index)) {
    return;
  }

  auto& disk = disk_peripheral->drives.at(static_cast<size_t>(drive_index));
  if (disk.driver == nullptr) {
    disk = Disk_t();
    return;
  }

  if (disk.track_buffer != nullptr && disk.is_dirty) {
    write_track_to_driver(disk_peripheral, drive_index);
  }

  close_format_driver(&disk);

  if (disk_peripheral->host != nullptr &&
      disk_peripheral->host->SetConfig != nullptr) {
    const char* key =
        (drive_index == 0) ? config::disk_image1_key : config::disk_image2_key;
    disk_peripheral->host->SetConfig("Slots", key, "");
  }
  notify_status_changed(disk_peripheral);

  disk = Disk_t();
}

auto update_disk_metadata(Disk_t* disk_ptr, const char* image_path) -> void {
  if (disk_ptr == nullptr || image_path == nullptr) {
    return;
  }

  disk_ptr->metadata.full_path = image_path;

  const char* start_pos = image_path;
  const char* last_sep = strrchr(start_pos, '/');
  if (last_sep != nullptr) {
    start_pos = last_sep + 1;
  }

  std::string image_title = start_pos;

  bool found_lower = false;
  for (unsigned char ch : image_title) {
    if (std::islower(ch)) {
      found_lower = true;
      break;
    }
  }

  if (!found_lower &&
      image_title.length() >= config::min_title_len_for_format) {
    for (size_t i = 1; i < image_title.length(); ++i) {
      image_title[i] = static_cast<char>(
          std::tolower(static_cast<unsigned char>(image_title[i])));
    }
  }

  const size_t dot_pos = image_title.rfind('.');
  if (dot_pos != std::string::npos && dot_pos > 0) {
    image_title.erase(dot_pos);
  }

  if (image_title.length() > max_disk_image_name_len) {
    image_title.resize(max_disk_image_name_len);
  }

  disk_ptr->metadata.display_name = image_title;
}

auto sync_drive_motor_state(DiskPeripheral_t* disk_peripheral) -> void {
  if (disk_peripheral == nullptr) {
    return;
  }
  auto& drive = get_active_drive(disk_peripheral);
  const bool was_spinning = (drive.spinning_ticks > 0);
  if (disk_peripheral->is_motor_on) {
    drive.spinning_ticks = physical::spinup_ticks;
  }
  const bool now_spinning = (drive.spinning_ticks > 0);

  if (was_spinning != now_spinning) {
    notify_activity_changed(disk_peripheral, now_spinning);
    notify_status_changed(disk_peripheral);
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

  disk_ptr->is_user_write_protected = write_protected;
  const DiskError_e error =
      disk_loader_open(image_path, create_if_necessary,
                       static_cast<uint8_t>(disk_peripheral->is_speed_enhanced),
                       &disk_ptr->is_os_read_only,
                       const_cast<DiskFormatDriver_t**>(&disk_ptr->driver),
                       &disk_ptr->driver_instance);

  disk_ptr->last_error = error;

  if (error == disk_err_none) {
    update_disk_metadata(disk_ptr, image_path);

    if (disk_peripheral->host != nullptr) {
      const char* key = (drive_index == 0) ? config::disk_image1_key
                                           : config::disk_image2_key;
      if (disk_peripheral->host->SetConfig != nullptr) {
        disk_peripheral->host->SetConfig("Slots", key, image_path);
      }
    }
  }

  notify_status_changed(disk_peripheral);

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
    return mem_return_random_data(physical::floating_bus);
  }

  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);

  disk_peripheral->is_motor_on = (memory_address & 0x01) != 0;

  sync_drive_motor_state(disk_peripheral);

  return mem_return_random_data(physical::floating_bus);
}

// Why: Emulates the physical movement of the disk head via the stepper motor.
// Handles phase-to-track mapping and ensures dirty tracks are flushed to the
// format driver before the head leaves the current cylinder.
auto step_drive_head(DiskPeripheral_t* disk_peripheral, int phase_delta)
    -> void {
  if (disk_peripheral == nullptr) {
    return;
  }

  auto& drive = get_active_drive(disk_peripheral);
  const int32_t old_phase = drive.phase;
  const int32_t old_track = drive.track;

  const int32_t new_phase = std::max<int32_t>(
      0, std::min<int32_t>(max_disk_phases - 1, drive.phase + phase_delta));
  const int32_t new_track =
      std::min<int32_t>(tracks_per_disk - 1, new_phase / phases_per_track);

  if (new_phase != old_phase) {
    if (new_track != old_track) {
      if (drive.track_buffer != nullptr && drive.is_dirty) {
        write_track_to_driver(disk_peripheral,
                              disk_peripheral->active_drive_index);
      }
      drive.is_data_loaded = false;
    }
    drive.phase = new_phase;
    drive.track = new_track;
  }
}

// Why: Emulates the physical magnetic stepper motor phases ($C0n0-$C0n7).
// The 6502 code manually energizes/de-energizes four physical magnets
// to 'pull' the head to the next or previous phase.
auto disk_io_control_stepper(void* instance, uint16_t, uint16_t memory_address,
                             uint8_t, uint8_t, uint32_t) -> uint8_t {
  if (instance == nullptr) {
    return mem_return_random_data(physical::floating_bus);
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

  return (memory_address == regs::stepper_alt)
             ? physical::floating_bus
             : mem_return_random_data(physical::floating_bus);
}

auto disk_io_enable_drive(void* instance, uint16_t, uint16_t memory_address,
                          uint8_t, uint8_t, uint32_t) -> uint8_t {
  if (instance == nullptr) {
    return mem_return_random_data(physical::floating_bus);
  }

  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);

  const uint16_t new_drive_index = static_cast<uint16_t>(memory_address & 0x01);
  if (new_drive_index != disk_peripheral->active_drive_index) {
    auto& inactive_drive = get_active_drive(disk_peripheral);
    if (inactive_drive.track_buffer != nullptr && inactive_drive.is_dirty) {
      write_track_to_driver(disk_peripheral,
                            disk_peripheral->active_drive_index);
    }
    inactive_drive.spinning_ticks = 0;
    inactive_drive.write_light_ticks = 0;
    disk_peripheral->active_drive_index = new_drive_index;
  }

  sync_drive_motor_state(disk_peripheral);

  return mem_return_random_data(physical::floating_bus);
}

auto disk_io_read_write(void* instance, uint16_t, uint16_t, uint8_t, uint8_t,
                        uint32_t) -> uint8_t {
  if (instance == nullptr) {
    return mem_return_random_data(physical::floating_bus);
  }

  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);
  auto& drive = get_active_drive(disk_peripheral);

  disk_peripheral->was_accessed_this_tick = true;

  if (!drive.is_data_loaded && drive.driver != nullptr) {
    read_track_from_driver(disk_peripheral,
                           disk_peripheral->active_drive_index);
  }

  if (!drive.is_data_loaded) {
    return mem_return_random_data(physical::floating_bus);
  }

  uint8_t data_byte = 0;
  const bool is_protected = is_disk_write_protected(
      disk_peripheral, disk_peripheral->active_drive_index);

  if (drive.current_byte_pos >= drive.nibble_count ||
      drive.current_byte_pos >= static_cast<uint32_t>(nibbles_per_track)) {
    drive.current_byte_pos = 0;
  }

  if (disk_peripheral->is_write_mode) {
    if (!is_protected &&
        (disk_peripheral->io_latch & physical::latch_bit) != 0) {
      drive.track_buffer[drive.current_byte_pos] = disk_peripheral->io_latch;
      drive.is_dirty = true;
    }
    data_byte = 0;
  } else {
    data_byte = drive.track_buffer[drive.current_byte_pos];
  }

  if (++drive.current_byte_pos >= drive.nibble_count) {
    drive.current_byte_pos = 0;
  }

  return data_byte;
}

auto disk_io_set_latch(void* instance, uint16_t, uint16_t, uint8_t is_write,
                       uint8_t data_value, uint32_t) -> uint8_t {
  if (instance == nullptr) {
    return mem_return_random_data(physical::floating_bus);
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
    return mem_return_random_data(physical::floating_bus);
  }

  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);

  disk_peripheral->is_write_mode = false;

  const bool is_protected = is_disk_write_protected(
      disk_peripheral, disk_peripheral->active_drive_index);

  return is_protected ? physical::latch_bit : 0x00;
}

auto disk_io_set_write_mode(void* instance, uint16_t, uint16_t, uint8_t,
                            uint8_t, uint32_t) -> uint8_t {
  if (instance == nullptr) {
    return mem_return_random_data(physical::floating_bus);
  }

  auto* disk_peripheral = static_cast<DiskPeripheral_t*>(instance);

  disk_peripheral->is_write_mode = true;

  auto& active_drive = get_active_drive(disk_peripheral);

  const bool was_already_writing = (active_drive.write_light_ticks > 0);
  active_drive.write_light_ticks = physical::write_light_ticks;

  if (!was_already_writing) {
    notify_status_changed(disk_peripheral);
  }

  return mem_return_random_data(physical::floating_bus);
}

auto update_drive_physics(DiskPeripheral_t* disk_peripheral, Disk_t* disk_ptr,
                          uint32_t spin_ticks, uint32_t rotation_ticks)
    -> void {
  if (disk_ptr->spinning_ticks > 0 && !disk_peripheral->is_motor_on) {
    if (spin_ticks >= disk_ptr->spinning_ticks) {
      disk_ptr->spinning_ticks = 0;
      if (disk_ptr->track_buffer != nullptr && disk_ptr->is_dirty) {
        const int drive_index =
            (disk_ptr == &disk_peripheral->drives.at(0)) ? 0 : 1;
        write_track_to_driver(disk_peripheral, drive_index);
      }
      notify_activity_changed(disk_peripheral, false);
      notify_status_changed(disk_peripheral);
    } else {
      disk_ptr->spinning_ticks -= spin_ticks;
    }
  }

  const bool is_active_drive = (&get_active_drive(disk_peripheral) == disk_ptr);

  if (disk_peripheral->is_write_mode && is_active_drive &&
      disk_ptr->spinning_ticks > 0) {
    disk_ptr->write_light_ticks = physical::write_light_ticks;
  } else if (disk_ptr->write_light_ticks > 0) {
    if (spin_ticks >= disk_ptr->write_light_ticks) {
      disk_ptr->write_light_ticks = 0;
      notify_status_changed(disk_peripheral);
    } else {
      disk_ptr->write_light_ticks -= spin_ticks;
    }
  }

  if (disk_peripheral->is_speed_enhanced ||
      disk_peripheral->was_accessed_this_tick ||
      disk_ptr->spinning_ticks == 0) {
    return;
  }

  if (disk_peripheral->host != nullptr &&
      disk_peripheral->host->RequestPreciseTiming != nullptr) {
    disk_peripheral->host->RequestPreciseTiming();
  }

  disk_ptr->current_byte_pos += rotation_ticks;
  if (disk_ptr->current_byte_pos >= disk_ptr->nibble_count) {
    disk_ptr->current_byte_pos %=
        (disk_ptr->nibble_count != 0 ? disk_ptr->nibble_count : 1);
  }
}

auto update_physical_disk_state(DiskPeripheral_t* disk_peripheral,
                                uint32_t elapsed_cycles) -> void {
  disk_peripheral->spin_cycle_accumulator += elapsed_cycles;
  const uint32_t spin_ticks =
      disk_peripheral->spin_cycle_accumulator >> physical::spin_cycle_shift;
  disk_peripheral->spin_cycle_accumulator &= physical::spin_cycle_mask;

  disk_peripheral->rotation_cycle_accumulator += elapsed_cycles;
  const uint32_t rotation_ticks = disk_peripheral->rotation_cycle_accumulator >>
                                  physical::rotation_cycle_shift;
  disk_peripheral->rotation_cycle_accumulator &= physical::rotation_cycle_mask;

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

  notify_status_changed(disk_peripheral);

  return true;
}

auto initialize_peripheral(DiskPeripheral_t* disk_peripheral) -> void {
  if (disk_peripheral == nullptr) {
    return;
  }

  for (size_t i = 0; i < disk_peripheral->drives.size(); ++i) {
    auto& drive = disk_peripheral->drives.at(i);
    if (drive.track_buffer != nullptr && drive.is_dirty) {
      write_track_to_driver(disk_peripheral, static_cast<int>(i));
    }
  }

  disk_peripheral->active_drive_index = 0;
  disk_peripheral->io_latch = 0;
  disk_peripheral->stepper_phase_mask = 0;
  disk_peripheral->is_motor_on = false;
  disk_peripheral->is_write_mode = false;
  disk_peripheral->was_accessed_this_tick = false;
  disk_peripheral->spin_cycle_accumulator = 0;
  disk_peripheral->rotation_cycle_accumulator = 0;

  for (auto& drive : disk_peripheral->drives) {
    drive.spinning_ticks = 0;
    drive.write_light_ticks = 0;
    drive.last_error = disk_err_none;
  }

  notify_status_changed(disk_peripheral);
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
    disk_image_metadata_export_name(drive.metadata, status->drive0_name,
                                    disk_status_name_max);
    disk_image_metadata_export_path(drive.metadata, status->drive0_full_path,
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
    disk_image_metadata_export_name(drive.metadata, status->drive1_name,
                                    disk_status_name_max);
    disk_image_metadata_export_path(drive.metadata, status->drive1_full_path,
                                    disk_status_path_max);
  }
}

auto disk_io_read(void* instance, uint16_t program_counter,
                  uint16_t memory_address, uint8_t is_write, uint8_t,
                  uint32_t remaining_cycles) -> uint8_t {
  if (instance == nullptr || is_write != 0) {
    return mem_return_random_data(physical::floating_bus);
  }
  const uint16_t addr = memory_address & regs::addr_hi_mask;
  const uint8_t is_write_op = 0;

  switch (addr & regs::addr_mask) {
    case regs::stepper_0:
    case regs::stepper_1:
    case regs::stepper_2:
    case regs::stepper_3:
    case regs::stepper_4:
    case regs::stepper_5:
    case regs::stepper_6:
    case regs::stepper_7:
      return disk_io_control_stepper(instance, program_counter, addr,
                                     is_write_op, 0, remaining_cycles);
    case regs::motor_off:
    case regs::motor_on:
      return disk_io_control_motor(instance, program_counter, addr, is_write_op,
                                   0, remaining_cycles);
    case regs::drive_1:
    case regs::drive_2:
      return disk_io_enable_drive(instance, program_counter, addr, is_write_op,
                                  0, remaining_cycles);
    case regs::read_write:
      return disk_io_read_write(instance, program_counter, addr, is_write_op, 0,
                                remaining_cycles);
    case regs::shift_reg:
      return disk_io_set_latch(instance, program_counter, addr, is_write_op, 0,
                               remaining_cycles);
    case regs::read_mode:
      return disk_io_set_read_mode(instance, program_counter, addr, is_write_op,
                                   0, remaining_cycles);
    case regs::write_mode:
      return disk_io_set_write_mode(instance, program_counter, addr,
                                    is_write_op, 0, remaining_cycles);
    default:
      break;
  }

  return mem_return_random_data(physical::floating_bus);
}

auto disk_io_write(void* instance, uint16_t program_counter,
                   uint16_t memory_address, uint8_t is_write,
                   uint8_t data_value, uint32_t remaining_cycles) -> uint8_t {
  if (instance == nullptr || is_write == 0) {
    return 0;
  }
  const uint16_t addr = memory_address & regs::addr_hi_mask;
  const uint8_t is_write_op = 1;

  switch (addr & regs::addr_mask) {
    case regs::stepper_0:
    case regs::stepper_1:
    case regs::stepper_2:
    case regs::stepper_3:
    case regs::stepper_4:
    case regs::stepper_5:
    case regs::stepper_6:
    case regs::stepper_7:
      return disk_io_control_stepper(instance, program_counter, addr,
                                     is_write_op, data_value, remaining_cycles);
    case regs::motor_off:
    case regs::motor_on:
      return disk_io_control_motor(instance, program_counter, addr, is_write_op,
                                   data_value, remaining_cycles);
    case regs::drive_1:
    case regs::drive_2:
      return disk_io_enable_drive(instance, program_counter, addr, is_write_op,
                                  data_value, remaining_cycles);
    case regs::read_write:
      return disk_io_read_write(instance, program_counter, addr, is_write_op,
                                data_value, remaining_cycles);
    case regs::shift_reg:
      return disk_io_set_latch(instance, program_counter, addr, is_write_op,
                               data_value, remaining_cycles);
    case regs::read_mode:
      return disk_io_set_read_mode(instance, program_counter, addr, is_write_op,
                                   data_value, remaining_cycles);
    case regs::write_mode:
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
  if (!is_drive_valid(c->drive) ||
      memchr(c->path, '\0', sizeof(c->path)) == nullptr) {
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
  dp->drives.at(static_cast<size_t>(c->drive)).is_user_write_protected =
      (c->write_protected != 0);
  notify_status_changed(dp);
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
  if (host == nullptr || host->RegisterIO == nullptr) {
    return nullptr;
  }
#if ENABLE_ROM_DISK2
  if (host->RegisterCxROM == nullptr) {
    return nullptr;
  }
#endif
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

  if (host->GetConfig != nullptr) {
    char enh[16] = {0};
    host->GetConfig("Slots", "Enhance Disk Speed", enh, sizeof(enh));
    dp->is_speed_enhanced = (enh[0] != '0');
  }

  initialize_peripheral(dp.get());

  char p1[config::path_max_len] = {0};
  char p2[config::path_max_len] = {0};
  if (host->GetConfig != nullptr) {
    host->GetConfig("Slots", config::disk_image1_key, p1, sizeof(p1));
    host->GetConfig("Slots", config::disk_image2_key, p2, sizeof(p2));
  }

  if (p1[0] != '\0') {
    insert_disk_into_drive(dp.get(), 0, p1, false, false);
  }
  if (p2[0] != '\0') {
    insert_disk_into_drive(dp.get(), 1, p2, false, false);
  }

#if ENABLE_ROM_DISK2
  host->RegisterCxROM(slot, const_cast<uint8_t*>(g_rom_disk2));
#endif
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

  switch (cmd) {
    case disk_query_status:
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
    case disk_query_supported_extensions:
    case disk_cmd_get_supported_extensions: {
      if (data == nullptr || *size == 0) {
        *size = 256;
        return peripheral_ok;
      }
      disk_loader_get_supported_extensions(static_cast<char*>(data), *size);
      *size = strlen(static_cast<const char*>(data)) + 1;
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
    disk_image_metadata_export_path(d.metadata, ds.full_path,
                                    sizeof(ds.full_path));
    ds.track = d.track;
    ds.phase = d.phase;
    ds.current_byte_pos = static_cast<int32_t>(d.current_byte_pos);
    ds.user_write_protected = d.is_user_write_protected ? 1 : 0;
    ds.is_os_read_only = d.is_os_read_only ? 1 : 0;
    ds.is_data_loaded = d.is_data_loaded ? 1 : 0;
    ds.is_dirty = d.is_dirty ? 1 : 0;
    ds.spinning_ticks = d.spinning_ticks;
    ds.write_light_ticks = d.write_light_ticks;
    ds.nibble_count = static_cast<int32_t>(d.nibble_count);
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

  dp->stepper_phase_mask = s->stepper_phase_mask & regs::phase_mask;
  dp->active_drive_index =
      (s->active_drive_index < disk_drive_count) ? s->active_drive_index : 0;
  dp->was_accessed_this_tick = (s->was_accessed_this_tick != 0);
  dp->is_speed_enhanced = (s->is_speed_enhanced != 0);
  dp->io_latch = s->io_latch;
  dp->is_motor_on = (s->is_motor_on != 0);
  dp->is_write_mode = (s->is_write_mode != 0);

  for (int i = 0; i < disk_drive_count; ++i) {
    const auto& ds = s->drives[i];
    eject_disk_from_drive(dp, i);
    const auto* end_it =
        std::find(ds.full_path, ds.full_path + sizeof(ds.full_path), '\0');
    const std::string safe_path(ds.full_path,
                                static_cast<size_t>(end_it - ds.full_path));

    if (insert_disk_into_drive(dp, i, safe_path.c_str(),
                               ds.user_write_protected != 0,
                               false) == disk_err_none) {
      auto& d = dp->drives.at(static_cast<size_t>(i));
      if (ds.track < 0 || ds.track >= tracks_per_disk || ds.phase < 0 ||
          ds.phase >= max_disk_phases || ds.nibble_count <= 0 ||
          ds.nibble_count > static_cast<int>(nibbles_per_track) ||
          ds.current_byte_pos < 0 ||
          static_cast<uint32_t>(ds.current_byte_pos) >=
              static_cast<uint32_t>(ds.nibble_count)) {
        if (dp->host != nullptr && dp->host->Log != nullptr) {
          dp->host->Log(
              dp, log_warn,
              "DiskSaveState: Clamped out-of-bounds drive %d state (track: %d, "
              "phase: %d, nibble_count: %d, pos: %d)",
              i, ds.track, ds.phase, ds.nibble_count, ds.current_byte_pos);
        }
      }
      d.track = (ds.track >= 0 && ds.track < tracks_per_disk) ? ds.track : 0;
      d.phase = (ds.phase >= 0 && ds.phase < max_disk_phases) ? ds.phase : 0;
      d.nibble_count = (ds.nibble_count > 0 &&
                        ds.nibble_count <= static_cast<int>(nibbles_per_track))
                           ? static_cast<uint32_t>(ds.nibble_count)
                           : static_cast<uint32_t>(nibbles_per_track);
      d.current_byte_pos =
          (ds.current_byte_pos >= 0 &&
           static_cast<uint32_t>(ds.current_byte_pos) < d.nibble_count)
              ? static_cast<uint32_t>(ds.current_byte_pos)
              : 0;
      d.is_os_read_only = (ds.is_os_read_only != 0);
      d.is_data_loaded = (ds.is_data_loaded != 0);
      d.is_dirty = (ds.is_dirty != 0);
      d.spinning_ticks = ds.spinning_ticks;
      d.write_light_ticks = ds.write_light_ticks;
      if (d.is_data_loaded) {
        if (!d.track_buffer) {
          d.track_buffer.reset(new uint8_t[nibbles_per_track]());
        }
        std::copy_n(ds.track_buffer, nibbles_per_track, d.track_buffer.get());
      }
    }
  }
  notify_status_changed(dp);
  sync_driver_options(dp);
  return peripheral_ok;
}

}  // namespace

static Peripheral_t g_disk_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
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
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-owning-memory, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-type-const-cast, bugprone-easily-swappable-parameters, modernize-make-unique)
