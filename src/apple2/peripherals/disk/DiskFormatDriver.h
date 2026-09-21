// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// modernize-use-trailing-return-type) Justification: This header defines a
// language-neutral C ABI. C system headers, typedefs, and C-style return types
// are required for compatibility with C-based consumers.
// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/disk/DiskError.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { disk_format_abi_version = 0 };

/* The longest medium a driver may hand over. A synthesised track is at worst
   6656 nibbles at ten cells each; a WOZ 5.25" track stays under 53,440. */
enum { max_track_bits = 69632 };

/* Cell time in 125 ns units, the scale WOZ INFO already uses: 32 is the
   nominal four microseconds. The raw byte crosses the boundary because every
   consumer divides it by eight to get CPU cycles, which keeps 31 and 33
   exact where nanoseconds would drift. */
enum { disk_default_bit_timing = 32 };

typedef enum { disk_driver_cap_write = 0x01 } DiskDriverCap_e;

typedef enum {
  disk_probe_no = 0,
  disk_probe_possible = 1,
  disk_probe_definite = 2
} DiskProbe_e;

/**
 * @brief Domain: Disk Format Driver ABI
 *
 * Defines the contract for pluggable disk image handlers. Drivers provide
 * probing, lifecycle management, and track-level I/O.
 *
 * The unit of exchange is the medium as the head sees it: cells packed eight
 * to a byte, the first cell in the most significant bit of byte zero. How
 * those cells group into bytes is the controller's reading of them, not the
 * driver's.
 */
typedef struct DiskFormatDriver_t {
  int abi_version;
  uint32_t capabilities;
  const char* name;
  const char* const* supported_exts;

  DiskProbe_e (*probe)(const uint8_t* header_data, size_t header_size,
                       uint32_t file_size, const char* ext_hint);

  /* read_only is the drive's answer before the medium is even read: a file
     the host will not let us write, or one the loader made and will delete.
     The driver folds it into is_write_protected rather than handing the same
     fact back a second way. */
  DiskError_e (*open)(const char* path, uint32_t file_offset, bool read_only,
                      void** out_instance);

  void (*close)(void* instance);

  bool (*is_write_protected)(void* instance);

  /* A quarter track the image does not map is not an error: it answers
     disk_err_none with a zero cell count, because an unrecorded surface is
     noise to the read amplifier rather than a refusal. A track longer than
     max_bits is refused with disk_err_unsupported and never truncated. */
  DiskError_e (*read_track_bits)(void* instance, uint32_t quarter_track,
                                 uint8_t* bits, uint32_t max_bits,
                                 uint32_t* out_bit_count,
                                 uint8_t* out_bit_timing);

  DiskError_e (*write_track_bits)(void* instance, uint32_t quarter_track,
                                  const uint8_t* bits, uint32_t bit_count);

  DiskError_e (*create)(const char* path);
} DiskFormatDriver_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using)
