// SPDX-License-Identifier: GPL-2.0-only
/* Compiled as C99: the hard disk's command and format-driver headers are the
 * ABI a C consumer sees, so what they measure is handed back for the C++
 * suite to compare with its own view. */
/* Justification: a header this unit names nothing from is still under test,
   since compiling it as C99 is the point. */
/* NOLINTBEGIN(misc-include-cleaner) */
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskError.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/HarddiskLoader.h"
/* NOLINTEND(misc-include-cleaner) */

unsigned harddisk_abi_c_frame_size(void) {
  return (unsigned)sizeof(HarddiskSaveState_t);
}

unsigned harddisk_abi_c_frame_drives_offset(void) {
  return (unsigned)offsetof(HarddiskSaveState_t, drives);
}

unsigned harddisk_abi_c_frame_drive_size(void) {
  return (unsigned)sizeof(HarddiskDriveSaveState_t);
}

unsigned harddisk_abi_c_frame_unit_offset(void) {
  return (unsigned)offsetof(HarddiskSaveState_t, unit_num);
}

unsigned harddisk_abi_c_insert_size(void) {
  return (unsigned)sizeof(HarddiskInsertCmd_t);
}

unsigned harddisk_abi_c_insert_path_offset(void) {
  return (unsigned)offsetof(HarddiskInsertCmd_t, path);
}

unsigned harddisk_abi_c_insert_drive_offset(void) {
  return (unsigned)offsetof(HarddiskInsertCmd_t, drive);
}

unsigned harddisk_abi_c_status_size(void) {
  return (unsigned)sizeof(HarddiskStatus_t);
}

unsigned harddisk_abi_c_state_version(void) { return HARDDISK_STATE_VERSION; }

uint32_t harddisk_abi_c_insert_id(void) { return harddisk_cmd_insert; }

uint32_t harddisk_abi_c_eject_id(void) { return harddisk_cmd_eject; }

uint32_t harddisk_abi_c_set_protect_id(void) {
  return harddisk_cmd_set_protect;
}

uint32_t harddisk_abi_c_status_query_id(void) { return harddisk_query_status; }

uint32_t harddisk_abi_c_extensions_query_id(void) {
  return harddisk_query_supported_extensions;
}

int harddisk_abi_c_error_none(void) { return harddisk_err_none; }

int harddisk_abi_c_error_not_block_image(void) {
  return harddisk_err_not_block_image;
}

unsigned harddisk_abi_c_prodos_codes(void) {
  return ((unsigned)harddisk_prodos_io_error << 16) |
         ((unsigned)harddisk_prodos_no_device << 8) |
         (unsigned)harddisk_prodos_write_protected;
}
