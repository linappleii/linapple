#include "apple2/peripherals/disk/formats/PoDriver.h"
#include "apple2/peripherals/disk/formats/SectorDiskImage.h"

#include <algorithm>
#include <cstring>

// NOLINTBEGIN(bugprone-easily-swappable-parameters,cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-array-to-pointer-decay)

namespace {
constexpr uint32_t MIN_140K_DISK_SIZE = 143105;
constexpr uint32_t MAX_140K_DISK_SIZE = 143364;
constexpr uint32_t DISK_SIZE_140K_ALT2 = 143488;
constexpr size_t VTOC_OFFSET = 0x11000;
constexpr size_t PAGE_SIZE = 0x0100;
constexpr size_t PRODOS_BLOCK_SIZE = 512;

constexpr size_t SECTORS_PER_TRACK = 16;
constexpr size_t TRACK_COUNT = 35;
constexpr size_t PRODOS_DIR_BLOCK = 2;
constexpr uint16_t MAX_PRODOS_BLOCKS_140K =
    static_cast<uint16_t>((TRACK_COUNT * SECTORS_PER_TRACK) / 2);

constexpr size_t VTOC_LINK_OFFSET = 2;
constexpr int MIN_VTOC_LOOP = 5;
constexpr int MAX_VTOC_LOOP = 13;
constexpr int VTOC_CHECK_BASE = 14;
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
  const size_t min_header_size =
      (PRODOS_DIR_BLOCK * PRODOS_BLOCK_SIZE) + PAGE_SIZE + VTOC_LINK_OFFSET;
  if (header_size >= min_header_size) {
    uint16_t prev = 0;
    uint16_t next = 0;
    std::memcpy(&prev, &header[(PRODOS_DIR_BLOCK * PRODOS_BLOCK_SIZE) + PAGE_SIZE],
                sizeof(prev));
    std::memcpy(&next,
                &header[(PRODOS_DIR_BLOCK * PRODOS_BLOCK_SIZE) + PAGE_SIZE +
                        VTOC_LINK_OFFSET],
                sizeof(next));

    // In ProDOS directory block 2, prev is always 0.
    if (prev == 0 && next > static_cast<uint16_t>(PRODOS_DIR_BLOCK) &&
        next < MAX_PRODOS_BLOCKS_140K) {
      return DISK_PROBE_DEFINITE;
    }
  }

  const size_t vtoc_min_header_size =
      VTOC_OFFSET + VTOC_LINK_OFFSET + (static_cast<size_t>(MAX_VTOC_LOOP) * PAGE_SIZE);
  if (header_size >= vtoc_min_header_size) {
    bool mismatch = false;
    for (int loop = MIN_VTOC_LOOP; loop <= MAX_VTOC_LOOP; ++loop) {
      if (header[VTOC_OFFSET + VTOC_LINK_OFFSET + (static_cast<size_t>(loop) * PAGE_SIZE)] !=
          static_cast<uint8_t>(VTOC_CHECK_BASE - loop)) {
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
  *out_instance = static_cast<void*>(image);
  return DISK_ERR_NONE;
}

static void PoClose(void* instance) {
  SectorDiskImage_Close(static_cast<SectorDiskImage_t*>(instance));
}

static auto PoIsWriteProtected(void* instance) -> bool {
  return SectorDiskImage_IsWriteProtected(
      static_cast<SectorDiskImage_t*>(instance));
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
  return SectorDiskImage_Command(static_cast<SectorDiskImage_t*>(instance), cmd_id,
                                 data, size);
}

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
static const char* const g_po_creatable_exts[] = {".po", nullptr};
// NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

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

// NOLINTEND(bugprone-easily-swappable-parameters,cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
