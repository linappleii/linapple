// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)
// Justification: This header defines a language-neutral C ABI. C system
// headers, typedefs, and C-style return types are required for compatibility
// with C-based consumers.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/disk/DiskError.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct DiskFormatDriver_t;

enum { disk_default_slot = 6 };

typedef enum {
  disk_drive_0 = 0,
  disk_drive_1 = 1,
  disk_drive_count = 2
} DiskDrive_e;

enum {
  max_disk_image_name_len = 15,
  max_disk_full_path_len = 255,

  tracks_per_disk = 40,
  phases_per_track = 2,
  max_disk_phases = 80,
  nibbles_per_track = 0x1A00,
  sectors_per_track = 16,
  interleave_modes_count = 3,

  disk_encoding_encode_table_size = 64,
  disk_encoding_decode_table_size = 128,
  disk_encoding_sector_data_size = 342,
  disk_encoding_sector_with_checksum_size = 343,

  disk_encoding_work_buffer_offset = 0x1000,
  disk_encoding_checksum_buffer_offset = 0x1400,

  disk_encoding_gap1_size = 16,
  disk_encoding_gap2_size = 10,
  disk_encoding_gap3_size = 16,

  disk_insert_path_max = 504,
  disk_status_name_max = 32,
  disk_status_path_max = 256
};

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
  disk_cmd_get_status = 0x0005,
  disk_cmd_boot = 0x0006,
  disk_driver_cmd_set_enhanced_speed = 0x1001
} DiskCmd_e;

// Why: Maintained for binary compatibility with legacy callers.
// Plan to remove in a future version in favor of a modern serialization format.
#pragma pack(push, 1)
typedef struct {
  uint8_t drive;
  char path[disk_insert_path_max];
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

typedef struct {
  int32_t drive0_last_error;
  uint8_t drive0_loaded;
  uint8_t drive0_spinning;
  uint8_t drive0_writing;
  uint8_t drive0_write_protected;
  char drive0_name[disk_status_name_max];
  char drive0_full_path[disk_status_path_max];

  int32_t drive1_last_error;
  uint8_t drive1_loaded;
  uint8_t drive1_spinning;
  uint8_t drive1_writing;
  uint8_t drive1_write_protected;
  char drive1_name[disk_status_name_max];
  char drive1_full_path[disk_status_path_max];
} DiskStatus_t;

// Why: Restores default alignment after the legacy compatibility block.
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, modernize-use-trailing-return-type)
