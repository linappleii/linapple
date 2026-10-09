// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"

#ifdef __cplusplus
extern "C" {
#endif

// NOLINTBEGIN(modernize-use-using, modernize-deprecated-headers, modernize-use-trailing-return-type, readability-identifier-naming)
// Justification: This header defines a language-neutral C ABI for the hard
// disk image loader.

/* Two contracts hold across everything below.
   Ownership: an open image is the caller's, released by passing it to the
   driver's own close; a driver pointer is never the caller's, it names storage
   its translation unit keeps for the life of the process.
   Threading: registration runs during static initialisation; every other
   entry point runs on the emulation thread and nowhere else.
   The registry is a plain list with no lock around it. */

/* Registering the same descriptor twice is a silent no-op. A different driver
   whose name is already registered is refused and the refusal recorded: the
   name is how an ambiguous image is settled, so it must pick exactly one. */
void harddisk_loader_register(const HarddiskFormatDriver_t* driver);

/* Restores the drivers that registered themselves and forgets everything else,
   refusals and notes included. Test-only: a suite that pushes a synthetic
   driver in needs a way back to a known state. Production code never calls
   it. */
void harddisk_loader_reset(void);

/* Reports one driver the loader refused, or one note about an image it
   opened: the subject is the driver's name, the text says what happened. */
typedef void (*HarddiskDriverRejectionFn_t)(void* context,
                                            const char* driver_name,
                                            const char* reason);

/* Hands over every refusal and note recorded so far and forgets them.
   Registration runs during static initialisation, when no host exists to be
   told, so the loader holds refusals until a caller with somewhere to put
   them asks; a note left by an open is drained the same way. */
void harddisk_loader_drain_rejections(HarddiskDriverRejectionFn_t sink,
                                      void* context);

/* Records a note for the next drain. A backend with no host of its own says
   through it what it could not say otherwise. */
void harddisk_loader_note(const char* subject, const char* text);

/* Opens the image at image_path, archives unwrapped, and hands back the
   driver that claimed it and the instance it opened. Both outputs are nulled
   first. A file no driver claims is harddisk_err_invalid_format; a file that
   does not exist harddisk_err_not_found. */
HarddiskError_e harddisk_loader_open(const char* image_path,
                                     const HarddiskFormatDriver_t** out_driver,
                                     void** out_instance);

/* Lists the extensions the registered drivers accept and then the container
   layer's, each once, joined by ';' and without dots (2mg;...;gz;zip).
   Whatever fits is copied and NUL-terminated when buffer_size is nonzero; the
   return is the length the whole list needs without its NUL, as snprintf
   reports it, so (NULL, 0) measures and a second call copies. */
size_t harddisk_loader_get_supported_extensions(char* out_buffer,
                                                size_t buffer_size);

/* The order is by driver name and an index is only valid until the next
   registration. */
uint32_t harddisk_loader_driver_count(void);
const HarddiskFormatDriver_t* harddisk_loader_driver_at(uint32_t index);

// NOLINTEND(modernize-use-using, modernize-deprecated-headers, modernize-use-trailing-return-type, readability-identifier-naming)

#ifdef __cplusplus
}
#endif
