// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/harddisk/formats/PoBlockDriver.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "apple2/peripherals/harddisk/HarddiskError.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/formats/BlockDiskImage.h"
#include "apple2/peripherals/harddisk/formats/HarddiskFormatRegistration.h"

// Justification: a C-compatible driver descriptor; its entry points share the
// ABI's signatures and its extension list is a C array.
// NOLINTBEGIN(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

namespace {

constexpr uint64_t block_size = 512;

auto is_dos_order_name(const char* ext_hint) -> bool {
  return strcmp(ext_hint, ".do") == 0 || strcmp(ext_hint, ".dsk") == 0;
}

auto is_two_img_name(const char* ext_hint) -> bool {
  return strcmp(ext_hint, ".2mg") == 0 || strcmp(ext_hint, ".2img") == 0 ||
         strcmp(ext_hint, ".2meg") == 0;
}

// Any file of whole blocks is a ProDOS-order image unless its name says
// otherwise; a 140 K file is also read for a file system, which can make the
// claim definite or hand the file to the DOS-order driver.
auto po_block_probe(const uint8_t* header_data, size_t header_size,
                    uint64_t file_size, const char* ext_hint)
    -> HarddiskProbe_e {
  const HarddiskProbe_e sig_probe = block_disk_image_probe_signature(
      header_data, header_size, file_size, block_disk_order_prodos);

  if (sig_probe == harddisk_probe_definite) {
    return harddisk_probe_definite;
  }

  if (file_size == 0 || (file_size % block_size) != 0) {
    return harddisk_probe_no;
  }

  if (ext_hint != nullptr &&
      (is_dos_order_name(ext_hint) || is_two_img_name(ext_hint))) {
    return harddisk_probe_no;
  }

  return harddisk_probe_possible;
}

auto po_block_open(const char* path, uint32_t file_offset, bool read_only,
                   void** out_instance) -> HarddiskError_e {
  return block_disk_image_open(path, file_offset, block_disk_order_prodos, 0,
                               read_only, out_instance);
}

const char* const po_block_supported_exts[] = {"po", "hdv", "img", nullptr};

}  // namespace

extern "C" const HarddiskFormatDriver_t g_po_block_driver = {
    .abi_version = harddisk_format_abi_version,
    .capabilities = harddisk_driver_cap_write,
    .name = "ProDOS Order",
    .supported_exts = po_block_supported_exts,
    .probe = po_block_probe,
    .open = po_block_open,
    .close = block_disk_image_close,
    .is_write_protected = block_disk_image_is_write_protected,
    .read_block = block_disk_image_read_block,
    .write_block = block_disk_image_write_block,
    .get_total_blocks = block_disk_image_get_total_blocks};

static const HarddiskFormatRegistration_t registration{&g_po_block_driver};

// NOLINTEND(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
