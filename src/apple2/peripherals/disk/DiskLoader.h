/*
 * DiskLoader.h - Centralised disk image loading and format detection
 */

#pragma once

#include "apple2/peripherals/disk/DiskFormatDriver.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialise the disk loader and register built-in drivers.
 */
void DiskLoader_Init(void);

/*
 * Clean up the disk loader and unregister all drivers.
 */
void DiskLoader_Shutdown(void);

/*
 * Register a custom format driver. The loader takes ownership of the driver
 * pointer if it was dynamically allocated (drivers should ideally be static).
 */
void DiskLoader_Register(DiskFormatDriver_t* driver);

/*
 * Open a disk image. Handles decompression and format probing.
 *
 * filename: Path to the image file (may be .gz or .zip).
 * bCreateIfNecessary: If true, create a new image if it doesn't exist.
 * pWriteProtected: IN/OUT. Forced if TRUE on input or if file is read-only.
 * out_driver: OUT. The driver that claimed the image.
 * out_instance: OUT. The driver-specific instance handle.
 *
 * Returns DISK_ERR_NONE on success.
 */
DiskError_e DiskLoader_Open(const char* filename, bool bCreateIfNecessary,
                            uint8_t enhanced_speed, bool* pWriteProtected,
                            DiskFormatDriver_t** out_driver,
                            void** out_instance);

#ifdef __cplusplus
}
#endif
