#include "apple2/peripherals/disk/formats/SectorDiskImage.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskGCR.h"

// NOLINTBEGIN(cppcoreguidelines-pro-type-static-cast-downcast,google-runtime-int,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-owning-memory)
// Rationale: fseek requires 'long'. Array to pointer decay is common in C-like procedural code.
// FILE* management is handled within the SectorDiskImage_t lifecycle.

struct SectorDiskImage_t {
  FILE* file = nullptr;
  uint32_t macbinary_offset = 0;
  bool os_readonly = false;
  bool is_dos_order = false;
  bool is_enhanced = false;
  std::array<uint8_t, GCR_WORKBUF_SIZE> work_buffer{};

  SectorDiskImage_t() = default;
  ~SectorDiskImage_t() {
    if (file != nullptr) {
      fclose(file);
    }
  }

  SectorDiskImage_t(const SectorDiskImage_t&) = delete;
  auto operator=(const SectorDiskImage_t&) -> SectorDiskImage_t& = delete;
  SectorDiskImage_t(SectorDiskImage_t&&) = delete;
  auto operator=(SectorDiskImage_t&&) -> SectorDiskImage_t& = delete;
};

namespace {
constexpr int DOS_TRACK_SIZE = 4096;
constexpr int DISK_SIZE_140K = 143360;
constexpr int CREATE_BUFFER_SIZE = 1024;
constexpr uint8_t SYNC_BYTE = 0xFF;
}  // namespace

auto SectorDiskImage_Open(const char* path, uint32_t file_offset,
                          bool is_dos_order, uint8_t enhanced_speed,
                          bool* out_os_readonly) -> SectorDiskImage_t* {
  auto* image = new SectorDiskImage_t();

  image->file = fopen(path, "r+b");
  if (image->file != nullptr) {
    image->os_readonly = false;
  } else {
    image->file = fopen(path, "rb");
    if (image->file != nullptr) {
      image->os_readonly = true;
    } else {
      delete image;
      return nullptr;
    }
  }

  if (out_os_readonly != nullptr) {
    *out_os_readonly = image->os_readonly;
  }
  image->macbinary_offset = file_offset;
  image->is_dos_order = is_dos_order;
  image->is_enhanced = (enhanced_speed != 0);

  return image;
}

void SectorDiskImage_Close(SectorDiskImage_t* image) { delete image; }

auto SectorDiskImage_IsWriteProtected(SectorDiskImage_t* image) -> bool {
  return (image != nullptr) ? image->os_readonly : true;
}

void SectorDiskImage_ReadTrack(SectorDiskImage_t* image, int track,
                               uint8_t* trackImageBuffer, int* nibbles_out) {
  if (image == nullptr || track < 0 || track >= TRACKS) {
    if (nibbles_out != nullptr) {
      *nibbles_out = 0;
    }
    return;
  }

  // Pre-fill with sync bytes
  memset(trackImageBuffer, SYNC_BYTE, NIBBLES_PER_TRACK);

  image->work_buffer.fill(0);
  auto offset = static_cast<int64_t>(image->macbinary_offset) +
                (static_cast<int64_t>(track) * DOS_TRACK_SIZE);

  if (fseek(image->file, static_cast<long>(offset), SEEK_SET) != 0) {
    if (nibbles_out != nullptr) {
      *nibbles_out = 0;
    }
    return;
  }

  if (fread(image->work_buffer.data(), 1, DOS_TRACK_SIZE, image->file) !=
      DOS_TRACK_SIZE) {
    if (nibbles_out != nullptr) {
      *nibbles_out = 0;
    }
    return;
  }

  uint32_t nibbles = GCR_NibblizeTrack(image->work_buffer.data(),
                                       trackImageBuffer, image->is_dos_order, track);

  if (!image->is_enhanced) {
    GCR_SkewTrack(image->work_buffer.data(), track, static_cast<int>(nibbles),
                  trackImageBuffer);
  }

  if (nibbles_out != nullptr) {
    *nibbles_out = NIBBLES_PER_TRACK;
  }
}

void SectorDiskImage_WriteTrack(SectorDiskImage_t* image, int track,
                                const uint8_t* trackImage, int nibbles) {
  if (image == nullptr || image->os_readonly || track < 0 || track >= TRACKS) {
    return;
  }

  image->work_buffer.fill(0);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  GCR_DenibblizeTrack(image->work_buffer.data(), const_cast<uint8_t*>(trackImage),
                      image->is_dos_order, nibbles);

  auto offset = static_cast<int64_t>(image->macbinary_offset) +
                (static_cast<int64_t>(track) * DOS_TRACK_SIZE);

  if (fseek(image->file, static_cast<long>(offset), SEEK_SET) == 0) {
    (void)fwrite(image->work_buffer.data(), 1, DOS_TRACK_SIZE, image->file);
  }
}

auto SectorDiskImage_Create(const char* path) -> DiskError_e {
  FILE* f = fopen(path, "wb");
  if (f == nullptr) {
    return DISK_ERR_IO;
  }

  std::array<uint8_t, CREATE_BUFFER_SIZE> zero{};
  zero.fill(0);
  for (int i = 0; i < DISK_SIZE_140K / CREATE_BUFFER_SIZE; ++i) {
    (void)fwrite(zero.data(), 1, zero.size(), f);
  }
  (void)fclose(f);
  return DISK_ERR_NONE;
}

auto SectorDiskImage_Command(SectorDiskImage_t* image, uint32_t cmd_id,
                             const void* data, size_t size) -> PeripheralStatus {
  if (image == nullptr) {
    return PERIPHERAL_ERROR;
  }

  if (cmd_id == DISK_DRIVER_CMD_SET_ENHANCED_SPEED) {
    if (size < 1) {
      return PERIPHERAL_ERROR;
    }
    image->is_enhanced = (*static_cast<const uint8_t*>(data) != 0);
    return PERIPHERAL_OK;
  }
  return PERIPHERAL_INCOMPATIBLE;
}

// NOLINTEND(cppcoreguidelines-pro-type-static-cast-downcast,google-runtime-int,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-owning-memory)
