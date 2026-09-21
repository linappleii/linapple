// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/SectorDiskImage.h"

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
#include "apple2/peripherals/Peripheral_Types.h"
#include "core/Util_Endian.h"
#include "core/Util_Path.h"

// NOLINTBEGIN(google-runtime-int, cppcoreguidelines-owning-memory, bugprone-easily-swappable-parameters, modernize-make-unique)
// Justification:
// This module uses procedural patterns for C-compatibility. google-runtime-int
// is required for fseek offsets. owning-memory and make-unique are suppressed
// for C++11 compatibility and handle-based resource management.
// easily-swappable-parameters is mandated by the shared sector image ABI
// signatures.

struct SectorDiskImage_t {
  FilePtr_t file{nullptr, fclose};
  uint32_t data_offset = 0;
  bool os_readonly = false;
  bool is_dos_order = false;
  std::array<uint8_t, disk_encoding_work_buffer_offset * 3> work_buffer{};
  std::array<uint8_t, nibbles_per_track> nibbles{};

  SectorDiskImage_t() = default;
  ~SectorDiskImage_t() = default;

  SectorDiskImage_t(const SectorDiskImage_t&) = delete;
  auto operator=(const SectorDiskImage_t&) -> SectorDiskImage_t& = delete;
  SectorDiskImage_t(SectorDiskImage_t&&) = default;
  auto operator=(SectorDiskImage_t&&) -> SectorDiskImage_t& = default;
};

namespace {
namespace disk {
constexpr int size_140k = 143360;
constexpr uint32_t min_140k_size = 143105;
constexpr uint32_t max_140k_size = 143364;
constexpr uint32_t alt_size_1 = 143403;
constexpr uint32_t alt_size_2 = 143488;
constexpr uint8_t sync_byte = 0xFF;
}  // namespace disk

namespace dos {
constexpr int track_size = 4096;
constexpr int vtoc_offset = 0x11000;
constexpr int page_size = 0x0100;
constexpr int catalog_start_sector = 1;
constexpr int catalog_end_sector = 15;
constexpr int next_sector_offset = 2;
}  // namespace dos

namespace prodos {
constexpr int block_size = 512;
constexpr int dir_start_block = 2;
constexpr int dir_end_block = 5;
constexpr int dir_link_offset = 0x0100;
constexpr uint16_t max_blocks_140k = 280;
}  // namespace prodos

constexpr int create_buffer_size = 1024;
constexpr uint32_t quarter_tracks_per_cylinder = 4;

// A sector image records nothing between cylinders, so all four quarter
// tracks of a cylinder synthesise the same surface.
auto quarter_track_to_cylinder(uint32_t quarter_track) -> int {
  return static_cast<int>(quarter_track / quarter_tracks_per_cylinder);
}
}  // namespace

auto sector_disk_image_open(const char* path, uint32_t file_offset,
                            bool is_dos_order, bool read_only)
    -> SectorDiskImage_t* {
  if (path == nullptr) {
    return nullptr;
  }

  auto image_ptr = std::unique_ptr<SectorDiskImage_t>(new SectorDiskImage_t());

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

  const int64_t total_size = Path::file_size(image_ptr->file.get());
  if (total_size < 0 || static_cast<size_t>(total_size) < file_offset) {
    return nullptr;
  }
  const size_t effective_size = static_cast<size_t>(total_size) - file_offset;
  if (effective_size < static_cast<size_t>(dos::track_size) ||
      (effective_size % dos::page_size != 0)) {
    return nullptr;
  }

  image_ptr->data_offset = file_offset;
  image_ptr->is_dos_order = is_dos_order;

  return image_ptr.release();
}

// Why: Destroys the sector image instance. The RAII FilePtr_t member ensures
// the physical file is closed during destruction.
auto sector_disk_image_close(SectorDiskImage_t* image_ptr) -> void {
  delete image_ptr;
}

auto sector_disk_image_is_write_protected(SectorDiskImage_t* image_ptr)
    -> bool {
  if (image_ptr == nullptr) {
    return true;
  }
  return image_ptr->os_readonly;
}

auto sector_disk_image_read_track_bits(SectorDiskImage_t* image_ptr,
                                       uint32_t quarter_track, uint8_t* bits,
                                       uint32_t max_bits,
                                       uint32_t* out_bit_count) -> DiskError_e {
  if (image_ptr == nullptr || bits == nullptr || out_bit_count == nullptr) {
    return disk_err_invalid_argument;
  }
  *out_bit_count = 0;

  const int track = quarter_track_to_cylinder(quarter_track);
  if (track >= tracks_per_disk) {
    return disk_err_invalid_argument;
  }

  image_ptr->work_buffer.fill(0);
  const auto offset = static_cast<int64_t>(image_ptr->data_offset) +
                      (static_cast<int64_t>(track) * dos::track_size);

  if (fseek(image_ptr->file.get(), static_cast<long>(offset), SEEK_SET) != 0) {
    return disk_err_io;
  }

  if (fread(image_ptr->work_buffer.data(), 1, dos::track_size,
            image_ptr->file.get()) != dos::track_size) {
    return disk_err_io;
  }

  image_ptr->nibbles.fill(disk::sync_byte);
  disk_encoding_nibblize_track(image_ptr->work_buffer.data(),
                               image_ptr->nibbles.data(),
                               image_ptr->is_dos_order, track);

  return disk_encoding_nibbles_to_bits(image_ptr->nibbles.data(),
                                       nibbles_per_track, bits, max_bits,
                                       out_bit_count);
}

