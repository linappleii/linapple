// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/harddisk/formats/BlockDiskImage.h"

#include <cstdio>
#include <memory>

#include "apple2/Apple2Types.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

// NOLINTBEGIN(google-runtime-int, cppcoreguidelines-owning-memory,
//             bugprone-easily-swappable-parameters, modernize-make-unique)
// Justification: This module uses procedural patterns for C-compatibility.
// google-runtime-int is required for fseek offsets. owning-memory and
// make-unique are suppressed for C++11 compatibility and handle-based
// resource management. easily-swappable-parameters is mandated by the
// shared block image ABI signatures.

struct BlockDiskImage_t {
  FilePtr_t file{nullptr, fclose};
  uint32_t data_offset = 0;
  uint32_t total_blocks = 0;
  bool os_readonly = false;

  BlockDiskImage_t() = default;
  ~BlockDiskImage_t() = default;

  BlockDiskImage_t(const BlockDiskImage_t&) = delete;
  auto operator=(const BlockDiskImage_t&) -> BlockDiskImage_t& = delete;
  BlockDiskImage_t(BlockDiskImage_t&&) = default;
  auto operator=(BlockDiskImage_t&&) -> BlockDiskImage_t& = default;
};

namespace {
constexpr uint32_t block_size = 512;
}

extern "C" auto block_disk_image_open(const char* path, uint32_t file_offset,
                                      bool* out_is_read_only)
    -> BlockDiskImage_t* {
  if (path == nullptr) {
    return nullptr;
  }

  auto image_ptr = std::unique_ptr<BlockDiskImage_t>(new BlockDiskImage_t());

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

  fseek(image_ptr->file.get(), 0, SEEK_END);
  const uint32_t file_size =
      static_cast<uint32_t>(ftell(image_ptr->file.get()));

  image_ptr->data_offset = file_offset;
  image_ptr->total_blocks = (file_size - file_offset) / block_size;

  return image_ptr.release();
}

extern "C" auto block_disk_image_close(BlockDiskImage_t* image_ptr) -> void {
  if (image_ptr == nullptr) {
    return;
  }
  delete image_ptr;
}

extern "C" auto block_disk_image_is_write_protected(BlockDiskImage_t* image_ptr)
    -> bool {
  if (image_ptr == nullptr) {
    return true;
  }
  return image_ptr->os_readonly;
}

extern "C" auto block_disk_image_read_block(BlockDiskImage_t* image_ptr,
                                            uint32_t block_num, uint8_t* buffer)
    -> HarddiskError_e {
  if (image_ptr == nullptr || buffer == nullptr ||
      block_num >= image_ptr->total_blocks) {
    return harddisk_err_io;
  }

  const auto offset = static_cast<int64_t>(image_ptr->data_offset) +
                      (static_cast<int64_t>(block_num) * block_size);

  if (fseek(image_ptr->file.get(), static_cast<long>(offset), SEEK_SET) != 0) {
    return harddisk_err_io;
  }

  if (fread(buffer, 1, block_size, image_ptr->file.get()) != block_size) {
    return harddisk_err_io;
  }

  return harddisk_err_none;
}

extern "C" auto block_disk_image_write_block(BlockDiskImage_t* image_ptr,
                                             uint32_t block_num,
                                             const uint8_t* buffer)
    -> HarddiskError_e {
  if (image_ptr == nullptr || buffer == nullptr || image_ptr->os_readonly) {
    return harddisk_err_read_only;
  }

  if (block_num >= image_ptr->total_blocks) {
    return harddisk_err_io;
  }

  const auto offset = static_cast<int64_t>(image_ptr->data_offset) +
                      (static_cast<int64_t>(block_num) * block_size);

  if (fseek(image_ptr->file.get(), static_cast<long>(offset), SEEK_SET) != 0) {
    return harddisk_err_io;
  }

  if (fwrite(buffer, 1, block_size, image_ptr->file.get()) != block_size) {
    return harddisk_err_io;
  }

  return harddisk_err_none;
}

extern "C" auto block_disk_image_get_total_blocks(BlockDiskImage_t* image_ptr)
    -> uint32_t {
  if (image_ptr == nullptr) {
    return 0;
  }
  return image_ptr->total_blocks;
}

// NOLINTEND(google-runtime-int, cppcoreguidelines-owning-memory,
//           bugprone-easily-swappable-parameters, modernize-make-unique)
