#include "apple2/peripherals/disk/formats/PoDriver.h"
#include "apple2/peripherals/disk/formats/SectorDiskImage.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr int MIN_140K_DISK_SIZE = 143105;
constexpr int MAX_140K_DISK_SIZE = 143364;
constexpr int DISK_SIZE_140K_ALT2 = 143488;
constexpr int VTOC_OFFSET = 0x11000;
constexpr int PAGE_SIZE = 0x0100;
constexpr int PRODOS_BLOCK_SIZE = 512;
}  // namespace

static auto PoProbe(const uint8_t* header, size_t header_size,
                    uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
  // PO probe accepts DISK_SIZE_140K_ALT2 but NOT DISK_SIZE_140K_ALT1
  if (file_size < MIN_140K_DISK_SIZE || file_size > MAX_140K_DISK_SIZE) {
    if (file_size != DISK_SIZE_140K_ALT2) {
      return DISK_PROBE_NO;
    }
  }

  // ProDOS directory chain check on track 0, starting at block 2 (Sectors 4,5
  // of Track 0). A typical ProDOS directory header at block 2, sector 4 (0x400)
  // has next/prev pointers.
  if (header_size >=
      static_cast<size_t>((2 * PRODOS_BLOCK_SIZE) + PAGE_SIZE + 2)) {
    uint16_t prev = *reinterpret_cast<const uint16_t*>(
        header + (2 * PRODOS_BLOCK_SIZE) + PAGE_SIZE);
    uint16_t next = *reinterpret_cast<const uint16_t*>(
        header + (2 * PRODOS_BLOCK_SIZE) + PAGE_SIZE + 2);
    // In ProDOS directory block 2, prev is always 0.
    if (prev == 0 && next > 2 && next < 35 * 16 / 2) {
      return DISK_PROBE_DEFINITE;
    }
  }

  if (header_size >= static_cast<size_t>(VTOC_OFFSET + 2 + (13 * PAGE_SIZE))) {
    bool mismatch = false;
    for (int loop = 5; loop <= 13; ++loop) {
      if (header[VTOC_OFFSET + 2 + (loop * PAGE_SIZE)] != 14 - loop) {
        mismatch = true;
        break;
      }
    }
    if (!mismatch) {
      return DISK_PROBE_DEFINITE;
    }
  }

  if (ext_hint && strcmp(ext_hint, ".po") == 0) {
    return DISK_PROBE_POSSIBLE;
  }

  return DISK_PROBE_POSSIBLE;  // Correct size is often enough to be "possible"
}

static auto PoOpen(const char* path, uint32_t file_offset, uint8_t enhanced_speed,
                   bool* out_os_readonly, void** out_instance) -> DiskError_e {
  auto* image = SectorDiskImage_Open(path, file_offset, false, enhanced_speed,
                                     out_os_readonly);
  if (!image) return DISK_ERR_IO;
  *out_instance = reinterpret_cast<void*>(image);
  return DISK_ERR_NONE;
}

static void PoClose(void* instance) {
  SectorDiskImage_Close(static_cast<SectorDiskImage_t*>(instance));
}

static auto PoIsWriteProtected(void* instance) -> bool {
  return SectorDiskImage_IsWriteProtected(static_cast<SectorDiskImage_t*>(instance));
}

static void PoReadTrack(void* instance, int track, int phase,
                        uint8_t* trackImageBuffer, int* nibbles_out) {
  (void)phase;
  SectorDiskImage_ReadTrack(static_cast<SectorDiskImage_t*>(instance), track,
                            trackImageBuffer, nibbles_out);
}

static void PoWriteTrack(void* instance, int track, int phase,
                         const uint8_t* trackImage, int nibbles) {
  (void)phase;
  SectorDiskImage_WriteTrack(static_cast<SectorDiskImage_t*>(instance), track,
                             trackImage, nibbles);
}

static auto PoCreate(const char* path) -> DiskError_e {
  return SectorDiskImage_Create(path);
}

static auto PoCommand(void* instance, uint32_t cmd_id, const void* data,
                     size_t size) -> PeripheralStatus {
  return SectorDiskImage_Command(static_cast<SectorDiskImage_t*>(instance), cmd_id, data, size);
}

static const char* const g_po_creatable_exts[] = {".po", nullptr};

extern "C" const DiskFormatDriver_t g_po_driver = {
    LINAPPLE_DISK_ABI_VERSION,
    DRIVER_CAP_WRITE,
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
    nullptr  // read_flux_bit
};
