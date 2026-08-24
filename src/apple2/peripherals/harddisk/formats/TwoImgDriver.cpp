// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/harddisk/formats/TwoImgDriver.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/formats/BlockDiskImage.h"
#include "core/Util_Path.h"

// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// cppcoreguidelines-pro-type-static-cast-downcast, google-runtime-int)
// Justification: Format drivers utilize a procedural C-compatible handle system
// and standardized probing signatures mandated by the Harddisk subsystem ABI.

namespace {

#pragma pack(push, 1)
struct TwoImgHeader_t {
  char magic[4];          // "2IMG"
  char creator[4];        // e.g. "2mgx"
  uint16_t header_len;    // 0x0040 (64 bytes)
  uint16_t version;       // 1
  uint32_t image_format;  // 0 = DOS 3.3, 1 = ProDOS, 2 = Nibble
  uint32_t flags;         // bit 31: write protected, bit 8: volume number valid
  uint32_t blocks;        // block count
  uint32_t data_offset;   // offset to data from start of file (usually 0x40)
  uint32_t data_len;      // length of data
  uint32_t comment_offset;
  uint32_t comment_len;
  uint32_t creator_offset;
  uint32_t creator_len;
  uint8_t reserved[16];
};
#pragma pack(pop)

constexpr uint32_t block_size = 512;
constexpr uint32_t two_img_header_size = 64;
constexpr uint32_t two_img_format_dos33 = 0;
constexpr uint32_t two_img_format_prodos = 1;
constexpr uint32_t two_img_flag_locked = 0x80000000;

auto two_img_probe(const uint8_t* header_data, size_t header_size,
                   uint32_t file_size, const char* ext_hint)
    -> HarddiskProbe_e {
  (void)ext_hint;
  if (header_data == nullptr || header_size < two_img_header_size ||
      file_size < two_img_header_size) {
    return harddisk_probe_no;
  }

  if (memcmp(header_data, "2IMG", 4) == 0) {
    uint32_t image_format = 0;
    memcpy(&image_format, header_data + 12, sizeof(image_format));
    if (image_format == two_img_format_prodos) {
      return harddisk_probe_definite;
    }
    if (image_format == two_img_format_dos33 &&
        ((file_size - two_img_header_size) % block_size == 0)) {
      return harddisk_probe_possible;
    }
    return harddisk_probe_possible;
  }

  return harddisk_probe_no;
}

auto two_img_open(const char* path, uint32_t file_offset, bool* out_os_readonly,
                  void** out_instance_handle) -> HarddiskError_e {
  if (path == nullptr || out_instance_handle == nullptr) {
    return harddisk_err_io;
  }

  FilePtr_t file{fopen(path, "rb"), fclose};
  if (file == nullptr) {
    return harddisk_err_not_found;
  }

  if (fseek(file.get(), static_cast<long>(file_offset), SEEK_SET) != 0) {
    return harddisk_err_io;
  }

  TwoImgHeader_t hdr{};
  if (fread(&hdr, 1, sizeof(hdr), file.get()) != sizeof(hdr)) {
    return harddisk_err_invalid_format;
  }
  file.reset();

  if (memcmp(hdr.magic, "2IMG", 4) != 0) {
    return harddisk_err_invalid_format;
  }

  uint32_t data_offset = hdr.data_offset;
  if (data_offset == 0) {
    data_offset = two_img_header_size;
  }

  const bool is_locked = (hdr.flags & two_img_flag_locked) != 0;

  auto* image_ptr =
      block_disk_image_open(path, file_offset + data_offset, out_os_readonly);
  if (image_ptr == nullptr) {
    return harddisk_err_not_found;
  }

  if (is_locked) {
    block_disk_image_set_write_protected(image_ptr, true);
  }

  *out_instance_handle = static_cast<void*>(image_ptr);
  return harddisk_err_none;
}

auto two_img_close(void* instance_handle) -> void {
  if (instance_handle == nullptr) {
    return;
  }
  block_disk_image_close(static_cast<BlockDiskImage_t*>(instance_handle));
}

auto two_img_is_write_protected(void* instance_handle) -> bool {
  if (instance_handle == nullptr) {
    return true;
  }
  return block_disk_image_is_write_protected(
      static_cast<BlockDiskImage_t*>(instance_handle));
}

auto two_img_read_block(void* instance_handle, uint32_t block_num,
                        uint8_t* buffer) -> HarddiskError_e {
  if (instance_handle == nullptr) {
    return harddisk_err_io;
  }
  return block_disk_image_read_block(
      static_cast<BlockDiskImage_t*>(instance_handle), block_num, buffer);
}

auto two_img_write_block(void* instance_handle, uint32_t block_num,
                         const uint8_t* buffer) -> HarddiskError_e {
  if (instance_handle == nullptr) {
    return harddisk_err_io;
  }
  return block_disk_image_write_block(
      static_cast<BlockDiskImage_t*>(instance_handle), block_num, buffer);
}

auto two_img_get_total_blocks(void* instance_handle) -> uint32_t {
  if (instance_handle == nullptr) {
    return 0;
  }
  return block_disk_image_get_total_blocks(
      static_cast<BlockDiskImage_t*>(instance_handle));
}

const char* const g_two_img_creatable_exts[] = {".2mg", ".2img", ".2meg",
                                                nullptr};
const char* const g_two_img_supported_exts[] = {"2mg", "2img", "2meg", nullptr};
}  // namespace

extern "C" const HarddiskFormatDriver_t g_two_img_driver = {
    .abi_version = harddisk_format_abi_version,
    .capabilities = harddisk_driver_cap_write,
    .name = "2MG",
    .creatable_exts = g_two_img_creatable_exts,
    .supported_exts = g_two_img_supported_exts,
    .probe = two_img_probe,
    .open = two_img_open,
    .close = two_img_close,
    .is_write_protected = two_img_is_write_protected,
    .read_block = two_img_read_block,
    .write_block = two_img_write_block,
    .get_total_blocks = two_img_get_total_blocks,
};

// NOLINTEND(bugprone-easily-swappable-parameters,
// cppcoreguidelines-pro-type-static-cast-downcast, google-runtime-int)
