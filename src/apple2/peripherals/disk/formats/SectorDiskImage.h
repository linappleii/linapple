// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <cstdio>

#include "apple2/peripherals/disk/DiskError.h"
#include "core/Peripheral_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SectorDiskImage_t SectorDiskImage_t;

SectorDiskImage_t* SectorDiskImage_Open(const char* path, uint32_t file_offset,
                                        bool is_dos_order,
                                        uint8_t enhanced_speed,
                                        bool* out_is_read_only);

void SectorDiskImage_Close(SectorDiskImage_t* image);

bool SectorDiskImage_IsWriteProtected(SectorDiskImage_t* image);

void SectorDiskImage_ReadTrack(SectorDiskImage_t* image, int track,
                               uint8_t* track_buffer, int* out_nibbles);

void SectorDiskImage_WriteTrack(SectorDiskImage_t* image, int track,
                                const uint8_t* track_buffer, int nibbles);

DiskError_e SectorDiskImage_Create(const char* path);

PeripheralStatus SectorDiskImage_Command(SectorDiskImage_t* image,
                                         uint32_t cmd_id, const void* data,
                                         size_t size);

#ifdef __cplusplus
}
#endif
