// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/harddisk/formats/TwoImgDriver.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "apple2/peripherals/harddisk/HarddiskError.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/formats/BlockDiskImage.h"
#include "apple2/peripherals/harddisk/formats/HarddiskFormatRegistration.h"
#include "core/Util_Endian.h"
#include "core/Util_Path.h"

// Justification: a C-compatible driver descriptor; its entry points share the
// ABI's signatures and its extension list is a C array; google-runtime-int
// for fseek offsets.
// NOLINTBEGIN(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, google-runtime-int)

namespace {

constexpr uint32_t block_size = 512;

// The 2IMG header's fields by offset (the 2IMG specification): every field
// is little-endian.
namespace field {
constexpr size_t magic = 0x00;
constexpr size_t header_size = 0x08;
constexpr size_t version = 0x0A;
constexpr size_t image_format = 0x0C;
constexpr size_t flags = 0x10;
constexpr size_t block_count = 0x14;
constexpr size_t data_offset = 0x18;
constexpr size_t data_length = 0x1C;
constexpr size_t comment_offset = 0x20;
constexpr size_t comment_length = 0x24;
constexpr size_t creator_offset = 0x28;
constexpr size_t creator_length = 0x2C;
}  // namespace field

constexpr uint32_t flag_locked = 0x80000000U;
constexpr size_t magic_size = 4;

// A chunk whose length is zero is absent whatever its offset says ("zero if
// there is no Comment"); one that is present must lie whole between the end
// of the data and the end of the file, or it would be served as blocks.
auto chunk_is_placed(uint64_t data_end, uint64_t file_size, uint32_t offset,
                     uint32_t length) -> bool {
  if (length == 0) {
    return true;
  }
  const uint64_t begin = offset;
  const uint64_t end = begin + length;
  return begin >= data_end && end <= file_size;
}

auto two_img_probe(const uint8_t* header_data, size_t header_size,
                   uint64_t /*unused*/, const char* /*unused*/)
    -> HarddiskProbe {
  if (header_data == nullptr || header_size < magic_size) {
    return harddisk_probe_no;
  }
  if (memcmp(header_data + field::magic, "2IMG", magic_size) == 0) {
    return harddisk_probe_definite;
  }
  return harddisk_probe_no;
}

auto two_img_open(const char* path, uint32_t file_offset, bool read_only,
                  void** out_instance) -> HarddiskError {
  if (out_instance == nullptr) {
    return harddisk_err_io;
  }
  *out_instance = nullptr;
  if (path == nullptr) {
    return harddisk_err_io;
  }

  FilePtr file{fopen(path, "rb"), fclose};
  if (file == nullptr) {
    return harddisk_err_not_found;
  }

  const int64_t total_file_size = Path::file_size(file.get());
  if (total_file_size < 0) {
    return harddisk_err_io;
  }
  if (static_cast<uint64_t>(total_file_size) <
      static_cast<uint64_t>(file_offset) + two_img_header_size) {
    return harddisk_err_invalid_format;
  }

  if (fseek(file.get(), static_cast<long>(file_offset), SEEK_SET) != 0) {
    return harddisk_err_io;
  }

  std::array<uint8_t, two_img_header_size> header{};
  if (fread(header.data(), 1, header.size(), file.get()) != header.size()) {
    return harddisk_err_invalid_format;
  }
  file.reset();

  TwoImgHeader_t parsed{};
  const HarddiskError parse_error = two_img_parse(
      header.data(), static_cast<uint64_t>(total_file_size) - file_offset,
      &parsed);
  if (parse_error != harddisk_err_none) {
    return parse_error;
  }

  const BlockDiskOrder order = (parsed.image_format == two_img_format_dos)
                                     ? block_disk_order_dos
                                     : block_disk_order_prodos;
  return block_disk_image_open(path, file_offset + parsed.data_offset, order,
                               parsed.block_count, read_only || parsed.locked,
                               out_instance);
}

const char* const two_img_supported_exts[] = {"2mg", "2img", "2meg", nullptr};

}  // namespace

