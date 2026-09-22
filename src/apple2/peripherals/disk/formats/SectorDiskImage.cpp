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
#include "core/Util_Endian.h"
#include "core/Util_Path.h"

// NOLINTBEGIN(google-runtime-int, cppcoreguidelines-owning-memory, bugprone-easily-swappable-parameters, modernize-make-unique)
// Justification:
// This module uses procedural patterns for C-compatibility. google-runtime-int
// is required for fseek offsets. owning-memory and make-unique are suppressed
// for C++11 compatibility and handle-based resource management.
// easily-swappable-parameters is mandated by the shared sector image ABI
// signatures.

enum { dos_track_size = 4096 };

struct SectorDiskImage_t {
  FilePtr_t file{nullptr, fclose};
  uint32_t data_offset = 0;
  bool os_readonly = false;
  DiskSectorOrder_e order = disk_sector_order_prodos;
  std::array<uint8_t, disk_encoding_scratch_size> scratch{};
  std::array<uint8_t, nibbles_per_track> nibbles{};
  std::array<uint8_t, nibbles_per_track> sync_mask{};
  std::array<uint8_t, dos_track_size> sectors{};

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
constexpr int catalog_track = 17;
constexpr int page_size = 0x0100;
constexpr int catalog_start_sector = 1;
constexpr int catalog_end_sector = 15;
constexpr int next_sector_offset = 2;
}  // namespace dos

