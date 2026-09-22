// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-deprecated-headers, modernize-use-using)
// Justification: a language-neutral C ABI for C consumers.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/disk/DiskError.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { disk_format_abi_version = 0 };

/* The longest medium a driver may hand over. A synthesised track is at worst
   6656 nibbles at ten cells each; a WOZ 5.25" track is at most 53,248
   (WOZ 2.0, TRKS). */
enum { max_track_bits = 69632 };

/* Cell time in the WOZ INFO unit of 125 ns: 32 is the nominal four
   microseconds. The card runs eight units to a CPU cycle and carries a cell's
   remainder into the next step, so 31 and 33 are exact where nanoseconds
   would drift; the two per cent between a microsecond and a 6502 cycle is
   accepted. */
enum { disk_default_bit_timing = 32 };

/* Each bit and its entry point agree or the loader refuses the descriptor:
   cap_write with write_track_bits, cap_create with create. A caller can then
   trust the bits alone, which is all a query hands the frontend. */
typedef enum {
  disk_driver_cap_write = 0x01,
  disk_driver_cap_create = 0x02
} DiskDriverCap_e;

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

  /* Dot-less and NULL-terminated ("do", "dsk"); ext_hint below is the same
     extension as the loader saw it, lowercased and with its leading dot. */
  const char* const* supported_exts;

  /* header_data is never NULL: the loader always reads ahead before asking,
     and header_size says how much of the image it holds. ext_hint is the
     lowercase extension with its leading dot (".dsk"), or "" when the name
     has none, so a driver compares it with strcmp. */
  DiskProbe_e (*probe)(const uint8_t* header_data, size_t header_size,
                       uint32_t file_size, const char* ext_hint);

  /* read_only is the drive's answer before the medium is even read: a file
     the host will not let us write, or one the loader made and will delete.
     The driver folds it into is_write_protected rather than handing the same
     fact back a second way. */
  DiskError_e (*open)(const char* path, uint32_t file_offset, bool read_only,
                      void** out_instance);

  void (*close)(void* instance);

  /* A NULL instance is write protected: nothing is safer to answer about a
     medium that is not there. */
  bool (*is_write_protected)(void* instance);

  /* bits holds at least (max_bits + 7) / 8 bytes, the caller's promise. A
     quarter track the image does not map is not an error: it answers
     disk_err_none with a zero cell count, because an unrecorded surface is
     noise to the read amplifier rather than a refusal. A track longer than
     max_bits is refused with disk_err_unsupported and never truncated. */
  DiskError_e (*read_track_bits)(void* instance, uint32_t quarter_track,
                                 uint8_t* bits, uint32_t max_bits,
                                 uint32_t* out_bit_count,
                                 uint8_t* out_bit_timing);

  /* The write-back guarantee: the cells are decoded into the track's image
     form before any byte reaches the file, the track's fixed-length slot is
     then rewritten whole in one write, and the fflush is checked. A track
     that will not decode leaves the file untouched; a track that reaches the
     file is complete. There is no temp-and-rename, so a crash between the
     write and the flush can leave one slot torn and never more. */
  DiskError_e (*write_track_bits)(void* instance, uint32_t quarter_track,
                                  const uint8_t* bits, uint32_t bit_count);

  DiskError_e (*create)(const char* path);
} DiskFormatDriver_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers, modernize-use-using)
