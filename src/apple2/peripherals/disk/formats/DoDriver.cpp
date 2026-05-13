#include "apple2/peripherals/disk/formats/DoDriver.h"
#include "apple2/peripherals/disk/formats/SectorDiskImage.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr int MIN_140K_DISK_SIZE = 143105;
constexpr int MAX_140K_DISK_SIZE = 143364;
constexpr int DISK_SIZE_140K_ALT1 = 143403;
constexpr int DISK_SIZE_140K_ALT2 = 143488;
constexpr int VTOC_OFFSET = 0x11000;
constexpr int PAGE_SIZE = 0x0100;
constexpr int PRODOS_BLOCK_SIZE = 512;
}  // namespace

static auto DoProbe(const uint8_t* header, size_t header_size,
                    uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
  if (file_size < MIN_140K_DISK_SIZE || file_size > MAX_140K_DISK_SIZE) {
    if (file_size != DISK_SIZE_140K_ALT1 && file_size != DISK_SIZE_140K_ALT2) {
      return DISK_PROBE_NO;
    }
  }

  // DOS VTOC structure check (track 17 sector-order byte sequence)
  if (header_size >= static_cast<size_t>(VTOC_OFFSET + 2 + (15 * PAGE_SIZE))) {
    bool mismatch = false;
    for (int loop = 1; loop <= 15; ++loop) {
      if (header[VTOC_OFFSET + 2 + (loop * PAGE_SIZE)] != loop - 1) {
        mismatch = true;
        break;
      }
    }
    if (!mismatch) {
      return DISK_PROBE_DEFINITE;
    }
  }

  // ProDOS bitmap chain check as secondary heuristic
  if (header_size >=
      static_cast<size_t>((5 * PRODOS_BLOCK_SIZE) + PAGE_SIZE + 2)) {
    bool mismatch = false;
    for (int loop = 2; loop <= 5; ++loop) {
      uint16_t next = *reinterpret_cast<const uint16_t*>(
          header + (loop * PRODOS_BLOCK_SIZE) + PAGE_SIZE);
      uint16_t prev = *reinterpret_cast<const uint16_t*>(
          header + (loop * PRODOS_BLOCK_SIZE) + PAGE_SIZE + 2);
      if ((next != ((loop == 5) ? 0 : 6 - loop)) ||
          (prev != ((loop == 2) ? 0 : 8 - loop))) {
        mismatch = true;
        break;
      }
    }
    if (!mismatch) {
      return DISK_PROBE_DEFINITE;
    }
  }

  if (ext_hint && (strcmp(ext_hint, ".do") == 0 || strcmp(ext_hint, ".dsk") == 0)) {
    return DISK_PROBE_POSSIBLE;
  }

  return DISK_PROBE_POSSIBLE;
}

static auto DoOpen(const char* path, uint32_t file_offset, uint8_t enhanced_speed,
                   bool* out_os_readonly, void** out_instance) -> DiskError_e {
  auto* image = SectorDiskImage_Open(path, file_offset, true, enhanced_speed,
                                     out_os_readonly);
  if (!image) return DISK_ERR_IO;
  *out_instance = reinterpret_cast<void*>(image);
  return DISK_ERR_NONE;
}

static void DoClose(void* instance) {
  SectorDiskImage_Close(static_cast<SectorDiskImage_t*>(instance));
}

static auto DoIsWriteProtected(void* instance) -> bool {
  return SectorDiskImage_IsWriteProtected(static_cast<SectorDiskImage_t*>(instance));
}

static void DoReadTrack(void* instance, int track, int phase,
                        uint8_t* trackImageBuffer, int* nibbles_out) {
  (void)phase;
  SectorDiskImage_ReadTrack(static_cast<SectorDiskImage_t*>(instance), track,
                            trackImageBuffer, nibbles_out);
}

static void DoWriteTrack(void* instance, int track, int phase,
                         const uint8_t* trackImage, int nibbles) {
  (void)phase;
  SectorDiskImage_WriteTrack(static_cast<SectorDiskImage_t*>(instance), track,
                             trackImage, nibbles);
}

static auto DoCreate(const char* path) -> DiskError_e {
  return SectorDiskImage_Create(path);
}

static auto DoCommand(void* instance, uint32_t cmd_id, const void* data,
                     size_t size) -> PeripheralStatus {
  return SectorDiskImage_Command(static_cast<SectorDiskImage_t*>(instance), cmd_id, data, size);
}

static const char* const g_do_creatable_exts[] = {".do", ".dsk", nullptr};

extern "C" const DiskFormatDriver_t g_do_driver = {
    LINAPPLE_DISK_ABI_VERSION,
    DRIVER_CAP_WRITE,
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
