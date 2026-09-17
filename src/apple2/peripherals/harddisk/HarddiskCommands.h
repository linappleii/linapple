// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// Justification:
// This header defines the C99-compatible public ABI for the Harddisk subsystem.
// C-style return types and typedefs are required for cross-language
// compatibility with C-based consumers.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { HARDDISK_STATE_VERSION = 1 };

typedef enum {
  harddisk_drive_0 = 0,
  harddisk_drive_1 = 1,
  harddisk_drive_count = 2
} HarddiskDrive_t;

typedef HarddiskDrive_t HarddiskDrive_e;

typedef enum {
  harddisk_cmd_insert = 0x0001,
  harddisk_cmd_eject = 0x0002,
  harddisk_cmd_set_protect = 0x0004,
  harddisk_cmd_reset_status = 0x0006,
  // Backward-compatibility aliases
  harddisk_cmd_get_status = 0x0005,
  harddisk_cmd_get_supported_extensions = 0x0007
} HarddiskCmd_t;

typedef HarddiskCmd_t HarddiskCmd_e;

typedef enum {
  harddisk_query_status = 0x0001,
  harddisk_query_supported_extensions = 0x0002
} HarddiskQuery_t;

enum { harddisk_insert_path_max = 504, harddisk_default_slot = 7 };

typedef struct {
  char path[harddisk_insert_path_max];
  uint8_t drive;
  uint8_t write_protected;
  uint8_t create_if_necessary;
  uint8_t padding[5];
} HarddiskInsertCmd_t;

typedef struct {
  uint8_t drive;
} HarddiskEjectCmd_t;

typedef struct {
  uint8_t drive;
  uint8_t write_protected;
} HarddiskSetProtectCmd_t;

enum { harddisk_status_name_max = 32, harddisk_status_path_max = 512 };

typedef enum {
  harddisk_status_off = 0x00,
  harddisk_status_read = 0x01,
  harddisk_status_write = 0x02,
  harddisk_status_prot = 0x04
} HarddiskStatus_e;

// Why: Uses natural alignment to ensure a deterministic binary layout without
// reliance on non-standard packing directives. Large types are placed at the
// start of the structure.
typedef struct {
  int32_t drive0_last_error;
  int32_t drive1_last_error;
  uint8_t drive0_loaded;
  uint8_t drive0_write_protected;
  uint8_t drive1_loaded;
  uint8_t drive1_write_protected;
  uint8_t activity_status;
  uint8_t padding[3];
  char drive0_name[harddisk_status_name_max];
  char drive0_full_path[harddisk_status_path_max];
  char drive1_name[harddisk_status_name_max];
  char drive1_full_path[harddisk_status_path_max];
} HarddiskStatus_t;

// Why: 1072-byte naturally aligned POD representing per-drive state.
typedef struct {
  char image_name[harddisk_status_name_max]; /* 32 bytes */
  char full_path[harddisk_status_path_max];  /* 512 bytes */
  int32_t last_error;                        /* 4 bytes */
  uint16_t memory_address;                   /* 2 bytes */
  uint16_t disk_block;                       /* 2 bytes */
  uint16_t buffer_ptr;                       /* 2 bytes */
  uint8_t error_code;                        /* 1 byte */
  uint8_t is_loaded;                         /* 1 byte */
  uint8_t os_readonly;                       /* 1 byte */
  uint8_t user_write_protected;              /* 1 byte */
  uint8_t padding[2];                        /* 2 bytes */
  uint8_t data_buffer[512];                  /* 512 bytes */
} HarddiskDriveSaveState_t;                  /* 1072 bytes */

// Why: 2160-byte naturally aligned POD representing controller & drive state.
// 8 + (2 * 1072) + 8 = 2160 bytes. (2160 % 8 == 0).
typedef struct {
  // --- Header (8 bytes) ---
  uint32_t version;     /* 4 bytes */
  uint32_t struct_size; /* 4 bytes */

  // --- Drives (2 * 1072 = 2144 bytes) ---
  HarddiskDriveSaveState_t drives[harddisk_drive_count];

  // --- Controller Scalars (8 bytes) ---
  uint8_t unit_num;
  uint8_t command_reg;
  uint8_t rom_active;
  uint8_t is_enabled;
  uint8_t activity_status;
  uint8_t slot;
  uint8_t padding[2];
} HarddiskSaveState_t; /* 2160 bytes */

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
