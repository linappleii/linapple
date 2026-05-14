#include "apple2/peripherals/disk/formats/IieDriver.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "apple2/peripherals/disk/DiskGCR.h"
#include "core/Common.h"

// This file implements a disk format driver that follows a specific C-style ABI.
// We use special directives to suppress warnings that are inherent
// to this architectural pattern or the underlying standard C library functions used.
// NOLINTBEGIN(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay, google-runtime-int, cppcoreguidelines-owning-memory, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access, cppcoreguidelines-pro-bounds-constant-array-index)

namespace {
// SIMSYSTEM_IIE signature: "SIMSYSTEM_IIE"
static constexpr std::array<uint8_t, 13> IIE_SIGNATURE = {
    'S', 'I', 'M', 'S', 'Y', 'S', 'T', 'E', 'M', '_', 'I', 'I', 'E'};
constexpr size_t IIE_SIGNATURE_LEN = 13;
constexpr int IIE_HEADER_SIZE = 88;
constexpr int IIE_TRACK_DATA_OFFSET = 30;
constexpr int SECTORS_PER_TRACK_16 = 16;
constexpr int DOS_TRACK_SIZE = 4096;
constexpr int IIE_TRACKS = 35;
constexpr int NIBBLES_PER_TRACK = 6656;

constexpr int IIE_VARIANT_OFFSET = 13;
constexpr int IIE_SECTOR_MAP_OFFSET = 14;
constexpr int IIE_NIBBLE_MAP_OFFSET = 14;

constexpr uint8_t IIE_VARIANT_MAX_LEGACY = 2;
constexpr uint8_t IIE_VARIANT_MAX_TOTAL = 3;
constexpr uint8_t SECTOR_NOT_FOUND = 0xFF;

struct IieInstance {
  FilePtr file{nullptr, fclose};
  std::array<uint8_t, IIE_HEADER_SIZE> header{};
  std::array<uint8_t, SECTORS_PER_TRACK_16> sector_order{};
  std::array<uint8_t, GCR_WORKBUF_SIZE> work_buffer{};
  bool os_readonly = false;

  IieInstance() = default;
  ~IieInstance() = default;

  IieInstance(const IieInstance&) = delete;
  auto operator=(const IieInstance&) -> IieInstance& = delete;
  IieInstance(IieInstance&&) = delete;
  auto operator=(IieInstance&&) -> IieInstance& = delete;
};

static void IieConvertSectorOrder(const uint8_t* sourceorder,
                                  uint8_t* sector_order) {
  for (int loop = 0; loop < SECTORS_PER_TRACK_16; ++loop) {
    uint8_t found = SECTOR_NOT_FOUND;
    for (int loop2 = 0; loop2 < SECTORS_PER_TRACK_16; ++loop2) {
      if (sourceorder[loop2] == static_cast<uint8_t>(loop)) {
        found = static_cast<uint8_t>(loop2);
        break;
      }
    }
    // Default to sector 0 if not found in map
    sector_order[loop] = (found == SECTOR_NOT_FOUND) ? 0 : found;
  }
}

static auto IieProbe(const uint8_t* header, size_t header_size,
                     uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
  (void)file_size;
  (void)ext_hint;

  if (header_size > static_cast<size_t>(IIE_VARIANT_OFFSET)) {
    if (memcmp(header, IIE_SIGNATURE.data(), IIE_SIGNATURE_LEN) == 0 &&
        header[IIE_VARIANT_OFFSET] <= IIE_VARIANT_MAX_TOTAL) {
      return DISK_PROBE_DEFINITE;
    }
  }

  return DISK_PROBE_NO;
}

static auto IieOpen(const char* path, uint32_t file_offset, uint8_t enhanced_speed,
                    bool* out_os_readonly, void** out_instance) -> DiskError_e {
  (void)file_offset;
  (void)enhanced_speed;

  std::unique_ptr<IieInstance> instance(new IieInstance());
  instance->file.reset(fopen(path, "r+b"));
  if (instance->file != nullptr) {
    instance->os_readonly = false;
  } else {
    instance->file.reset(fopen(path, "rb"));
    if (instance->file != nullptr) {
      instance->os_readonly = true;
    } else {
      return DISK_ERR_IO;
    }
  }

  if (out_os_readonly != nullptr) {
    *out_os_readonly = instance->os_readonly;
  }

  if (fread(instance->header.data(), 1, IIE_HEADER_SIZE, instance->file.get()) !=
      IIE_HEADER_SIZE) {
    return DISK_ERR_IO;
  }

  if (instance->header[IIE_VARIANT_OFFSET] <= IIE_VARIANT_MAX_LEGACY) {
    IieConvertSectorOrder(&instance->header[IIE_SECTOR_MAP_OFFSET],
                          instance->sector_order.data());
  }

  *out_instance = reinterpret_cast<void*>(instance.release());
  return DISK_ERR_NONE;
}

static void IieClose(void* instance) {
  delete reinterpret_cast<IieInstance*>(instance);
}

static auto IieIsWriteProtected(void* instance) -> bool {
  (void)instance;
  return false;
}

static inline auto ReadU16LE(const uint8_t* p) -> uint16_t {
  constexpr int BITS_PER_BYTE = 8;
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << BITS_PER_BYTE));
}

