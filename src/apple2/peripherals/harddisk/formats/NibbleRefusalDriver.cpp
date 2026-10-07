// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/harddisk/formats/NibbleRefusalDriver.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "apple2/peripherals/harddisk/HarddiskError.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/formats/HarddiskFormatRegistration.h"

// Justification: a C-compatible driver descriptor; its entry points share the
// ABI's signatures and its extension list is a C array.
// NOLINTBEGIN(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

// A nibble or flux image records the surface of a floppy, not its blocks, so
// a block device has nothing to serve from it. This driver exists to claim
// such files by name or magic and refuse them with a code that says why,
// instead of letting a 512-multiple of nibbles pass as blocks.

namespace {

constexpr size_t woz_magic_size = 4;

auto nibble_refusal_probe(const uint8_t* header_data, size_t header_size,
                          uint64_t, const char* ext_hint) -> HarddiskProbe_e {
  if (ext_hint != nullptr &&
      (strcmp(ext_hint, ".nib") == 0 || strcmp(ext_hint, ".nb2") == 0 ||
       strcmp(ext_hint, ".woz") == 0)) {
    return harddisk_probe_definite;
  }
  if (header_data != nullptr && header_size >= woz_magic_size &&
      (memcmp(header_data, "WOZ1", woz_magic_size) == 0 ||
       memcmp(header_data, "WOZ2", woz_magic_size) == 0)) {
    return harddisk_probe_definite;
  }
  return harddisk_probe_no;
}

auto nibble_refusal_open(const char*, uint32_t, bool, void** out_instance)
    -> HarddiskError_e {
  if (out_instance != nullptr) {
    *out_instance = nullptr;
  }
  return harddisk_err_not_block_image;
}

// open never hands out an instance, so none of the entry points below is ever
// reached; each is present so the loader admits the driver by the same test
// as any other and the card never has to ask whether a member exists.
auto nibble_refusal_close(void*) -> void {}

auto nibble_refusal_is_write_protected(void*) -> bool { return true; }

auto nibble_refusal_read_block(void*, uint32_t, uint8_t*) -> HarddiskError_e {
  return harddisk_err_not_block_image;
}

auto nibble_refusal_write_block(void*, uint32_t, const uint8_t*)
    -> HarddiskError_e {
  return harddisk_err_not_block_image;
}

auto nibble_refusal_get_total_blocks(void*) -> uint32_t { return 0; }

const char* const nibble_refusal_supported_exts[] = {"nib", "nb2", "woz",
                                                     nullptr};

}  // namespace

extern "C" const HarddiskFormatDriver_t g_nibble_refusal_driver = {
    .abi_version = harddisk_format_abi_version,
    .capabilities = harddisk_driver_cap_write,
    .name = "Nibble image",
    .supported_exts = nibble_refusal_supported_exts,
    .probe = nibble_refusal_probe,
    .open = nibble_refusal_open,
    .close = nibble_refusal_close,
    .is_write_protected = nibble_refusal_is_write_protected,
    .read_block = nibble_refusal_read_block,
    .write_block = nibble_refusal_write_block,
    .get_total_blocks = nibble_refusal_get_total_blocks};

static const HarddiskFormatRegistration_t registration{
    &g_nibble_refusal_driver};

// NOLINTEND(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
