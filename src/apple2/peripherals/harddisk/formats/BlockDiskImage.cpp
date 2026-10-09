// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/harddisk/formats/BlockDiskImage.h"

#include <stdio.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "apple2/peripherals/harddisk/HarddiskError.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/HarddiskLoader.h"
#include "core/Util_Endian.h"
#include "core/Util_Path.h"

// NOLINTBEGIN(google-runtime-int, cppcoreguidelines-owning-memory, bugprone-easily-swappable-parameters, modernize-make-unique)
// Justification:
// This module uses procedural patterns for C-compatibility. google-runtime-int
// is required for fseek offsets. owning-memory and make-unique are suppressed
// for C++11 compatibility and handle-based resource management.
// easily-swappable-parameters is mandated by the shared block image ABI
// signatures.

namespace {

struct BlockDiskImage_t {
  FilePtr file{nullptr, fclose};
  std::string path;
  uint64_t data_offset = 0;
  uint32_t total_blocks = 0;
  bool host_read_only = false;
  BlockDiskOrder_e order = block_disk_order_prodos;

  BlockDiskImage_t() = default;
  ~BlockDiskImage_t() = default;

  BlockDiskImage_t(const BlockDiskImage_t&) = delete;
  auto operator=(const BlockDiskImage_t&) -> BlockDiskImage_t& = delete;
  BlockDiskImage_t(BlockDiskImage_t&&) = default;
  auto operator=(BlockDiskImage_t&&) -> BlockDiskImage_t& = default;
};

constexpr uint32_t block_size = 512;
constexpr uint32_t half_block = 256;
constexpr uint32_t sector_size = 256;
constexpr uint32_t sectors_per_track = 16;
constexpr uint32_t track_bytes = sectors_per_track * sector_size;
constexpr uint32_t blocks_per_track = 8;

// Block k of a track is the pair of DOS 3.3 sectors Fig. 3.14 of Beneath
// Apple ProDOS gives (pp. 3-16 to 3-18 by the printed footers), first-named
// sector first: 0&E, D&C, B&A, 9&8, 7&6, 5&4, 3&2, 1&F.
constexpr std::array<std::array<uint8_t, 2>, blocks_per_track>
    k_dos_sectors_of_block = {
        {
            {{0x0, 0xE}},
            {{0xD, 0xC}},
            {{0xB, 0xA}},
            {{0x9, 0x8}},
            {{0x7, 0x6}},
            {{0x5, 0x4}},
            {{0x3, 0x2}},
            {{0x1, 0xF}},
        },
};

// The byte offset of either 256-byte half of block b in a DOS-order file:
// track b >> 3, the sector the table gives for b & 7.
auto dos_block_half_offset(uint32_t block, uint32_t half) -> uint64_t {
  const uint32_t track = block / blocks_per_track;
  const uint32_t sector =
      k_dos_sectors_of_block.at(block % blocks_per_track).at(half);
  return (static_cast<uint64_t>(track) * track_bytes) +
         (static_cast<uint64_t>(sector) * sector_size);
}

// Where DOS 3.3 sector s of track t lies in a file of either order: in a
// DOS-order file at its own index; in a ProDOS-order file inside the half of
// the block the table pairs it with.
auto dos_sector_offset(BlockDiskOrder_e order, uint32_t track, uint32_t sector)
    -> uint64_t {
  if (order == block_disk_order_dos) {
    return (static_cast<uint64_t>(track) * track_bytes) +
           (static_cast<uint64_t>(sector) * sector_size);
  }
  for (uint32_t k = 0; k < blocks_per_track; ++k) {
    for (uint32_t half = 0; half < 2; ++half) {
      if (k_dos_sectors_of_block.at(k).at(half) == sector) {
        return (static_cast<uint64_t>(track) * track_bytes) +
               (static_cast<uint64_t>(k) * block_size) +
               (static_cast<uint64_t>(half) * half_block);
      }
    }
  }
  return 0;
}

auto block_offset(BlockDiskOrder_e order, uint32_t block, uint32_t half)
    -> uint64_t {
  if (order == block_disk_order_dos) {
    return dos_block_half_offset(block, half);
  }
  return (static_cast<uint64_t>(block) * block_size) +
         (static_cast<uint64_t>(half) * half_block);
}

namespace dos {
constexpr uint32_t catalog_track = 17;
constexpr uint32_t catalog_first_sector = 15;
constexpr uint32_t catalog_last_sector = 1;
constexpr uint32_t next_sector_offset = 2;
}  // namespace dos

namespace prodos {
constexpr uint32_t directory_key_block = 2;
constexpr uint32_t link_bytes = 4;
constexpr uint32_t max_blocks_140k = block_disk_image_dos_blocks;
}  // namespace prodos

// The catalog on track 17 is a chain from sector 15 down to sector 1, each
// sector's link naming the one below it (Beneath Apple DOS ch. 4).
auto has_dos_catalog(const uint8_t* header_data, size_t header_size,
                     BlockDiskOrder_e order) -> bool {
  for (uint32_t sector = dos::catalog_last_sector;
       sector <= dos::catalog_first_sector; ++sector) {
    const uint64_t offset =
        dos_sector_offset(order, dos::catalog_track, sector) +
        dos::next_sector_offset;
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
                          BlockDiskOrder_e order) -> bool {
  uint32_t previous = 0;
  uint32_t block = prodos::directory_key_block;
  for (uint32_t visited = 0; visited < prodos::max_blocks_140k; ++visited) {
    const uint64_t offset = block_offset(order, block, 0);
    if (offset + prodos::link_bytes > header_size) {
      return false;
    }
    const uint16_t prev = read_u16_le(&header_data[offset]);
    const uint16_t next = read_u16_le(&header_data[offset + 2]);
    if (prev != previous) {
      return false;
    }
    if (next == 0) {
      return block != prodos::directory_key_block;
    }
    if (next >= prodos::max_blocks_140k) {
      return false;
    }
    previous = block;
    block = next;
  }
  return false;
}

auto image(void* instance) -> BlockDiskImage_t* {
  return static_cast<BlockDiskImage_t*>(instance);
}

auto seek_to(BlockDiskImage_t* image_ptr, uint64_t offset) -> bool {
  return fseeko(image_ptr->file.get(), static_cast<off_t>(offset), SEEK_SET) ==
         0;
}

}  // namespace

auto block_disk_image_open(const char* path, uint32_t file_offset,
                           BlockDiskOrder_e order, uint32_t block_count,
                           bool read_only, void** out_instance)
    -> HarddiskError_e {
  if (out_instance == nullptr) {
    return harddisk_err_io;
  }
  *out_instance = nullptr;
  if (path == nullptr) {
    return harddisk_err_io;
  }

  bool host_read_only = read_only;
  FilePtr file{nullptr, fclose};
  if (!read_only) {
    file.reset(fopen(path, "r+b"));
  }

  // A file the host will not let us write is still a disk we can read, so the
  // fallback answers write protected rather than refusing to open it.
  if (file == nullptr) {
    file.reset(fopen(path, "rb"));
    host_read_only = true;
  }

  if (file == nullptr) {
    return (errno == ENOENT) ? harddisk_err_not_found : harddisk_err_io;
  }

  const int64_t total_size = Path::file_size(file.get());
  if (total_size < 0) {
    return harddisk_err_io;
  }
  if (static_cast<uint64_t>(total_size) < file_offset) {
    return harddisk_err_invalid_format;
  }
  const uint64_t available = static_cast<uint64_t>(total_size) - file_offset;

  if (block_count == 0) {
    const uint64_t whole_blocks = available / block_size;
    if (whole_blocks == 0 || whole_blocks > UINT32_MAX) {
      return harddisk_err_invalid_format;
    }
    block_count = static_cast<uint32_t>(whole_blocks);
  } else if (static_cast<uint64_t>(block_count) * block_size > available) {
    return harddisk_err_invalid_format;
  }
  if (order == block_disk_order_dos &&
      block_count != block_disk_image_dos_blocks) {
    return harddisk_err_invalid_format;
  }

  auto image_ptr = std::unique_ptr<BlockDiskImage_t>(new BlockDiskImage_t());
  image_ptr->file = std::move(file);
  image_ptr->path = path;
  image_ptr->host_read_only = host_read_only;
  image_ptr->data_offset = file_offset;
  image_ptr->total_blocks = block_count;
  image_ptr->order = order;

  *out_instance = image_ptr.release();
  return harddisk_err_none;
}

auto block_disk_image_close(void* instance) -> void {
  BlockDiskImage_t* image_ptr = image(instance);
  if (image_ptr == nullptr) {
    return;
  }
  // Every block write already reached the kernel; this makes the kernel put
  // them on the medium before the handle goes.
  if (!image_ptr->host_read_only && image_ptr->file != nullptr &&
      fsync(fileno(image_ptr->file.get())) != 0) {
    const std::string text = "could not sync '" + image_ptr->path +
                             "' to its medium: " + strerror(errno);
    harddisk_loader_note("block image", text.c_str());
  }
  delete image_ptr;
}

auto block_disk_image_is_write_protected(void* instance) -> bool {
  const BlockDiskImage_t* image_ptr = image(instance);
  if (image_ptr == nullptr) {
    return true;
  }
  return image_ptr->host_read_only;
}

auto block_disk_image_read_block(void* instance, uint32_t block_num,
                                 uint8_t* buffer) -> HarddiskError_e {
  BlockDiskImage_t* image_ptr = image(instance);
  if (image_ptr == nullptr || buffer == nullptr ||
      block_num >= image_ptr->total_blocks) {
    return harddisk_err_io;
  }

  // A ProDOS-order block is one run of 512 bytes; a DOS-order block is two
  // sectors that need not be adjacent.
  const uint32_t pieces = (image_ptr->order == block_disk_order_dos) ? 2 : 1;
  const uint32_t piece_size = block_size / pieces;
  for (uint32_t half = 0; half < pieces; ++half) {
    const uint64_t offset = image_ptr->data_offset +
                            block_offset(image_ptr->order, block_num, half);
    if (!seek_to(image_ptr, offset)) {
      return harddisk_err_io;
    }
    if (fread(buffer + (static_cast<size_t>(half) * piece_size), 1, piece_size,
              image_ptr->file.get()) != piece_size) {
      return harddisk_err_io;
    }
  }

  return harddisk_err_none;
}

auto block_disk_image_write_block(void* instance, uint32_t block_num,
                                  const uint8_t* buffer) -> HarddiskError_e {
  BlockDiskImage_t* image_ptr = image(instance);
  if (image_ptr == nullptr || buffer == nullptr) {
    return harddisk_err_io;
  }
  if (image_ptr->host_read_only) {
    return harddisk_err_read_only;
  }
  if (block_num >= image_ptr->total_blocks) {
    return harddisk_err_io;
  }

  // Both halves of a DOS-order block go out before the one flush, so a block
  // reaches the kernel whole or, should the second write fail, torn between
  // its two sectors; there is no slot to rewrite atomically.
  const uint32_t pieces = (image_ptr->order == block_disk_order_dos) ? 2 : 1;
  const uint32_t piece_size = block_size / pieces;
  for (uint32_t half = 0; half < pieces; ++half) {
    const uint64_t offset = image_ptr->data_offset +
                            block_offset(image_ptr->order, block_num, half);
    if (!seek_to(image_ptr, offset)) {
      return harddisk_err_io;
    }
    if (fwrite(buffer + (static_cast<size_t>(half) * piece_size), 1, piece_size,
               image_ptr->file.get()) != piece_size) {
      return harddisk_err_io;
    }
  }
  if (fflush(image_ptr->file.get()) != 0) {
    return harddisk_err_io;
  }

  return harddisk_err_none;
}

auto block_disk_image_get_total_blocks(void* instance) -> uint32_t {
  const BlockDiskImage_t* image_ptr = image(instance);
  if (image_ptr == nullptr) {
    return 0;
  }
  return image_ptr->total_blocks;
}

auto block_disk_image_probe_signature(const uint8_t* header_data,
                                      size_t header_size, uint64_t file_size,
                                      BlockDiskOrder_e order)
    -> HarddiskProbe_e {
  if (header_data == nullptr) {
    return harddisk_probe_no;
  }
  if (file_size != block_disk_image_dos_size) {
    return harddisk_probe_no;
  }

  // Either file system may have been imaged in either order, so the order
  // is definite once one of them reads coherently in it.
  if (has_dos_catalog(header_data, header_size, order) ||
      has_prodos_directory(header_data, header_size, order)) {
    return harddisk_probe_definite;
  }

  return harddisk_probe_possible;
}

// NOLINTEND(google-runtime-int, cppcoreguidelines-owning-memory, bugprone-easily-swappable-parameters, modernize-make-unique)
