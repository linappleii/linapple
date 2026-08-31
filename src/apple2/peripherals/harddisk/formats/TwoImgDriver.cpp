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

constexpr uint32_t two_img_header_size = 64;
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
    const uint32_t image_format = static_cast<uint32_t>(header_data[12]) |
                                  (static_cast<uint32_t>(header_data[13]) << 8) |
                                  (static_cast<uint32_t>(header_data[14]) << 16) |
                                  (static_cast<uint32_t>(header_data[15]) << 24);
    if (image_format == two_img_format_prodos) {
      return harddisk_probe_definite;
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

  if (fseek(file.get(), 0, SEEK_END) != 0) {
    return harddisk_err_io;
  }
  const long total_file_size = ftell(file.get());
  if (total_file_size < static_cast<long>(two_img_header_size) ||
      static_cast<long>(file_offset) >= total_file_size) {
    return harddisk_err_invalid_format;
  }

  if (fseek(file.get(), static_cast<long>(file_offset), SEEK_SET) != 0) {
    return harddisk_err_io;
  }

  uint8_t hdr[two_img_header_size] = {};
  if (fread(hdr, 1, sizeof(hdr), file.get()) != sizeof(hdr)) {
    return harddisk_err_invalid_format;
  }
  file.reset();

  if (memcmp(hdr, "2IMG", 4) != 0) {
    return harddisk_err_invalid_format;
  }

  const uint32_t flags = static_cast<uint32_t>(hdr[16]) |
                         (static_cast<uint32_t>(hdr[17]) << 8) |
                         (static_cast<uint32_t>(hdr[18]) << 16) |
                         (static_cast<uint32_t>(hdr[19]) << 24);

  uint32_t data_offset = static_cast<uint32_t>(hdr[24]) |
                         (static_cast<uint32_t>(hdr[25]) << 8) |
                         (static_cast<uint32_t>(hdr[26]) << 16) |
                         (static_cast<uint32_t>(hdr[27]) << 24);
  if (data_offset == 0) {
    data_offset = two_img_header_size;
  }

  if (data_offset < two_img_header_size ||
      static_cast<long>(file_offset + data_offset) >= total_file_size) {
    return harddisk_err_invalid_format;
  }

  const bool is_locked = (flags & two_img_flag_locked) != 0;

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
