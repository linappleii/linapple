// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// modernize-use-trailing-return-type) Justification: This header defines a
// language-neutral C ABI. C system headers, typedefs, and C-style return types
// are required for compatibility with C-based consumers.
// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct DiskFormatDriver_t;

// Default expansion slot for the Disk II controller card.
enum { disk_default_slot = 6 };

// Binary save-state format version. Increment when DiskSavedState_t changes.
enum { disk_state_version = 1 };

typedef enum { disk_drive_0 = 0, disk_drive_1 = 1 } DiskDrive_t;

enum { disk_drive_count = 2 };

enum {
  max_disk_full_path_len = 255,

  tracks_per_disk = 40,
  phases_per_track = 2,
  max_disk_phases = 80,
  nibbles_per_track = 6656,
  sectors_per_track = 16,
  interleave_modes_count = 3,

  disk_insert_path_max = 504,
  disk_status_name_max = 32,
  disk_status_path_max = 256
};

// Bitmask flags — values may be combined with | to represent concurrent states
// (e.g. disk_status_read | disk_status_prot for a protected spinning drive).
typedef enum {
  disk_status_off = 0x00,
  disk_status_read = 0x01,
  disk_status_write = 0x02,
  disk_status_prot = 0x04
} DiskStatus_e;

typedef enum {
  disk_cmd_insert = 0x0001,
  disk_cmd_eject = 0x0002,
  disk_cmd_swap_drives = 0x0003,
  disk_cmd_set_protect = 0x0004,
  disk_cmd_boot = 0x0006,
  // disk_driver_cmd_* commands are issued by the peripheral layer to format
  // drivers rather than by frontends to the controller.
  disk_driver_cmd_set_enhanced_speed = 0x1001
} DiskCmd_t;

// Query IDs are dispatched through the query ABI callback, separate from
// command IDs, so numeric values do not need to be unique across both spaces.
enum { disk_query_status = 0x0001, disk_query_supported_extensions = 0x0002 };

typedef struct {
  char path[disk_insert_path_max];
  uint8_t drive;
  uint8_t write_protected;
  uint8_t create_if_necessary;
  uint8_t padding[5];
} DiskInsertCmd_t;

typedef struct {
  uint8_t drive;
} DiskEjectCmd_t;

typedef struct {
  uint8_t drive;
  uint8_t write_protected;
} DiskSetProtectCmd_t;

// Why: Uses natural alignment to ensure a deterministic binary layout without
// reliance on non-standard packing directives. Large types are placed at the
// start of the structure.
typedef struct {
  int32_t drive0_last_error;
  int32_t drive1_last_error;
  uint8_t drive0_loaded;
  uint8_t drive0_spinning;
  uint8_t drive0_writing;
  uint8_t drive0_write_protected;
  uint8_t drive1_loaded;
  uint8_t drive1_spinning;
  uint8_t drive1_writing;
  uint8_t drive1_write_protected;
  uint8_t padding[2];
  char drive0_name[disk_status_name_max];
  char drive0_full_path[disk_status_path_max];
  char drive1_name[disk_status_name_max];
  char drive1_full_path[disk_status_path_max];
} DiskStatus_t;

// Why: Maintained for binary compatibility with legacy save-states.
// Plan to remove in a future version in favor of a modern serialization format.
#pragma pack(push, 1)
typedef struct {
  uint32_t version;
  uint32_t size;
} DiskStateHeader_t;

typedef struct {
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
} DiskDriveState_t;

typedef struct {
  DiskStateHeader_t header;
  DiskDriveState_t drives[disk_drive_count];
  uint16_t stepper_phase_mask;
  uint16_t active_drive_index;
  uint8_t was_accessed_this_tick;
  uint8_t is_speed_enhanced;
  uint8_t io_latch;
  uint8_t is_motor_on;
  uint8_t is_write_mode;
} DiskSavedState_t;
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class)
