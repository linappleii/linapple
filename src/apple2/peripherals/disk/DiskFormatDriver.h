// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using,
// modernize-use-trailing-return-type) Justification: This header defines a
// language-neutral C ABI. C system headers, typedefs, and C-style return types
// are required for compatibility with C-based consumers.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/disk/DiskError.h"
#include "core/Peripheral_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { disk_format_abi_version = 0 };

typedef enum {
  disk_driver_cap_write = 0x01,
  disk_driver_cap_flux = 0x02,
  disk_driver_cap_double_sided = 0x04
} DiskDriverCap_e;

typedef enum {
  disk_probe_no = 0,
  disk_probe_possible = 1,
  disk_probe_definite = 2
} DiskProbe_e;

typedef struct DiskFluxBit_s DiskFluxBit_t;

/**
 * @brief Domain: Disk Format Driver ABI
 *
 * Defines the contract for pluggable disk image handlers. Drivers provide
 * probing, lifecycle management, and track-level I/O.
 */
typedef struct DiskFormatDriver_t {
  int abi_version;
  uint32_t capabilities;
  const char* name;
  const char* const* creatable_exts;

  DiskProbe_e (*probe)(const uint8_t* header_data, size_t header_size,
                       uint32_t file_size, const char* ext_hint);

  DiskError_e (*open)(const char* path, uint32_t file_offset,
                      uint8_t enhanced_speed, bool* out_is_read_only,
                      void** out_instance);

  void (*close)(void* instance);

  bool (*is_write_protected)(void* instance);

  void (*read_track)(void* instance, int track, int phase,
                     uint8_t* track_buffer, int* out_nibbles);

  void (*write_track)(void* instance, int track, int phase,
                      const uint8_t* track_buffer, int nibbles);

  DiskError_e (*create)(const char* path);

  PeripheralStatus_t (*command)(void* instance, uint32_t cmd_id,
                                const void* data, size_t size);

  void (*read_flux_bit)(void* instance, uint32_t elapsed_cycles,
                        DiskFluxBit_t* out_flux);
} DiskFormatDriver_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using,
// modernize-use-trailing-return-type)