auto sector_disk_image_write_track_bits(SectorDiskImage_t* image_ptr,
                                        uint32_t quarter_track,
                                        const uint8_t* bits, uint32_t bit_count)
    -> DiskError_e {
  if (image_ptr == nullptr || bits == nullptr) {
    return disk_err_invalid_argument;
  }
  if (image_ptr->os_readonly) {
    return disk_err_write_protected;
  }

  const int track = quarter_track_to_cylinder(quarter_track);
  if (track >= tracks_per_disk) {
    return disk_err_invalid_argument;
  }

  uint32_t nibble_count = 0;
  const DiskError_e decoded =
      disk_encoding_bits_to_nibbles(bits, bit_count, image_ptr->nibbles.data(),
                                    nibbles_per_track, &nibble_count);
  if (decoded != disk_err_none) {
    return decoded;
  }

  const auto offset = static_cast<int64_t>(image_ptr->data_offset) +
                      (static_cast<int64_t>(track) * dos::track_size);

  if (fseek(image_ptr->file.get(), static_cast<long>(offset), SEEK_SET) == 0) {
    if (fread(image_ptr->work_buffer.data(), 1, dos::track_size,
              image_ptr->file.get()) != static_cast<size_t>(dos::track_size)) {
      image_ptr->work_buffer.fill(0);
    }
  } else {
    image_ptr->work_buffer.fill(0);
  }

  disk_encoding_denibblize_track(
      image_ptr->work_buffer.data(), image_ptr->nibbles.data(),
      image_ptr->is_dos_order, static_cast<int>(nibble_count));

  if (fseek(image_ptr->file.get(), static_cast<long>(offset), SEEK_SET) != 0) {
    return disk_err_io;
  }
  if (fwrite(image_ptr->work_buffer.data(), 1, dos::track_size,
             image_ptr->file.get()) != static_cast<size_t>(dos::track_size)) {
    return disk_err_io;
  }
  fflush(image_ptr->file.get());
  return disk_err_none;
}

auto sector_disk_image_create(const char* path) -> DiskError_e {
  if (path == nullptr) {
    return disk_err_io;
  }

  FilePtr_t file{fopen(path, "wb"), fclose};
  if (file == nullptr) {
    return disk_err_io;
  }

  std::array<uint8_t, create_buffer_size> zero{};
  zero.fill(0);
  for (int i = 0; i < disk::size_140k / create_buffer_size; ++i) {
    if (fwrite(zero.data(), 1, zero.size(), file.get()) != zero.size()) {
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

// Why: Probes the image for DOS or ProDOS file system signatures
// (VTOC/Directory blocks). Used by the high-level loader to automatically
// determine disk order.
auto sector_disk_image_probe_signature(const uint8_t* header_data,
                                       size_t header_size, uint32_t file_size,
                                       bool is_dos_order) -> DiskProbe_e {
  if (file_size < disk::min_140k_size || file_size > disk::max_140k_size) {
    if (file_size != disk::alt_size_1 && file_size != disk::alt_size_2) {
      return disk_probe_no;
    }
  }

  if (is_dos_order) {
    const size_t dos_vtoc_min = static_cast<size_t>(dos::vtoc_offset) +
                                static_cast<size_t>(dos::next_sector_offset) +
                                (static_cast<size_t>(dos::catalog_end_sector) *
                                 static_cast<size_t>(dos::page_size));
    if (header_size >= dos_vtoc_min) {
      bool mismatch = false;
      for (int loop = dos::catalog_start_sector;
           loop <= dos::catalog_end_sector; ++loop) {
        const size_t offset =
            static_cast<size_t>(dos::vtoc_offset) +
            static_cast<size_t>(dos::next_sector_offset) +
            (static_cast<size_t>(loop) * static_cast<size_t>(dos::page_size));
        if (header_data[offset] != static_cast<uint8_t>(loop - 1)) {
          mismatch = true;
          break;
        }
      }
      if (!mismatch) {
        return disk_probe_definite;
      }
    }
  } else {
    const size_t prodos_min = (static_cast<size_t>(prodos::dir_end_block) *
                               static_cast<size_t>(prodos::block_size)) +
                              static_cast<size_t>(prodos::dir_link_offset) + 2;
    if (header_size >= prodos_min) {
      const size_t offset_prev = (static_cast<size_t>(prodos::dir_start_block) *
                                  static_cast<size_t>(prodos::block_size)) +
                                 static_cast<size_t>(prodos::dir_link_offset);
      const size_t offset_next = offset_prev + 2;

      const uint16_t prev = read_u16_le(&header_data[offset_prev]);
      const uint16_t next = read_u16_le(&header_data[offset_next]);

      if (prev == 0 && next > static_cast<uint16_t>(prodos::dir_start_block) &&
          next < prodos::max_blocks_140k) {
        return disk_probe_definite;
      }
    }
  }

  return disk_probe_possible;
}

auto sector_disk_image_command(SectorDiskImage_t* image_ptr, uint32_t cmd_id,
                               const void* payload, size_t payload_size)
    -> PeripheralStatus_t {
  (void)cmd_id;
  (void)payload;
  (void)payload_size;
  if (image_ptr == nullptr) {
    return peripheral_error;
  }
  return peripheral_incompatible;
}

// NOLINTEND(google-runtime-int, cppcoreguidelines-owning-memory, bugprone-easily-swappable-parameters, modernize-make-unique)
