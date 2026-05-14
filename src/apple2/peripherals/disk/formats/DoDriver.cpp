#include "apple2/peripherals/disk/formats/DoDriver.h"
#include "apple2/peripherals/disk/formats/SectorDiskImage.h"

#include <algorithm>
#include <cstring>

namespace {
// Constants for 140K disk sizes
constexpr uint32_t MIN_140K_DISK_SIZE = 143105;
constexpr uint32_t MAX_140K_DISK_SIZE = 143364;
constexpr uint32_t DISK_SIZE_140K_ALT1 = 143403;
constexpr uint32_t DISK_SIZE_140K_ALT2 = 143488;

// DOS VTOC and sector constants
constexpr int VTOC_OFFSET = 0x11000;
constexpr int PAGE_SIZE = 0x0100;
constexpr int DOS_CATALOG_START_SECTOR = 1;
constexpr int DOS_CATALOG_END_SECTOR = 15;
constexpr int DOS_NEXT_SECTOR_OFFSET = 2;

// ProDOS constants
constexpr int PRODOS_BLOCK_SIZE = 512;
constexpr int PRODOS_DIR_START_BLOCK = 2;
constexpr int PRODOS_DIR_END_BLOCK = 5;
constexpr int PRODOS_DIR_LINK_OFFSET = PAGE_SIZE;
constexpr int PRODOS_NEXT_LINK_LIMIT = 6;
constexpr int PRODOS_PREV_LINK_LIMIT = 8;

/**
 * @brief Reads a 16-bit little-endian value from a byte buffer.
 * 
 * @param p Pointer to the buffer.
 * @return The 16-bit value.
 */
inline auto ReadU16LE(const uint8_t* p) -> uint16_t {
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-avoid-magic-numbers)
  return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
  // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-avoid-magic-numbers)
}

}  // namespace

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static auto DoProbe(const uint8_t* header, size_t header_size,
                    uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
// NOLINTEND(bugprone-easily-swappable-parameters)
  if (file_size < MIN_140K_DISK_SIZE || file_size > MAX_140K_DISK_SIZE) {
    if (file_size != DISK_SIZE_140K_ALT1 && file_size != DISK_SIZE_140K_ALT2) {
      return DISK_PROBE_NO;
    }
  }

  // DOS VTOC structure check (track 17 sector-order byte sequence)
  const size_t dos_vtoc_min = static_cast<size_t>(VTOC_OFFSET) + 
                              static_cast<size_t>(DOS_NEXT_SECTOR_OFFSET) +
                              (static_cast<size_t>(DOS_CATALOG_END_SECTOR) * 
                               static_cast<size_t>(PAGE_SIZE));
  if (header_size >= dos_vtoc_min) {
    bool mismatch = false;
    for (int loop = DOS_CATALOG_START_SECTOR; loop <= DOS_CATALOG_END_SECTOR; ++loop) {
      const size_t offset = static_cast<size_t>(VTOC_OFFSET) + 
                            static_cast<size_t>(DOS_NEXT_SECTOR_OFFSET) + 
                            (static_cast<size_t>(loop) * static_cast<size_t>(PAGE_SIZE));
      if (header[offset] != static_cast<uint8_t>(loop - 1)) { // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        mismatch = true;
        break;
      }
    }
    if (!mismatch) {
      return DISK_PROBE_DEFINITE;
    }
  }

  // ProDOS bitmap chain check as secondary heuristic
  const size_t prodos_min = (static_cast<size_t>(PRODOS_DIR_END_BLOCK) * 
                             static_cast<size_t>(PRODOS_BLOCK_SIZE)) +
                            static_cast<size_t>(PRODOS_DIR_LINK_OFFSET) + 2;
  if (header_size >= prodos_min) {
    bool mismatch = false;
    for (int loop = PRODOS_DIR_START_BLOCK; loop <= PRODOS_DIR_END_BLOCK; ++loop) {
      const size_t offset_next = (static_cast<size_t>(loop) * static_cast<size_t>(PRODOS_BLOCK_SIZE)) + 
                                  static_cast<size_t>(PRODOS_DIR_LINK_OFFSET);
      const size_t offset_prev = offset_next + 2;
      
      const uint16_t next = ReadU16LE(&header[offset_next]); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      const uint16_t prev = ReadU16LE(&header[offset_prev]); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)

      if ((next != ((loop == PRODOS_DIR_END_BLOCK) ? 0 : static_cast<uint16_t>(PRODOS_NEXT_LINK_LIMIT - loop))) ||
          (prev != ((loop == PRODOS_DIR_START_BLOCK) ? 0 : static_cast<uint16_t>(PRODOS_PREV_LINK_LIMIT - loop)))) {
        mismatch = true;
        break;
      }
    }
    if (!mismatch) {
      return DISK_PROBE_DEFINITE;
    }
  }

  if (ext_hint && (std::strcmp(ext_hint, ".do") == 0 || std::strcmp(ext_hint, ".dsk") == 0)) {
    return DISK_PROBE_POSSIBLE;
  }

  return DISK_PROBE_POSSIBLE;
}

static auto DoOpen(const char* path, uint32_t file_offset, uint8_t enhanced_speed,
                   bool* out_os_readonly, void** out_instance) -> DiskError_e {
  auto* image = SectorDiskImage_Open(path, file_offset, true, enhanced_speed,
                                     out_os_readonly);
  if (!image) return DISK_ERR_IO;
  *out_instance = static_cast<void*>(image);
  return DISK_ERR_NONE;
}

static void DoClose(void* instance) {
  SectorDiskImage_Close(static_cast<SectorDiskImage_t*>(instance));
}

static auto DoIsWriteProtected(void* instance) -> bool {
  return SectorDiskImage_IsWriteProtected(static_cast<SectorDiskImage_t*>(instance));
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static void DoReadTrack(void* instance, int track, int phase,
                        uint8_t* trackImageBuffer, int* nibbles_out) {
// NOLINTEND(bugprone-easily-swappable-parameters)
  (void)phase;
  SectorDiskImage_ReadTrack(static_cast<SectorDiskImage_t*>(instance), track,
                            trackImageBuffer, nibbles_out);
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static void DoWriteTrack(void* instance, int track, int phase,
                         const uint8_t* trackImage, int nibbles) {
// NOLINTEND(bugprone-easily-swappable-parameters)
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

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
static const char* const g_do_creatable_exts[] = {".do", ".dsk", nullptr};
// NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

extern "C" const DiskFormatDriver_t g_do_driver = {
    LINAPPLE_DISK_ABI_VERSION,
    DRIVER_CAP_WRITE,
    "DOS Order",
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
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
