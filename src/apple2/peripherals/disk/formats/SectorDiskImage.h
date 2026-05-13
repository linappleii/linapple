#pragma once

#include <cstdint>
#include <cstdio>
#include "apple2/peripherals/disk/DiskError.h"
#include "core/Peripheral_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Domain: Sector-Based Floppy Disk Images (.dsk, .do, .po)
 * 
 * Encapsulates the logic for reading and writing track data to flat 
 * sector-ordered files.
 */
typedef struct SectorDiskImage_t SectorDiskImage_t;

SectorDiskImage_t* SectorDiskImage_Open(const char* path, uint32_t file_offset,
                                        bool is_dos_order, uint8_t enhanced_speed,
                                        bool* out_os_readonly);

void SectorDiskImage_Close(SectorDiskImage_t* image);

bool SectorDiskImage_IsWriteProtected(SectorDiskImage_t* image);

void SectorDiskImage_ReadTrack(SectorDiskImage_t* image, int track,
                               uint8_t* trackImageBuffer, int* nibbles_out);

void SectorDiskImage_WriteTrack(SectorDiskImage_t* image, int track,
                                const uint8_t* trackImage, int nibbles);

DiskError_e SectorDiskImage_Create(const char* path);

PeripheralStatus SectorDiskImage_Command(SectorDiskImage_t* image, uint32_t cmd_id,
                                        const void* data, size_t size);

#ifdef __cplusplus
}
#endif
