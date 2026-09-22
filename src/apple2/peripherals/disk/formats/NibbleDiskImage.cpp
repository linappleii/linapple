// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/NibbleDiskImage.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskEncoding.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "core/Util_Path.h"

namespace {
constexpr uint8_t sync_byte = 0xFF;
constexpr size_t woz_magic_len = 4;
constexpr size_t sector_bytes = 256;

auto has_woz_magic(const uint8_t* header_data, size_t header_size) -> bool {
  if (header_size < woz_magic_len) {
    return false;
  }
  return memcmp(header_data, "WOZ1", woz_magic_len) == 0 ||
         memcmp(header_data, "WOZ2", woz_magic_len) == 0;
}
}  // namespace

// NOLINTBEGIN(google-runtime-int, cppcoreguidelines-owning-memory, bugprone-easily-swappable-parameters, modernize-make-unique)
// Justification:
// This module uses procedural patterns for C-compatibility. google-runtime-int
// is required for fseek offsets. owning-memory and make-unique are suppressed
// for C++11 compatibility and handle-based resource management.
// easily-swappable-parameters is mandated by the Disk Driver ABI signatures.

struct NibbleDiskImage_t {
  FilePtr_t file{nullptr, fclose};
  uint32_t data_offset = 0;
  uint32_t track_size = 0;
  uint32_t track_count = 0;
  bool os_readonly = false;
  // One byte over the slot, so a stream one nibble too long is caught by the
  // count rather than silently cut to fit.
  std::array<uint8_t, nibbles_per_track + 1> nibbles{};

  NibbleDiskImage_t() = default;
  ~NibbleDiskImage_t() = default;

  NibbleDiskImage_t(const NibbleDiskImage_t&) = delete;
  auto operator=(const NibbleDiskImage_t&) -> NibbleDiskImage_t& = delete;
  NibbleDiskImage_t(NibbleDiskImage_t&&) = default;
  auto operator=(NibbleDiskImage_t&&) -> NibbleDiskImage_t& = default;
};

extern "C" auto nibble_disk_image_probe(const uint8_t* header_data,
                                        size_t header_size, uint32_t file_size,
                                        const char* ext_hint,
                                        uint32_t image_bytes, const char* ext)
    -> DiskProbe_e {
  if (header_data == nullptr) {
    return disk_probe_no;
  }
  // 455 blocks of 512 is a legal WOZ length and also every byte of a NIB, so
  // the size test below would claim such a file; its magic settles it first.
  if (has_woz_magic(header_data, header_size)) {
    return disk_probe_no;
  }
  if (file_size != image_bytes) {
    return disk_probe_no;
  }
  if (ext_hint != nullptr && strcmp(ext_hint, ext) == 0) {
    return disk_probe_definite;
  }
  return disk_probe_possible;
}

extern "C" auto nibble_disk_image_open(const char* path, uint32_t file_offset,
                                       uint32_t track_nibbles, bool read_only,
                                       void** out_instance) -> DiskError_e {
  if (out_instance == nullptr) {
    return disk_err_invalid_argument;
  }
  *out_instance = nullptr;
  if (path == nullptr || track_nibbles == 0 ||
      track_nibbles > nibbles_per_track) {
    return disk_err_invalid_argument;
  }

  FilePtr_t file{nullptr, fclose};
  bool os_readonly = read_only;
  if (!read_only) {
    file.reset(fopen(path, "r+b"));
  }

  if (file == nullptr) {
    file.reset(fopen(path, "rb"));
    os_readonly = true;
  }

  if (file == nullptr) {
    return (errno == ENOENT) ? disk_err_file_not_found : disk_err_io;
  }

  const int64_t total_size = Path::file_size(file.get());
  if (total_size < 0) {
    return disk_err_io;
  }
  if (static_cast<uint64_t>(total_size) < file_offset) {
    return disk_err_corrupt;
  }

  // The file's length is the one thing a hostile image controls before a
  // byte of it is read, so the ceiling is applied before the instance and its
  // track buffer exist.
  const uint64_t recorded = static_cast<uint64_t>(total_size) - file_offset;
  if (recorded > nibble_image_max_bytes) {
    return disk_err_unsupported;
  }
  if (recorded == 0) {
    return disk_err_corrupt;
  }

  auto image_ptr = std::unique_ptr<NibbleDiskImage_t>(new NibbleDiskImage_t());
  image_ptr->file = std::move(file);
  image_ptr->os_readonly = os_readonly;
  image_ptr->data_offset = file_offset;
  image_ptr->track_size = track_nibbles;
  // A slot the file ends inside still counts: a truncated track reads as far
  // as it goes, and the tracks after it as blank surface.
  image_ptr->track_count =
      static_cast<uint32_t>((recorded + track_nibbles - 1) / track_nibbles);

  *out_instance = image_ptr.release();
  return disk_err_none;
}

extern "C" auto nibble_disk_image_close(void* instance) -> void {
  delete static_cast<NibbleDiskImage_t*>(instance);
}

extern "C" auto nibble_disk_image_is_write_protected(void* instance) -> bool {
  if (instance == nullptr) {
    return true;
  }
  return static_cast<NibbleDiskImage_t*>(instance)->os_readonly;
}

