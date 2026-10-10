// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, readability-identifier-naming)
// Justification: a language-neutral C ABI for C consumers.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/harddisk/HarddiskError.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { harddisk_format_abi_version = 0 };

/* The bit and its entry point agree or the loader refuses the descriptor:
   cap_write with write_block. */
typedef enum {
  harddisk_driver_cap_none = 0x00,
  harddisk_driver_cap_write = 0x01,
} HarddiskDriverCap;

typedef enum {
  harddisk_probe_no = 0,
  harddisk_probe_possible = 1,
  harddisk_probe_definite = 2,
} HarddiskProbe;

/* The unit of exchange is the 512-byte block ProDOS addresses: how the file
   serializes it is the driver's business and nobody else's. */
typedef struct HarddiskFormatDriver_t {
  int abi_version;
  uint32_t capabilities;
  const char* name;

  /* Dot-less and NULL-terminated ("hdv", "po"); ext_hint below is the same
     extension as the loader saw it, lowercased and with its leading dot. */
  const char* const* supported_exts;

  /* header_data is never NULL: the loader always reads ahead before asking,
     and header_size says how much of the image it holds. ext_hint is the
     lowercase extension with its leading dot (".hdv"), or "" when the name
     has none, so a driver compares it with strcmp. */
  HarddiskProbe (*probe)(const uint8_t* header_data, size_t header_size,
                           uint64_t file_size, const char* ext_hint);

  /* read_only is known before the medium is read: the loader sets it for a
     temporary it made and will delete. The driver folds it into
     is_write_protected with what it finds itself, such as a file it cannot
     open for writing, rather than handing the same fact back a second way. */
  HarddiskError (*open)(const char* path, uint32_t file_offset,
                          bool read_only, void** out_instance);

  void (*close)(void* instance);

  /* A NULL instance is write protected: nothing is safer to answer about a
     medium that is not there. */
  bool (*is_write_protected)(void* instance);

  /* buffer holds 512 bytes, the caller's promise. A block past the end is
     harddisk_err_io, as a seek past the medium would be. */
  HarddiskError (*read_block)(void* instance, uint32_t block_num,
                                uint8_t* buffer);

  /* The block reaches the kernel before the call returns: the bytes are
     written and the stream flushed, and a failure of either is
     harddisk_err_io. A protected medium answers harddisk_err_read_only. */
  HarddiskError (*write_block)(void* instance, uint32_t block_num,
                                 const uint8_t* buffer);

  uint32_t (*get_total_blocks)(void* instance);
} HarddiskFormatDriver_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using, cppcoreguidelines-use-enum-class, readability-identifier-naming)
