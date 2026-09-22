// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "apple2/peripherals/disk/DiskFormatDriver.h"

#ifdef __cplusplus
extern "C" {
#endif

// NOLINTBEGIN(modernize-use-using)
// Justification: This header defines a language-neutral C ABI for the disk
// image loader.

/* Two contracts hold across everything below.
   Ownership: an open image is the caller's, released by passing it to the
   driver's own close; a driver pointer is never the caller's, it names storage
   its translation unit keeps for the life of the process.
   Threading: registration runs during static initialisation; every other
   entry point runs on the emulation thread and nowhere else.
   The registry is a plain list with no lock around it. */

/* Registering the same descriptor twice is a silent no-op. A different driver
   whose name is already registered is refused and the refusal recorded: the
   name is how a user and disk_loader_create pick a format, so it must pick
   exactly one. */
void disk_loader_register(const DiskFormatDriver_t* driver);

/* Restores the drivers that registered themselves and forgets everything else,
   refusals included. Test-only: a suite that pushes a synthetic driver in
   needs a way back to a known state. Production code never calls it. */
void disk_loader_reset(void);

/* Reports one driver the loader refused. */
typedef void (*DiskDriverRejectionFn_t)(void* context, const char* driver_name,
                                        const char* reason);

/* Hands over every refusal recorded so far and forgets them. Registration runs
   during static initialisation, when no host exists to be told, so the loader
   holds refusals until a caller with somewhere to put them asks. */
void disk_loader_drain_rejections(DiskDriverRejectionFn_t sink, void* context);

DiskError_e disk_loader_open(const char* image_path,
                             const DiskFormatDriver_t** out_driver,
                             void** out_instance);

/* Lists the extensions the registered drivers accept and then the container
   layer's, each once, joined by ';' and without dots (do;dsk;...;gz;zip).
   Whatever fits is copied and NUL-terminated when buffer_size is nonzero; the
   return is the length the whole list needs without its NUL, as snprintf
   reports it, so (NULL, 0) measures and a second call copies. */
size_t disk_loader_get_supported_extensions(char* out_buffer,
                                            size_t buffer_size);

/* How many drivers are registered, and the one at an index. The order is by
   driver name and an index is only valid until the next registration. */
uint32_t disk_loader_driver_count(void);
const DiskFormatDriver_t* disk_loader_driver_at(uint32_t index);

/* Make a blank image at path in the named driver's format. Refuses a path that
   already exists with disk_err_io, and removes a file it created if the driver
   fails part way. */
DiskError_e disk_loader_create(const char* path, const char* driver_name);

// NOLINTEND(modernize-use-using)

#ifdef __cplusplus
}
#endif
