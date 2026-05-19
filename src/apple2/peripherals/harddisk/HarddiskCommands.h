// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using,
//             cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// Justification: This header defines the C99-compatible public ABI for the
// Harddisk subsystem. C-style return types and typedefs are required for
// cross-language compatibility with C-based consumers.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  harddisk_drive_0 = 0,
  harddisk_drive_1 = 1,
  harddisk_drive_count = 2
} HarddiskDrive_e;

typedef enum {
  harddisk_cmd_insert = 0x0001,
  harddisk_cmd_eject = 0x0002,
  harddisk_cmd_set_protect = 0x0004,
  harddisk_cmd_get_status = 0x0005,
  harddisk_cmd_reset_status = 0x0006
} HarddiskCmd_e;

enum { harddisk_insert_path_max = 504 };

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

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using,
//           cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
