/*
 * HarddiskFormatDriver.h - LinApple Harddisk Format Driver ABI
 */

#pragma once

// NOLINTBEGIN(modernize-deprecated-headers,modernize-use-using)
// Rationale: intentional C99 ABI header. <cstdint> and friends and 'using'
// are C++ only and cannot appear in a C99-compatible interface.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { LINAPPLE_HARDDISK_ABI_VERSION = 0 };

typedef enum {
  HARDDISK_DRIVER_CAP_WRITE = 0x01
} HarddiskDriverCap_e;

typedef enum {
  HARDDISK_PROBE_NO = 0,
  HARDDISK_PROBE_POSSIBLE = 1,
  HARDDISK_PROBE_DEFINITE = 2
} HarddiskProbe_e;

typedef enum {
  HARDDISK_ERR_NONE = 0,
  HARDDISK_ERR_IO = 1,
  HARDDISK_ERR_NOT_FOUND = 2,
  HARDDISK_ERR_READ_ONLY = 3,
  HARDDISK_ERR_INVALID_FORMAT = 4
} HarddiskError_e;

/*
 * Register a harddisk driver by populating this struct and passing it to HarddiskLoader.
 * All function pointers must be non-NULL unless documented otherwise.
 */
typedef struct HarddiskFormatDriver_t {
  int         abi_version;
  uint32_t    capabilities;  /* bitmask of HarddiskDriverCap_e */
  const char* name;          /* e.g. "Raw" */

  /* header contains up to 4096 bytes.
   * ext_hint is lowercase with dot (e.g. ".hdv") or empty if unknown. */
  HarddiskProbe_e (*probe)(const uint8_t* header, size_t header_size,
                          uint32_t file_size, const char* ext_hint);

  /* Called after probe() succeeds. out_os_readonly is set to true if the file was
   * opened read-only by the OS. Writes an opaque instance to *out_instance. */
  HarddiskError_e (*open)(const char* path, bool* out_os_readonly, void** out_instance);

  void (*close)(void* instance);

  bool (*is_write_protected)(void* instance);

  /* block_num is 0 to (total_blocks - 1). buffer is 512 bytes. */
  HarddiskError_e (*read_block)(void* instance, uint32_t block_num, uint8_t* buffer);

  /* NULL unless HARDDISK_DRIVER_CAP_WRITE is set. */
  HarddiskError_e (*write_block)(void* instance, uint32_t block_num, const uint8_t* buffer);

  uint32_t (*get_total_blocks)(void* instance);
} HarddiskFormatDriver_t;

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-deprecated-headers,modernize-use-using)
