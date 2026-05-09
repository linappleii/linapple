/*
 * DiskFormatDriver.h - LinApple Disk Format Driver ABI
 *
 * Drivers implemented against this ABI can be statically linked or loaded
 * as shared objects. ABI version 0 is alpha; it becomes 1 at LinApple 4.0.
 */

#pragma once

// NOLINTBEGIN(modernize-deprecated-headers,modernize-use-using)
// Rationale: intentional C99 ABI header. <cstdint> and friends and 'using'
// are C++ only and cannot appear in a C99-compatible interface.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "apple2/peripherals/disk/DiskError.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { LINAPPLE_DISK_ABI_VERSION = 0 };

/* OR together to set DiskFormatDriver_t.capabilities */
typedef enum {
  DRIVER_CAP_WRITE = 0x01,
  DRIVER_CAP_FLUX = 0x02, /* reserved — see read_flux_bit */
  DRIVER_CAP_DOUBLE_SIDED = 0x04
} DiskDriverCap_e;

typedef enum {
  DISK_PROBE_NO = 0,
  DISK_PROBE_POSSIBLE = 1, /* use only if no DEFINITE driver claims the image */
  DISK_PROBE_DEFINITE = 2
} DiskProbe_e;

/* Opaque type reserved for future flux-accurate timing. Never dereference. */
typedef struct FluxBit_s FluxBit_t;

/*
 * Register a driver by populating this struct and passing it to DiskLoader.
 * All function pointers must be non-NULL unless documented otherwise.
 * probe() receives at most 4096 bytes of the file header.
 * read_track()/write_track() buffers are always 6656 bytes (NIBBLES_PER_TRACK).
 */
typedef struct DiskFormatDriver_t {
  int         abi_version;
  uint32_t    capabilities;  /* bitmask of DiskDriverCap_e */
  const char* name;          /* e.g. "WOZ2" */

  /* null-terminated array of creatable extensions, e.g. {".do", ".dsk", NULL}; NULL if none */
  const char* const* creatable_exts;

  /* header contains up to 4096 bytes (header_size may be less).
   * ext_hint is lowercase with dot (e.g. ".woz") or empty if unknown.
   * Must not assume the full file is in memory. */
  DiskProbe_e (*probe)(const uint8_t* header, size_t header_size,
                       uint32_t file_size, const char* ext_hint);

  /* Called after probe() succeeds. file_offset is non-zero when a MacBinary
   * wrapper is present. out_os_readonly is set to true if the file was
   * opened read-only. Writes an opaque instance to *out_instance. */
  DiskError_e (*open)(const char* path, uint32_t file_offset,
                      bool* out_os_readonly, void** out_instance);

  /* Must not be NULL. */
  void (*close)(void* instance);

  /* Format-level protection only; does not reflect OS read-only status. */
  bool (*is_write_protected)(void* instance);

  /* track is 0–34; phase is the half-track index (0–79, odd = half-track
   * between whole tracks). */
  void (*read_track)(void* instance, int track, int phase,
                     uint8_t* trackImageBuffer, int* nibbles_out);

  /* NULL unless DRIVER_CAP_WRITE is set. track/phase same ranges as read_track. */
  void (*write_track)(void* instance, int track, int phase,
                      const uint8_t* trackImage, int nibbles);

  /* NULL unless creatable_exts is non-empty. path is absolute. */
  DiskError_e (*create)(const char* path);

  /* Reserved. All drivers must set this to NULL.
   * Do not set DRIVER_CAP_FLUX until this interface is finalised. */
  void (*read_flux_bit)(void* instance, uint32_t elapsed_cycles,
                        FluxBit_t* out);
} DiskFormatDriver_t;

// NOLINTEND(modernize-deprecated-headers,modernize-use-using)

#ifdef __cplusplus
}
#endif