namespace prodos {
constexpr int block_size = 512;
constexpr int dir_start_block = 2;
constexpr int link_words_size = 4;
constexpr int blocks_per_track = 8;
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
  image_ptr->order =
      is_dos_order ? disk_sector_order_dos : disk_sector_order_prodos;

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
                                       uint32_t* out_bit_count,
                                       uint8_t* out_bit_timing) -> DiskError_e {
  if (image_ptr == nullptr || bits == nullptr || out_bit_count == nullptr ||
      out_bit_timing == nullptr) {
    return disk_err_invalid_argument;
  }
  *out_bit_count = 0;
  *out_bit_timing = disk_default_bit_timing;

  const int track = quarter_track_to_cylinder(quarter_track);
  if (track >= tracks_per_disk) {
    return disk_err_invalid_argument;
  }

  const auto offset = static_cast<int64_t>(image_ptr->data_offset) +
                      (static_cast<int64_t>(track) * dos::track_size);

  if (fseek(image_ptr->file.get(), static_cast<long>(offset), SEEK_SET) != 0) {
    return disk_err_io;
  }

  if (fread(image_ptr->sectors.data(), 1, dos::track_size,
            image_ptr->file.get()) != dos::track_size) {
    return disk_err_io;
  }

  uint32_t nibble_count = 0;
  const DiskError_e synthesised = disk_encoding_nibblize_track(
      disk_encoding_sector_order(image_ptr->order),
      static_cast<uint32_t>(track), image_ptr->sectors.data(),
      image_ptr->nibbles.data(), image_ptr->sync_mask.data(), &nibble_count,
      image_ptr->scratch.data());
  if (synthesised != disk_err_none) {
    return synthesised;
  }

  return disk_encoding_nibbles_to_bits(image_ptr->nibbles.data(), nibble_count,
                                       image_ptr->sync_mask.data(), bits,
                                       max_bits, out_bit_count);
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

  // Nothing reaches the file unless all sixteen sectors came back, so a
  // track the head only half-read cannot cost the image the other half.
  const DiskError_e decoded_track = disk_encoding_denibblize_track(
      disk_encoding_sector_order(image_ptr->order),
      static_cast<uint32_t>(track), image_ptr->nibbles.data(), nibble_count,
      image_ptr->sectors.data(), image_ptr->scratch.data());
  if (decoded_track != disk_err_none) {
    return decoded_track;
  }

  const auto offset = static_cast<int64_t>(image_ptr->data_offset) +
                      (static_cast<int64_t>(track) * dos::track_size);
  if (fseek(image_ptr->file.get(), static_cast<long>(offset), SEEK_SET) != 0) {
    return disk_err_io;
  }
  if (fwrite(image_ptr->sectors.data(), 1, dos::track_size,
             image_ptr->file.get()) != static_cast<size_t>(dos::track_size)) {
    return disk_err_io;
  }
  if (fflush(image_ptr->file.get()) != 0) {
    return disk_err_io;
  }
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

namespace {
// DOS logical sector s of a track shares its physical slot with ProDOS
// logical sector 15 - s for s = 1..14, and with itself for 0 and 15, so a
// ProDOS-order file keeps DOS's sectors at those indices (Beneath Apple DOS
// ch. 3, the two skewing tables).
auto dos_sector_offset(bool is_dos_order, int track, int sector) -> size_t {
  const int last_sector = sectors_per_track - 1;
  const bool shares_index =
      is_dos_order || sector == 0 || sector == last_sector;
  const int file_sector = shares_index ? sector : last_sector - sector;
  return (static_cast<size_t>(track) * static_cast<size_t>(dos::track_size)) +
         (static_cast<size_t>(file_sector) *
          static_cast<size_t>(dos::page_size));
}

// Block k of a track is ProDOS sectors 2k and 2k + 1, and the physical slot
// of sector 2k holds DOS sector 15 - 2k for k = 1..7 (sector 0 for k = 0), so
// a DOS-order file keeps the block's first half there.
auto prodos_block_offset(bool is_dos_order, uint16_t block) -> size_t {
  if (!is_dos_order) {
    return static_cast<size_t>(block) * static_cast<size_t>(prodos::block_size);
  }
  const int track = block / prodos::blocks_per_track;
  const int block_in_track = block % prodos::blocks_per_track;
  const int sector = (block_in_track == 0)
                         ? 0
                         : (sectors_per_track - 1) - (2 * block_in_track);
  return (static_cast<size_t>(track) * static_cast<size_t>(dos::track_size)) +
         (static_cast<size_t>(sector) * static_cast<size_t>(dos::page_size));
}

// The catalog on track 17 is a chain from sector 15 down to sector 1, each
// sector's link naming the one below it (Beneath Apple DOS ch. 4).
auto has_dos_catalog(const uint8_t* header_data, size_t header_size,
                     bool is_dos_order) -> bool {
  for (int sector = dos::catalog_start_sector;
       sector <= dos::catalog_end_sector; ++sector) {
    const size_t offset =
        dos_sector_offset(is_dos_order, dos::catalog_track, sector) +
        static_cast<size_t>(dos::next_sector_offset);
    if (offset >= header_size ||
        header_data[offset] != static_cast<uint8_t>(sector - 1)) {
      return false;
    }
  }
  return true;
}

// The volume directory starts at block 2 and every block opens with its
// previous and next links, the key block's previous being 0 and the last
// block's next being 0 (ProDOS 8 Technical Reference, B.2.2).
auto has_prodos_directory(const uint8_t* header_data, size_t header_size,
                          bool is_dos_order) -> bool {
  uint16_t previous = 0;
  uint16_t block = prodos::dir_start_block;
  for (int visited = 0; visited < prodos::max_blocks_140k; ++visited) {
    const size_t offset = prodos_block_offset(is_dos_order, block);
    if (offset + prodos::link_words_size > header_size) {
      return false;
    }
    const uint16_t prev = read_u16_le(&header_data[offset]);
    const uint16_t next = read_u16_le(&header_data[offset + 2]);
    if (prev != previous) {
      return false;
    }
    if (next == 0) {
      return block != prodos::dir_start_block;
    }
    if (next >= prodos::max_blocks_140k) {
      return false;
    }
    previous = block;
    block = next;
  }
  return false;
}
}  // namespace

auto sector_disk_image_probe_signature(const uint8_t* header_data,
                                       size_t header_size, uint32_t file_size,
                                       bool is_dos_order) -> DiskProbe_e {
  if (file_size < disk::min_140k_size || file_size > disk::max_140k_size) {
    if (file_size != disk::alt_size_1 && file_size != disk::alt_size_2) {
      return disk_probe_no;
    }
  }

  // Either file system may have been imaged in either order, so the order
  // is definite once one of them reads coherently in it.
  if (has_dos_catalog(header_data, header_size, is_dos_order) ||
      has_prodos_directory(header_data, header_size, is_dos_order)) {
    return disk_probe_definite;
  }

  return disk_probe_possible;
}

// NOLINTEND(google-runtime-int, cppcoreguidelines-owning-memory, bugprone-easily-swappable-parameters, modernize-make-unique)