static void IieReadTrack(void* instance, int track, int phase,
                         uint8_t* trackImageBuffer, int* nibbles_out) {
  (void)phase;
  auto* ii = reinterpret_cast<IieInstance*>(instance);

  if (track < 0 || track >= IIE_TRACKS) {
    *nibbles_out = 0;
    return;
  }

  if (ii->header[IIE_VARIANT_OFFSET] <= IIE_VARIANT_MAX_LEGACY) {
    std::fill(ii->work_buffer.begin(), ii->work_buffer.end(), 0);
    if (fseek(ii->file.get(),
              static_cast<long>(static_cast<size_t>(track) * DOS_TRACK_SIZE +
                                IIE_TRACK_DATA_OFFSET),
              SEEK_SET) != 0) {
      *nibbles_out = 0;
      return;
    }
    if (fread(ii->work_buffer.data(), 1, DOS_TRACK_SIZE, ii->file.get()) !=
        DOS_TRACK_SIZE) {
      *nibbles_out = 0;
      return;
    }
    *nibbles_out = static_cast<int>(
        GCR_NibblizeTrackCustomOrder(ii->work_buffer.data(), trackImageBuffer,
                                     ii->sector_order.data(), track));
  } else {
    // Pre-nibblized variant
    uint16_t nib_count = ReadU16LE(
        &ii->header[static_cast<size_t>(track << 1) + IIE_NIBBLE_MAP_OFFSET]);

    if (nib_count > NIBBLES_PER_TRACK) {
      nib_count = NIBBLES_PER_TRACK;
    }

    uint32_t offset = IIE_HEADER_SIZE;
    for (int t = 0; t < track; ++t) {
      uint16_t prev_nib_count = ReadU16LE(
          &ii->header[static_cast<size_t>(t << 1) + IIE_NIBBLE_MAP_OFFSET]);
      // Use a reasonable upper bound for previous track sizes.
      // SIMSYSTEM_IIE tracks should not exceed NIBBLES_PER_TRACK.
      if (prev_nib_count > NIBBLES_PER_TRACK) {
        prev_nib_count = NIBBLES_PER_TRACK;
      }
      offset += prev_nib_count;
    }
    if (fseek(ii->file.get(), static_cast<long>(offset), SEEK_SET) != 0) {
      *nibbles_out = 0;
      return;
    }
    *nibbles_out =
        static_cast<int>(fread(trackImageBuffer, 1, nib_count, ii->file.get()));
  }
}

static void IieWriteTrack(void* instance, int track, int phase,
                          const uint8_t* trackImage, int nibbles) {
  (void)instance;
  (void)track;
  (void)phase;
  (void)trackImage;
  (void)nibbles;
  // Write is intentionally not implemented for IIE
}

}  // namespace

extern "C" const DiskFormatDriver_t g_iie_driver = {
    LINAPPLE_DISK_ABI_VERSION,
    0,  // capabilities (no write)
    "IIE",
    nullptr,  // creatable_exts
    IieProbe,
    IieOpen,
    IieClose,
    IieIsWriteProtected,
    IieReadTrack,
    IieWriteTrack,
    nullptr,  // create
    nullptr,  // command
    nullptr   // read_flux_bit
};

// NOLINTEND(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay, google-runtime-int, cppcoreguidelines-owning-memory, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access, cppcoreguidelines-pro-bounds-constant-array-index)
