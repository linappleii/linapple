// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/DoDriver.h"

#include <algorithm>
#include <cstring>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/formats/SectorDiskImage.h"

namespace {
constexpr uint32_t MIN_140K_DISK_SIZE = 143105;
constexpr uint32_t MAX_140K_DISK_SIZE = 143364;
constexpr uint32_t DISK_SIZE_140K_ALT1 = 143403;
constexpr uint32_t DISK_SIZE_140K_ALT2 = 143488;

constexpr int VTOC_OFFSET = 0x11000;
constexpr int PAGE_SIZE = 0x0100;
constexpr int DOS_CATALOG_START_SECTOR = 1;
constexpr int DOS_CATALOG_END_SECTOR = 15;
constexpr int DOS_NEXT_SECTOR_OFFSET = 2;

constexpr int PRODOS_BLOCK_SIZE = 512;
constexpr int PRODOS_DIR_START_BLOCK = 2;
constexpr int PRODOS_DIR_END_BLOCK = 5;
constexpr int PRODOS_DIR_LINK_OFFSET = PAGE_SIZE;
constexpr int PRODOS_NEXT_LINK_LIMIT = 6;
constexpr int PRODOS_PREV_LINK_LIMIT = 8;

inline auto ReadU16LE(const uint8_t* p) -> uint16_t {
  return static_cast<uint16_t>(p[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
}

}  // namespace

static auto DoProbe(const uint8_t* header_data, size_t header_size,
                    uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
  if (file_size < MIN_140K_DISK_SIZE || file_size > MAX_140K_DISK_SIZE) {
    if (file_size != DISK_SIZE_140K_ALT1 && file_size != DISK_SIZE_140K_ALT2) {
      return disk_probe_no;
    }
  }

  const size_t dos_vtoc_min = static_cast<size_t>(VTOC_OFFSET) +
                              static_cast<size_t>(DOS_NEXT_SECTOR_OFFSET) +
                              (static_cast<size_t>(DOS_CATALOG_END_SECTOR) *
                               static_cast<size_t>(PAGE_SIZE));
  if (header_size >= dos_vtoc_min) {
    bool mismatch = false;
    for (int loop = DOS_CATALOG_START_SECTOR; loop <= DOS_CATALOG_END_SECTOR;
         ++loop) {
      const size_t offset =
          static_cast<size_t>(VTOC_OFFSET) +
          static_cast<size_t>(DOS_NEXT_SECTOR_OFFSET) +
          (static_cast<size_t>(loop) * static_cast<size_t>(PAGE_SIZE));
      if (header_data[offset] != static_cast<uint8_t>(loop - 1)) {
        mismatch = true;
        break;
      }
    }
    if (!mismatch) {
      return disk_probe_definite;
    }
  }

  const size_t prodos_min = (static_cast<size_t>(PRODOS_DIR_END_BLOCK) *
                             static_cast<size_t>(PRODOS_BLOCK_SIZE)) +
                            static_cast<size_t>(PRODOS_DIR_LINK_OFFSET) + 2;
  if (header_size >= prodos_min) {
    bool mismatch = false;
    for (int loop = PRODOS_DIR_START_BLOCK; loop <= PRODOS_DIR_END_BLOCK;
         ++loop) {
      const size_t offset_next =
          (static_cast<size_t>(loop) * static_cast<size_t>(PRODOS_BLOCK_SIZE)) +
          static_cast<size_t>(PRODOS_DIR_LINK_OFFSET);
      const size_t offset_prev = offset_next + 2;

      const uint16_t next = ReadU16LE(&header_data[offset_next]);
      const uint16_t prev = ReadU16LE(&header_data[offset_prev]);

      if ((next !=
           ((loop == PRODOS_DIR_END_BLOCK)
                ? 0
                : static_cast<uint16_t>(PRODOS_NEXT_LINK_LIMIT - loop))) ||
          (prev !=
           ((loop == PRODOS_DIR_START_BLOCK)
                ? 0
                : static_cast<uint16_t>(PRODOS_PREV_LINK_LIMIT - loop)))) {
        mismatch = true;
        break;
      }
    }
    if (!mismatch) {
      return disk_probe_definite;
    }
  }

  if (ext_hint && (std::strcmp(ext_hint, ".do") == 0 ||
                   std::strcmp(ext_hint, ".dsk") == 0)) {
    return disk_probe_possible;
  }

  return disk_probe_possible;
}

static auto DoOpen(const char* path, uint32_t file_offset,
                   uint8_t enhanced_speed, bool* out_is_read_only,
                   void** out_instance) -> DiskError_e {
  auto* image = SectorDiskImage_Open(path, file_offset, true, enhanced_speed,
                                     out_is_read_only);
  if (!image) return disk_err_io;
  *out_instance = static_cast<void*>(image);
  return disk_err_none;
}

static void DoClose(void* instance) {
  SectorDiskImage_Close(static_cast<SectorDiskImage_t*>(instance));
}

static auto DoIsWriteProtected(void* instance) -> bool {
  return SectorDiskImage_IsWriteProtected(
      static_cast<SectorDiskImage_t*>(instance));
}

static void DoReadTrack(void* instance, int track, int phase,
                        uint8_t* track_buffer, int* out_nibbles) {
  (void)phase;
  SectorDiskImage_ReadTrack(static_cast<SectorDiskImage_t*>(instance), track,
                            track_buffer, out_nibbles);
}

static void DoWriteTrack(void* instance, int track, int phase,
                         const uint8_t* track_buffer, int nibbles) {
  (void)phase;
  SectorDiskImage_WriteTrack(static_cast<SectorDiskImage_t*>(instance), track,
                             track_buffer, nibbles);
}

static auto DoCreate(const char* path) -> DiskError_e {
  return SectorDiskImage_Create(path);
}

static auto DoCommand(void* instance, uint32_t cmd_id, const void* data,
                      size_t size) -> PeripheralStatus {
  return SectorDiskImage_Command(static_cast<SectorDiskImage_t*>(instance),
                                 cmd_id, data, size);
}

static const char* const g_do_creatable_exts[] = {".do", ".dsk", nullptr};

extern "C" const DiskFormatDriver_t g_do_driver = {
    disk_format_abi_version,
    disk_driver_cap_write,
    "DOS Order",
    g_do_creatable_exts,
    DoProbe,
    DoOpen,
    DoClose,
    DoIsWriteProtected,
    DoReadTrack,
    DoWriteTrack,
    DoCreate,
    DoCommand,
    nullptr  // read_flux_bit
};
