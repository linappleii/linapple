// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/BitstreamDiskImage.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

#include "apple2/Apple2Types.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

// NOLINTBEGIN(google-runtime-int, cppcoreguidelines-owning-memory,
//             bugprone-easily-swappable-parameters, modernize-make-unique)
// Justification: This module uses procedural patterns for C-compatibility.
// google-runtime-int is required for fseek offsets. owning-memory and
// make-unique are suppressed for C++11 compatibility and handle-based
// resource management. easily-swappable-parameters is mandated by the
// Disk Driver ABI signatures.

struct BitstreamDiskImage_t {
  FilePtr_t file{nullptr, fclose};
  uint32_t data_offset = 0;
  uint32_t track_size = 0;
  bool os_readonly = false;

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
                                          bool* out_is_read_only)
    -> BitstreamDiskImage_t* {
  if (path == nullptr) {
    return nullptr;
  }

  auto image_ptr =
      std::unique_ptr<BitstreamDiskImage_t>(new BitstreamDiskImage_t());

  // 1. Attempt Read/Write acquisition
  image_ptr->file.reset(fopen(path, "r+b"));
  image_ptr->os_readonly = false;

  // 2. Fallback to Read-Only if Write access was denied
  if (image_ptr->file == nullptr) {
    image_ptr->file.reset(fopen(path, "rb"));
    image_ptr->os_readonly = true;
  }

  // 3. Return null if both attempts failed
  if (image_ptr->file == nullptr) {
    return nullptr;
  }

  if (out_is_read_only != nullptr) {
    *out_is_read_only = image_ptr->os_readonly;
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

extern "C" auto bitstream_disk_image_read_track(BitstreamDiskImage_t* image_ptr,
                                                int track,
                                                uint8_t* track_buffer,
                                                int* out_nibbles) -> void {
  if (image_ptr == nullptr || track_buffer == nullptr || track < 0 ||
      track >= tracks_per_disk) {
    if (out_nibbles != nullptr) {
      *out_nibbles = 0;
    }
    return;
  }

  const auto offset = static_cast<int64_t>(image_ptr->data_offset) +
                      (static_cast<int64_t>(track) *
                       static_cast<int64_t>(image_ptr->track_size));

  if (fseek(image_ptr->file.get(), static_cast<long>(offset), SEEK_SET) != 0) {
    if (out_nibbles != nullptr) {
      *out_nibbles = 0;
    }
    return;
  }

  const size_t read_count =
      fread(track_buffer, 1, image_ptr->track_size, image_ptr->file.get());

  if (out_nibbles != nullptr) {
    *out_nibbles = static_cast<int>(read_count);
  }
}

extern "C" auto bitstream_disk_image_write_track(
    BitstreamDiskImage_t* image_ptr, int track, const uint8_t* track_buffer,
    int nibbles) -> void {
  if (image_ptr == nullptr || track_buffer == nullptr ||
      image_ptr->os_readonly || track < 0 || track >= tracks_per_disk) {
    return;
  }

  const auto offset = static_cast<int64_t>(image_ptr->data_offset) +
                      (static_cast<int64_t>(track) *
                       static_cast<int64_t>(image_ptr->track_size));

  if (fseek(image_ptr->file.get(), static_cast<long>(offset), SEEK_SET) == 0) {
    (void)fwrite(track_buffer, 1, static_cast<size_t>(nibbles),
                 image_ptr->file.get());
  }
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
    (void)fwrite(zero.data(), 1, zero.size(), file.get());
  }

  const size_t remaining_bytes = total_size % chunk_size;
  if (remaining_bytes != 0) {
    (void)fwrite(zero.data(), 1, remaining_bytes, file.get());
  }

  return disk_err_none;
}

// NOLINTEND(google-runtime-int, cppcoreguidelines-owning-memory,
//           bugprone-easily-swappable-parameters, modernize-make-unique)
