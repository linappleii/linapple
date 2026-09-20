// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class)
// Justification: a language-neutral C ABI for C consumers.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/Peripheral_Subsystems.h"

#ifdef __cplusplus
extern "C" {
#endif

struct DiskFormatDriver_t;

enum { disk_default_slot = 6 };

// Bump when DiskSavedState_t changes.
enum { disk_state_version = 1 };

typedef enum { disk_drive_0 = 0, disk_drive_1 = 1 } DiskDrive_t;

enum { disk_drive_count = 2 };

enum {
  max_disk_full_path_len = 255,

  tracks_per_disk = 40,
  phases_per_track = 2,
  // Two stepper phases a track and two half steps a phase: a whole-track
  // format serves the same surface for all four quarter tracks of a cylinder.
  quarter_tracks_per_cylinder = 4,
  max_disk_phases = 80,
  nibbles_per_track = 6656,
  sectors_per_track = 16,

  // One limit for every disk path the card handles, command and query alike:
  // a path that survives an insert must survive coming back out.
  disk_path_max = 504,
  disk_insert_path_max = disk_path_max,
  disk_format_name_max = 64,
  // The command queue carries at most PERIPHERAL_CMD_MAX_DATA bytes, so a
  // path travelling beside a format name has less room than one travelling
  // alone.
  disk_create_path_max = 448,
  disk_status_name_max = 32,
  disk_status_path_max = disk_path_max
};

typedef enum {
  disk_status_off = 0x00,
  disk_status_read = 0x01,
  disk_status_write = 0x02,
  disk_status_prot = 0x04
} DiskStatus_e;

typedef enum {
  disk_cmd_insert = PERIPHERAL_SUBSYSTEM_DISK | 0x0001,
  disk_cmd_eject = PERIPHERAL_SUBSYSTEM_DISK | 0x0002,
  disk_cmd_swap_drives = PERIPHERAL_SUBSYSTEM_DISK | 0x0003,
  disk_cmd_set_protect = PERIPHERAL_SUBSYSTEM_DISK | 0x0004,
  disk_cmd_create_image = PERIPHERAL_SUBSYSTEM_DISK | 0x0007
} DiskCmd_t;

// Query IDs are dispatched through the query ABI callback, separate from
// command IDs, so numeric values do not need to be unique across both spaces.
enum {
  disk_query_status = PERIPHERAL_SUBSYSTEM_DISK | 0x0001,
  disk_query_supported_extensions = PERIPHERAL_SUBSYSTEM_DISK | 0x0002,
  disk_query_format_count = PERIPHERAL_SUBSYSTEM_DISK | 0x0003,
  disk_query_format_name = PERIPHERAL_SUBSYSTEM_DISK | 0x0004
};

typedef struct {
  char path[disk_insert_path_max];
  uint8_t drive;
  uint8_t write_protected;
  uint8_t reserved;
  uint8_t padding[5];
} DiskInsertCmd_t;

typedef struct {
  char path[disk_create_path_max];
  char format_name[disk_format_name_max];
} DiskCreateImageCmd_t;

// The query ABI has no input buffer, so the index the caller wants travels in
// the same struct the name comes back in. capabilities carries the driver's
// DiskDriverCap_e bits, so a "new image" menu can offer only the formats
// whose create the loader has verified is really there.
typedef struct {
  uint32_t index;
  uint32_t capabilities;
  char name[disk_format_name_max];
} DiskFormatNameQuery_t;

typedef struct {
  uint8_t drive;
} DiskEjectCmd_t;

typedef struct {
  uint8_t drive;
  uint8_t write_protected;
} DiskSetProtectCmd_t;

// Uses natural alignment to ensure a deterministic binary layout without
// reliance on non-standard packing directives.
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

// The v1 save-state layout, packed so a file is a fixed 13,897 bytes;
// Disk.cpp converts the card's cell state to and from it.
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
  uint8_t reserved_os_read_only;
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
  uint8_t reserved_tick;
  uint8_t reserved_speed;
  uint8_t io_latch;
  uint8_t is_motor_on;
  uint8_t is_write_mode;
} DiskSavedState_t;
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class)
