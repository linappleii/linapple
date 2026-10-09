// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t printer_abi_c_state_size(void);
size_t printer_abi_c_version_offset(void);
size_t printer_abi_c_struct_size_offset(void);
size_t printer_abi_c_total_chars_printed_offset(void);
size_t printer_abi_c_busy_cycles_offset(void);
size_t printer_abi_c_data_latch_offset(void);
size_t printer_abi_c_status_latch_offset(void);
size_t printer_abi_c_is_online_offset(void);
size_t printer_abi_c_is_busy_offset(void);
uint32_t printer_abi_c_state_version(void);

#ifdef __cplusplus
}
#endif
