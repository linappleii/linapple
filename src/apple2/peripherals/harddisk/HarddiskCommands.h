// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, readability-identifier-naming)
// Justification:
// This header defines the C99-compatible public ABI for the Harddisk subsystem.
// C-style return types and typedefs are required for cross-language
// compatibility with C-based consumers.

#include <stdbool.h>
#include <stdint.h>

#include "apple2/peripherals/Peripheral_Subsystems.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { HARDDISK_STATE_VERSION = 1 };

typedef enum {
  harddisk_drive_0 = 0,
  harddisk_drive_1 = 1,
  harddisk_drive_count = 2,
} HarddiskDrive_t;

/* 0x0005-0x0007 are retired ids, never reassigned, so a sender built against
   them is answered incompatible. */
typedef enum {
  harddisk_cmd_insert = PERIPHERAL_SUBSYSTEM_HARDDISK | 0x0001,
  harddisk_cmd_eject = PERIPHERAL_SUBSYSTEM_HARDDISK | 0x0002,
  harddisk_cmd_set_protect = PERIPHERAL_SUBSYSTEM_HARDDISK | 0x0004,
} HarddiskCmd_t;

typedef enum {
  harddisk_query_status = PERIPHERAL_SUBSYSTEM_HARDDISK | 0x0001,
  harddisk_query_supported_extensions = PERIPHERAL_SUBSYSTEM_HARDDISK | 0x0002,
} HarddiskQuery_t;

enum { harddisk_insert_path_max = 504, harddisk_default_slot = 7 };

typedef struct {
  char path[harddisk_insert_path_max];
  uint8_t drive;
  uint8_t write_protected;
  uint8_t reserved;
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
  harddisk_status_prot = 0x04,
} HarddiskStatus_e;

// Widest members first and natural alignment, so the layout is the same in
// every consumer without a packing directive.
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

/* The controller's registers, and nothing else: the image in each drive is
   the host's to mount and the configuration's to name, and the 512-byte
   buffer is re-read from the mounted image on load. data_phase is 0 idle, 1
   read-out (the buffer holds the block the firmware is fetching), 2 write-in
   (the firmware was pushing a WRITE, whose bytes a load cannot recover). */
typedef struct {
  uint32_t version;
  uint32_t struct_size;
  uint8_t unit;
  uint8_t command;
  uint8_t result;
  uint8_t data_phase;
  uint16_t block;
  uint16_t data_index;
  uint16_t block_count;
  uint8_t reserved[2];
} HarddiskSaveState_t;

enum { harddisk_save_state_size = 20 };

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, readability-identifier-naming)
