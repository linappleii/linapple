// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t clockcard_abi_c_state_size(void);
size_t clockcard_abi_c_fixed_epoch_offset(void);
size_t clockcard_abi_c_latches_offset(void);
size_t clockcard_abi_c_use_fixed_epoch_offset(void);
size_t clockcard_abi_c_reserved_offset(void);
uint32_t clockcard_abi_c_state_version(void);

#ifdef __cplusplus
}
#endif