extern "C" auto nibble_disk_image_read_track_bits(
    void* instance, uint32_t quarter_track, uint8_t* bits, uint32_t max_bits,
    uint32_t* out_bit_count, uint8_t* out_bit_timing) -> DiskError_e {
  auto* image_ptr = static_cast<NibbleDiskImage_t*>(instance);
  if (image_ptr == nullptr || bits == nullptr || out_bit_count == nullptr ||
      out_bit_timing == nullptr) {
    return disk_err_invalid_argument;
  }
  *out_bit_count = 0;
  *out_bit_timing = disk_default_bit_timing;

  // All four quarter tracks of a cylinder read the one whole track the image
  // holds for it; past the last slot the surface is blank, as a WOZ treats an
  // unrecorded track.
  const uint32_t track = quarter_track / quarter_tracks_per_cylinder;
  if (track >= image_ptr->track_count) {
    return disk_err_none;
  }

  const auto offset = static_cast<int64_t>(image_ptr->data_offset) +
                      (static_cast<int64_t>(track) *
                       static_cast<int64_t>(image_ptr->track_size));

  if (fseek(image_ptr->file.get(), static_cast<long>(offset), SEEK_SET) != 0) {
    return disk_err_io;
  }

  const size_t read_count = fread(image_ptr->nibbles.data(), 1,
                                  image_ptr->track_size, image_ptr->file.get());
  // A track cut short by the end of the file is the image's own shape; one
  // cut short by the device is not.
  if (read_count < image_ptr->track_size &&
      ferror(image_ptr->file.get()) != 0) {
    return disk_err_io;
  }

  // A nibble image keeps no record of which bytes were written as sync, so
  // the adapter is left to read the track's own shape.
  return disk_encoding_nibbles_to_bits(image_ptr->nibbles.data(),
                                       static_cast<uint32_t>(read_count),
                                       nullptr, bits, max_bits, out_bit_count);
}

extern "C" auto nibble_disk_image_write_track_bits(void* instance,
                                                   uint32_t quarter_track,
                                                   const uint8_t* bits,
                                                   uint32_t bit_count)
    -> DiskError_e {
  auto* image_ptr = static_cast<NibbleDiskImage_t*>(instance);
  if (image_ptr == nullptr || bits == nullptr) {
    return disk_err_invalid_argument;
  }
  if (image_ptr->os_readonly) {
    return disk_err_write_protected;
  }

  // A write past the last slot has nowhere to land; it is dropped the way an
  // unrecorded WOZ track drops it, rather than growing the file.
  const uint32_t track = quarter_track / quarter_tracks_per_cylinder;
  if (track >= image_ptr->track_count) {
    return disk_err_none;
  }

  uint32_t nibble_count = 0;
  const DiskError_e decoded =
      disk_encoding_bits_to_nibbles(bits, bit_count, image_ptr->nibbles.data(),
                                    image_ptr->track_size + 1, &nibble_count);
  if (decoded != disk_err_none) {
    return decoded;
  }
  if (nibble_count > image_ptr->track_size) {
    return disk_err_unsupported;
  }

  // The slot is rewritten whole: the pad after a shorter track joins its
  // closing gap as sync, where the old track's tail would have left stale
  // address and data fields for DOS to find. One fwrite of the slot is the
  // atomicity on offer; a device failing mid-slot tears it.
  std::fill(image_ptr->nibbles.begin() + nibble_count,
            image_ptr->nibbles.begin() + image_ptr->track_size, sync_byte);

  const auto offset = static_cast<int64_t>(image_ptr->data_offset) +
                      (static_cast<int64_t>(track) *
                       static_cast<int64_t>(image_ptr->track_size));

  if (fseek(image_ptr->file.get(), static_cast<long>(offset), SEEK_SET) != 0) {
    return disk_err_io;
  }
  if (fwrite(image_ptr->nibbles.data(), 1, image_ptr->track_size,
             image_ptr->file.get()) != image_ptr->track_size) {
    return disk_err_io;
  }
  if (fflush(image_ptr->file.get()) != 0) {
    return disk_err_io;
  }
  return disk_err_none;
}

extern "C" auto nibble_disk_image_create(const char* path,
                                         uint32_t track_nibbles)
    -> DiskError_e {
  if (path == nullptr) {
    return disk_err_invalid_argument;
  }
  if (track_nibbles == 0 || track_nibbles > nibbles_per_track) {
    return disk_err_invalid_argument;
  }

  // Creating exclusively makes the existence check and the create one call,
  // so a name that is already an image cannot be truncated in the gap
  // between them.
  const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (fd < 0) {
    return disk_err_io;
  }
  // NOLINTNEXTLINE(misc-include-cleaner)
  FilePtr_t file{fdopen(fd, "wb"), fclose};
  if (file == nullptr) {
    close(fd);
    unlink(path);
    return disk_err_io;
  }

  // A blank is a formatted disk of zero sectors, as the sector images read,
  // not a surface of zero nibbles DOS would call unformatted. Zero sectors
  // read the same in either order, so the table that lays them out is moot.
  const std::array<uint8_t, sectors_per_track * sector_bytes> sectors{};
  std::array<uint8_t, disk_encoding_scratch_size> scratch{};
  std::array<uint8_t, nibbles_per_track> slot{};
  for (uint32_t track = 0; track < nibble_image_tracks; ++track) {
    uint32_t nibble_count = 0;
    DiskError_e err = disk_encoding_nibblize_track(
        disk_encoding_sector_order(disk_sector_order_dos), track,
        sectors.data(), slot.data(), nullptr, &nibble_count, scratch.data());
    if (err == disk_err_none && nibble_count > track_nibbles) {
      err = disk_err_unsupported;
    }
    if (err != disk_err_none) {
      file.reset();
      unlink(path);
      return err;
    }
    std::fill(slot.begin() + nibble_count, slot.begin() + track_nibbles,
              sync_byte);
    if (fwrite(slot.data(), 1, track_nibbles, file.get()) != track_nibbles) {
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
