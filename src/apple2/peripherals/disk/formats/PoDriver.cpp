// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/PoDriver.h"

#include <algorithm>
#include <cstring>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/formats/SectorDiskImage.h"

// NOLINTBEGIN(bugprone-easily-swappable-parameters,cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-array-to-pointer-decay)

namespace {
constexpr uint32_t MIN_140K_DISK_SIZE = 143105;
constexpr uint32_t MAX_140K_DISK_SIZE = 143364;
constexpr uint32_t DISK_SIZE_140K_ALT2 = 143488;
constexpr size_t VTOC_OFFSET = 0x11000;
constexpr size_t PAGE_SIZE = 0x0100;
constexpr size_t PRODOS_BLOCK_SIZE = 512;

constexpr size_t TRACK_COUNT = 35;
constexpr size_t PRODOS_DIR_BLOCK = 2;
constexpr uint16_t MAX_PRODOS_BLOCKS_140K =
    static_cast<uint16_t>((TRACK_COUNT * sectors_per_track) / 2);

constexpr size_t VTOC_LINK_OFFSET = 2;
constexpr int MIN_VTOC_LOOP = 5;
constexpr int MAX_VTOC_LOOP = 13;
constexpr int VTOC_CHECK_BASE = 14;
}  // namespace

static auto PoProbe(const uint8_t* header_data, size_t header_size,
                    uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
  if (file_size < MIN_140K_DISK_SIZE || file_size > MAX_140K_DISK_SIZE) {
    if (file_size != DISK_SIZE_140K_ALT2) {
      return disk_probe_no;
    }
  }

  const size_t min_header_size =
      (PRODOS_DIR_BLOCK * PRODOS_BLOCK_SIZE) + PAGE_SIZE + VTOC_LINK_OFFSET;
  if (header_size >= min_header_size) {
    uint16_t prev = 0;
    uint16_t next = 0;
    std::memcpy(
        &prev, &header_data[(PRODOS_DIR_BLOCK * PRODOS_BLOCK_SIZE) + PAGE_SIZE],
        sizeof(prev));
    std::memcpy(&next,
                &header_data[(PRODOS_DIR_BLOCK * PRODOS_BLOCK_SIZE) +
                             PAGE_SIZE + VTOC_LINK_OFFSET],
                sizeof(next));

    if (prev == 0 && next > static_cast<uint16_t>(PRODOS_DIR_BLOCK) &&
        next < MAX_PRODOS_BLOCKS_140K) {
      return disk_probe_definite;
    }
  }

  const size_t vtoc_min_header_size =
      VTOC_OFFSET + VTOC_LINK_OFFSET +
      (static_cast<size_t>(MAX_VTOC_LOOP) * PAGE_SIZE);
  if (header_size >= vtoc_min_header_size) {
    bool mismatch = false;
    for (int loop = MIN_VTOC_LOOP; loop <= MAX_VTOC_LOOP; ++loop) {
      if (header_data[VTOC_OFFSET + VTOC_LINK_OFFSET +
                      (static_cast<size_t>(loop) * PAGE_SIZE)] !=
          static_cast<uint8_t>(VTOC_CHECK_BASE - loop)) {
        mismatch = true;
        break;
      }
    }
    if (!mismatch) {
      return disk_probe_definite;
    }
  }

  if (ext_hint && strcmp(ext_hint, ".po") == 0) {
    return disk_probe_possible;
  }

  return disk_probe_possible;
}

static auto PoOpen(const char* path, uint32_t file_offset,
                   uint8_t enhanced_speed, bool* out_is_read_only,
                   void** out_instance) -> DiskError_e {
  auto* image = SectorDiskImage_Open(path, file_offset, false, enhanced_speed,
                                     out_is_read_only);
  if (!image) return disk_err_io;
  *out_instance = static_cast<void*>(image);
  return disk_err_none;
}

static void PoClose(void* instance) {
  SectorDiskImage_Close(static_cast<SectorDiskImage_t*>(instance));
}

static auto PoIsWriteProtected(void* instance) -> bool {
  return SectorDiskImage_IsWriteProtected(
      static_cast<SectorDiskImage_t*>(instance));
}

static void PoReadTrack(void* instance, int track, int phase,
                        uint8_t* track_buffer, int* out_nibbles) {
  (void)phase;
  SectorDiskImage_ReadTrack(static_cast<SectorDiskImage_t*>(instance), track,
                            track_buffer, out_nibbles);
}

static void PoWriteTrack(void* instance, int track, int phase,
                         const uint8_t* track_buffer, int nibbles) {
  (void)phase;
  SectorDiskImage_WriteTrack(static_cast<SectorDiskImage_t*>(instance), track,
                             track_buffer, nibbles);
}

static auto PoCreate(const char* path) -> DiskError_e {
  return SectorDiskImage_Create(path);
}

static auto PoCommand(void* instance, uint32_t cmd_id, const void* data,
                      size_t size) -> PeripheralStatus {
  return SectorDiskImage_Command(static_cast<SectorDiskImage_t*>(instance),
                                 cmd_id, data, size);
}

static const char* const g_po_creatable_exts[] = {".po", nullptr};

extern "C" const DiskFormatDriver_t g_po_driver = {disk_format_abi_version,
                                                   disk_driver_cap_write,
                                                   "ProDOS Order",
                                                   g_po_creatable_exts,
                                                   PoProbe,
                                                   PoOpen,
                                                   PoClose,
                                                   PoIsWriteProtected,
                                                   PoReadTrack,
                                                   PoWriteTrack,
                                                   PoCreate,
                                                   PoCommand,
                                                   nullptr};

// NOLINTEND(bugprone-easily-swappable-parameters,cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
