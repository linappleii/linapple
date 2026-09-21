// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/BitstreamDiskImage.h"

#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskEncoding.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "core/Util_Path.h"

namespace {
constexpr uint32_t quarter_tracks_per_cylinder = 4;
constexpr uint8_t sync_byte = 0xFF;
}  // namespace

// NOLINTBEGIN(google-runtime-int, cppcoreguidelines-owning-memory, bugprone-easily-swappable-parameters, modernize-make-unique)
// Justification:
// This module uses procedural patterns for C-compatibility. google-runtime-int
// is required for fseek offsets. owning-memory and make-unique are suppressed
// for C++11 compatibility and handle-based resource management.
// easily-swappable-parameters is mandated by the Disk Driver ABI signatures.

struct BitstreamDiskImage_t {
  FilePtr_t file{nullptr, fclose};
  uint32_t data_offset = 0;
  uint32_t track_size = 0;
  bool os_readonly = false;
  std::array<uint8_t, nibbles_per_track> nibbles{};

  BitstreamDiskImage_t() = default;
  ~BitstreamDiskImage_t() = default;

  BitstreamDiskImage_t(const BitstreamDiskImage_t&) = delete;
  auto operator=(const BitstreamDiskImage_t&) -> BitstreamDiskImage_t& = delete;
  BitstreamDiskImage_t(BitstreamDiskImage_t&&) = default;
  auto operator=(BitstreamDiskImage_t&&) -> BitstreamDiskImage_t& = default;
};

extern "C" auto bitstream_disk_image_open(const char* path,
                                          uint32_t file_offset,
                                          uint32_t nibbles_per_track,
                                          bool read_only)
    -> BitstreamDiskImage_t* {
  if (path == nullptr) {
    return nullptr;
  }

  auto image_ptr =
      std::unique_ptr<BitstreamDiskImage_t>(new BitstreamDiskImage_t());

  image_ptr->os_readonly = read_only;
  if (!read_only) {
    image_ptr->file.reset(fopen(path, "r+b"));
  }

  if (image_ptr->file == nullptr) {
    image_ptr->file.reset(fopen(path, "rb"));
    image_ptr->os_readonly = true;
  }

  if (image_ptr->file == nullptr) {
    return nullptr;
  }

  image_ptr->data_offset = file_offset;
  image_ptr->track_size = nibbles_per_track;

  return image_ptr.release();
}

// Why: Destroys the bitstream image instance. The RAII FilePtr_t member
// automatically ensures the physical file is closed during destruction.
extern "C" auto bitstream_disk_image_close(BitstreamDiskImage_t* image_ptr)
    -> void {
  if (image_ptr == nullptr) {
    return;
  }
  delete image_ptr;
}

extern "C" auto bitstream_disk_image_is_write_protected(
    BitstreamDiskImage_t* image_ptr) -> bool {
  return (image_ptr != nullptr) ? image_ptr->os_readonly : true;
}

extern "C" auto bitstream_disk_image_read_track_bits(
    BitstreamDiskImage_t* image_ptr, uint32_t quarter_track, uint8_t* bits,
    uint32_t max_bits, uint32_t* out_bit_count, uint8_t* out_bit_timing)
    -> DiskError_e {
  if (image_ptr == nullptr || bits == nullptr || out_bit_count == nullptr ||
      out_bit_timing == nullptr) {
    return disk_err_invalid_argument;
  }
  *out_bit_count = 0;
  *out_bit_timing = disk_default_bit_timing;

  const uint32_t track = quarter_track / quarter_tracks_per_cylinder;
  if (track >= static_cast<uint32_t>(tracks_per_disk)) {
    return disk_err_invalid_argument;
  }

  const auto offset = static_cast<int64_t>(image_ptr->data_offset) +
                      (static_cast<int64_t>(track) *
                       static_cast<int64_t>(image_ptr->track_size));

  if (fseek(image_ptr->file.get(), static_cast<long>(offset), SEEK_SET) != 0) {
    return disk_err_io;
  }

  image_ptr->nibbles.fill(sync_byte);
  const size_t read_count = fread(image_ptr->nibbles.data(), 1,
                                  image_ptr->track_size, image_ptr->file.get());

  return disk_encoding_nibbles_to_bits(image_ptr->nibbles.data(),
                                       static_cast<uint32_t>(read_count), bits,
                                       max_bits, out_bit_count);
}

extern "C" auto bitstream_disk_image_write_track_bits(
    BitstreamDiskImage_t* image_ptr, uint32_t quarter_track,
    const uint8_t* bits, uint32_t bit_count) -> DiskError_e {
  if (image_ptr == nullptr || bits == nullptr) {
    return disk_err_invalid_argument;
  }
  if (image_ptr->os_readonly) {
    return disk_err_write_protected;
  }

  const uint32_t track = quarter_track / quarter_tracks_per_cylinder;
  if (track >= static_cast<uint32_t>(tracks_per_disk)) {
    return disk_err_invalid_argument;
  }

  uint32_t nibble_count = 0;
  const DiskError_e decoded =
      disk_encoding_bits_to_nibbles(bits, bit_count, image_ptr->nibbles.data(),
                                    image_ptr->track_size, &nibble_count);
  if (decoded != disk_err_none) {
    return decoded;
  }

  const auto offset = static_cast<int64_t>(image_ptr->data_offset) +
                      (static_cast<int64_t>(track) *
                       static_cast<int64_t>(image_ptr->track_size));

  if (fseek(image_ptr->file.get(), static_cast<long>(offset), SEEK_SET) != 0) {
    return disk_err_io;
  }
  if (fwrite(image_ptr->nibbles.data(), 1, nibble_count,
             image_ptr->file.get()) != nibble_count) {
    return disk_err_io;
  }
  fflush(image_ptr->file.get());
  return disk_err_none;
}

// Why: Generates a new, zero-filled raw bitstream image of the specified
// physical size. Used for creating blank NIB or NB2 images.
extern "C" auto bitstream_disk_image_create(const char* path,
                                            uint32_t total_size)
    -> DiskError_e {
  if (path == nullptr) {
    return disk_err_io;
  }

  FilePtr_t file{fopen(path, "wb"), fclose};
  if (file == nullptr) {
    return disk_err_io;
  }

  constexpr size_t chunk_size = 1024;
  std::array<uint8_t, chunk_size> zero{};
  zero.fill(0);

  const uint32_t full_chunks = total_size / static_cast<uint32_t>(chunk_size);
  for (uint32_t i = 0; i < full_chunks; ++i) {
    if (fwrite(zero.data(), 1, zero.size(), file.get()) != zero.size()) {
      file.reset();
      unlink(path);
      return disk_err_io;
    }
  }

  const size_t remaining_bytes = total_size % chunk_size;
  if (remaining_bytes != 0) {
    if (fwrite(zero.data(), 1, remaining_bytes, file.get()) !=
        remaining_bytes) {
      file.reset();
      unlink(path);
      return disk_err_io;
    }
  }

  if (fflush(file.get()) != 0) {
    file.reset();
    unlink(path);
    return disk_err_io;
  }

  return disk_err_none;
}

// NOLINTEND(google-runtime-int, cppcoreguidelines-owning-memory, bugprone-easily-swappable-parameters, modernize-make-unique)
