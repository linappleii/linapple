// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned harddisk_abi_c_frame_size(void);
unsigned harddisk_abi_c_frame_offset(int field);
unsigned harddisk_abi_c_insert_size(void);
unsigned harddisk_abi_c_insert_path_offset(void);
unsigned harddisk_abi_c_insert_drive_offset(void);
unsigned harddisk_abi_c_insert_reserved_offset(void);
unsigned harddisk_abi_c_status_size(void);
unsigned harddisk_abi_c_state_version(void);
uint32_t harddisk_abi_c_insert_id(void);
uint32_t harddisk_abi_c_eject_id(void);
uint32_t harddisk_abi_c_set_protect_id(void);
uint32_t harddisk_abi_c_status_query_id(void);
uint32_t harddisk_abi_c_extensions_query_id(void);
int harddisk_abi_c_error_none(void);
int harddisk_abi_c_error_not_block_image(void);
unsigned harddisk_abi_c_prodos_codes(void);

#ifdef __cplusplus
}
#endif
