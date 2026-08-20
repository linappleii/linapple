// SPDX-License-Identifier: GPL-2.0-only
#include <cstring>

#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/formats/BlockDiskImage.h"

// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// cppcoreguidelines-pro-type-static-cast-downcast) Justification: Format
// drivers utilize a procedural C-compatible handle system and standardized
// probing signatures mandated by the Harddisk subsystem ABI.

namespace {

constexpr uint32_t block_size = 512;

auto raw_hd_probe(const uint8_t* header_data, size_t header_size,
                  uint32_t file_size, const char* ext_hint) -> HarddiskProbe_e {
  (void)header_data;
  (void)header_size;

  if (file_size > 0 && (file_size % block_size) == 0) {
    if (ext_hint != nullptr &&
        (strcmp(ext_hint, ".hdv") == 0 || strcmp(ext_hint, ".po") == 0 ||
         strcmp(ext_hint, ".2meg") == 0 || strcmp(ext_hint, ".2mg") == 0 ||
         strcmp(ext_hint, ".img") == 0 || strcmp(ext_hint, ".bin") == 0)) {
      return harddisk_probe_possible;
    }
    return harddisk_probe_possible;
  }

  return harddisk_probe_no;
}

auto raw_hd_open(const char* path, uint32_t file_offset, bool* out_os_readonly,
                 void** out_instance_handle) -> HarddiskError_e {
  if (path == nullptr || out_instance_handle == nullptr) {
    return harddisk_err_io;
  }

  auto* image_ptr = block_disk_image_open(path, file_offset, out_os_readonly);
  if (image_ptr == nullptr) {
    return harddisk_err_not_found;
  }

  *out_instance_handle = static_cast<void*>(image_ptr);
  return harddisk_err_none;
}

auto raw_hd_close(void* instance_handle) -> void {
  if (instance_handle == nullptr) {
    return;
  }
  block_disk_image_close(static_cast<BlockDiskImage_t*>(instance_handle));
}

auto raw_hd_is_write_protected(void* instance_handle) -> bool {
  if (instance_handle == nullptr) {
    return true;
  }
  return block_disk_image_is_write_protected(
      static_cast<BlockDiskImage_t*>(instance_handle));
}

auto raw_hd_read_block(void* instance_handle, uint32_t block_num,
                       uint8_t* buffer) -> HarddiskError_e {
  if (instance_handle == nullptr) {
    return harddisk_err_io;
  }
  return block_disk_image_read_block(
      static_cast<BlockDiskImage_t*>(instance_handle), block_num, buffer);
}

auto raw_hd_write_block(void* instance_handle, uint32_t block_num,
                        const uint8_t* buffer) -> HarddiskError_e {
  if (instance_handle == nullptr) {
    return harddisk_err_io;
  }
  return block_disk_image_write_block(
      static_cast<BlockDiskImage_t*>(instance_handle), block_num, buffer);
}

auto raw_hd_get_total_blocks(void* instance_handle) -> uint32_t {
  if (instance_handle == nullptr) {
    return 0;
  }
  return block_disk_image_get_total_blocks(
      static_cast<BlockDiskImage_t*>(instance_handle));
}

const char* const g_raw_hd_creatable_exts[] = {".hdv", ".po", nullptr};
}  // namespace

extern "C" const HarddiskFormatDriver_t g_raw_hd_driver = {
    .abi_version = harddisk_format_abi_version,
    .capabilities = harddisk_driver_cap_write,
    .name = "Raw",
    .creatable_exts = g_raw_hd_creatable_exts,
    .probe = raw_hd_probe,
    .open = raw_hd_open,
    .close = raw_hd_close,
    .is_write_protected = raw_hd_is_write_protected,
    .read_block = raw_hd_read_block,
    .write_block = raw_hd_write_block,
    .get_total_blocks = raw_hd_get_total_blocks};

// NOLINTEND(bugprone-easily-swappable-parameters,
// cppcoreguidelines-pro-type-static-cast-downcast)