auto two_img_parse(const uint8_t* header, uint64_t file_size,
                   TwoImgHeader_t* out) -> HarddiskError {
  if (header == nullptr || out == nullptr) {
    return harddisk_err_io;
  }
  *out = TwoImgHeader_t{};

  if (memcmp(header + field::magic, "2IMG", magic_size) != 0) {
    return harddisk_err_invalid_format;
  }

  const uint16_t header_size = read_u16_le(header + field::header_size);
  if (header_size < two_img_header_size || header_size > file_size) {
    return harddisk_err_invalid_format;
  }
  (void)read_u16_le(header + field::version);

  const uint32_t image_format = read_u32_le(header + field::image_format);
  const uint32_t flags = read_u32_le(header + field::flags);
  const uint32_t block_count = read_u32_le(header + field::block_count);
  uint32_t data_offset = read_u32_le(header + field::data_offset);
  const uint32_t data_length = read_u32_le(header + field::data_length);

  if (data_offset == 0) {
    data_offset = header_size;
  }
  if (data_offset < header_size || data_offset > file_size) {
    return harddisk_err_invalid_format;
  }

  // The length comes from the field that names it; a ProDOS image may name
  // it twice ("for ProDOS should be 512 x Number of blocks"), and then the two
  // must agree.
  uint64_t length = data_length;
  if (length == 0 && image_format == two_img_format_prodos) {
    length = static_cast<uint64_t>(block_count) * block_size;
  }
  if (length == 0) {
    length = file_size - data_offset;
  }
  if (image_format == two_img_format_prodos && block_count != 0 &&
      data_length != 0 &&
      static_cast<uint64_t>(block_count) * block_size != data_length) {
    return harddisk_err_invalid_format;
  }
  if (length == 0 || (length % block_size) != 0 || length > UINT32_MAX ||
      data_offset + length > file_size) {
    return harddisk_err_invalid_format;
  }

  switch (image_format) {
    case two_img_format_prodos:
      break;
    // No 5.25-inch disk has another size; the volume-number flag bits
    // concern sector headers and mean nothing to a block device.
    case two_img_format_dos:
      if (length != block_disk_image_dos_size) {
        return harddisk_err_invalid_format;
      }
      break;
    case two_img_format_nibble:
      return harddisk_err_not_block_image;
    default:
      return harddisk_err_invalid_format;
  }

  const uint64_t data_end = data_offset + length;
  if (!chunk_is_placed(data_end, file_size,
                       read_u32_le(header + field::comment_offset),
                       read_u32_le(header + field::comment_length)) ||
      !chunk_is_placed(data_end, file_size,
                       read_u32_le(header + field::creator_offset),
                       read_u32_le(header + field::creator_length))) {
    return harddisk_err_invalid_format;
  }

  out->image_format = image_format;
  out->data_offset = data_offset;
  out->data_length = static_cast<uint32_t>(length);
  out->block_count = static_cast<uint32_t>(length / block_size);
  out->locked = (flags & flag_locked) != 0;
  return harddisk_err_none;
}

extern "C" const HarddiskFormatDriver_t g_two_img_driver = {
    .abi_version = harddisk_format_abi_version,
    .capabilities = harddisk_driver_cap_write,
    .name = "2MG",
    .supported_exts = two_img_supported_exts,
    .probe = two_img_probe,
    .open = two_img_open,
    .close = block_disk_image_close,
    .is_write_protected = block_disk_image_is_write_protected,
    .read_block = block_disk_image_read_block,
    .write_block = block_disk_image_write_block,
    .get_total_blocks = block_disk_image_get_total_blocks,
};

static const HarddiskFormatRegistration_t registration{&g_two_img_driver};

// NOLINTEND(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, google-runtime-int)
