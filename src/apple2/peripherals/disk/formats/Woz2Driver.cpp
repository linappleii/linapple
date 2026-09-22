// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/Woz2Driver.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/formats/DiskFormatRegistration.h"
#include "apple2/peripherals/disk/formats/WozChunk.h"
#include "core/Util_Endian.h"
#include "core/Util_Path.h"

// NOLINTBEGIN(google-runtime-int, cppcoreguidelines-owning-memory, bugprone-easily-swappable-parameters, modernize-make-unique)
// Justification:
// This module uses procedural patterns for C-compatibility. google-runtime-int
// is required for fseek offsets. owning-memory and make-unique are suppressed
// for C++11 compatibility and handle-based resource management.
// easily-swappable-parameters is mandated by the Disk Driver ABI signatures.

namespace {
namespace woz2 {
constexpr char signature[] = "WOZ2\xFF\n\r\n";
constexpr int header_size = 1536;
constexpr int data_block_size = 512;
constexpr int trks_entry_size = 8;
constexpr int info_optimal_bit_timing_offset = 39;
constexpr uint8_t info_version_2_0 = 2;
constexpr uint8_t info_version_2_1 = 3;

// The specification places the first TRKS data block right after the
// 1,536-byte header, so blocks 0 to 2 can only be a corrupt entry.
constexpr uint16_t first_data_block = 3;

// A record spanning more blocks than the ABI track buffer holds can never be
// read whole; the specification's own largest tracks stay under 14 blocks.
constexpr uint32_t bits_per_block = data_block_size * woz::bits_per_byte;
constexpr uint32_t max_track_blocks = max_track_bits / bits_per_block;
}  // namespace woz2

struct Woz2Instance_t {
  FilePtr_t file{nullptr, fclose};
  std::array<uint8_t, woz2::header_size> header{};
  uint32_t tmap_offset = 0;
  uint32_t trks_offset = 0;
  uint32_t base_offset = 0;
  uint8_t optimal_bit_timing = disk_default_bit_timing;
  bool format_write_protected = false;
  bool os_readonly = false;

  Woz2Instance_t() = default;
  ~Woz2Instance_t() = default;

  Woz2Instance_t(const Woz2Instance_t&) = delete;
  auto operator=(const Woz2Instance_t&) -> Woz2Instance_t& = delete;
  Woz2Instance_t(Woz2Instance_t&&) = default;
  auto operator=(Woz2Instance_t&&) -> Woz2Instance_t& = default;

  auto header_at(uint64_t offset, size_t len) const -> const uint8_t* {
    return woz_header_at(header.data(), header.size(), offset, len);
  }

  auto find_chunk(const char* id) const -> uint32_t {
    return woz_find_chunk(header.data(), header.size(), id, nullptr);
  }
};
}  // namespace

static auto woz2_probe(const uint8_t* header_data, size_t header_size,
                       uint32_t file_size, const char* ext_hint)
    -> DiskProbe_e {
  (void)ext_hint;

  if (header_data == nullptr || header_size < woz::signature_len ||
      file_size < woz2::header_size) {
    return disk_probe_no;
  }

  if (memcmp(header_data, woz2::signature, woz::signature_len) == 0) {
    return disk_probe_definite;
  }

  return disk_probe_no;
}

static auto woz2_open(const char* path, uint32_t file_offset, bool read_only,
                      void** out_instance) -> DiskError_e {
  if (out_instance == nullptr) {
    return disk_err_invalid_argument;
  }
  *out_instance = nullptr;
  if (path == nullptr) {
    return disk_err_invalid_argument;
  }
  auto wi_ptr = std::unique_ptr<Woz2Instance_t>(new Woz2Instance_t());

  wi_ptr->base_offset = file_offset;
  wi_ptr->os_readonly = read_only;
  if (!read_only) {
    wi_ptr->file.reset(fopen(path, "r+b"));
  }
  if (wi_ptr->file == nullptr) {
    wi_ptr->file.reset(fopen(path, "rb"));
    wi_ptr->os_readonly = true;
  }
  if (wi_ptr->file == nullptr) {
    return (errno == ENOENT) ? disk_err_file_not_found : disk_err_io;
  }

  if (fseek(wi_ptr->file.get(), static_cast<long>(file_offset), SEEK_SET) !=
      0) {
    return disk_err_io;
  }

  if (fread(wi_ptr->header.data(), 1, woz2::header_size, wi_ptr->file.get()) !=
      static_cast<size_t>(woz2::header_size)) {
    return disk_err_io;
  }

  const uint32_t info_ptr = wi_ptr->find_chunk("INFO");
  wi_ptr->tmap_offset = wi_ptr->find_chunk("TMAP");
  wi_ptr->trks_offset = wi_ptr->find_chunk("TRKS");

  if (info_ptr == 0 || wi_ptr->tmap_offset == 0 || wi_ptr->trks_offset == 0) {
    return disk_err_corrupt;
  }

  const uint8_t* const info_data =
      wi_ptr->header_at(info_ptr, woz::info_write_protect_offset + 1);
  if (info_data == nullptr ||
      wi_ptr->header_at(wi_ptr->tmap_offset, woz::tmap_entries) == nullptr ||
      wi_ptr->header_at(wi_ptr->trks_offset, woz2::trks_entry_size) ==
          nullptr) {
    return disk_err_corrupt;
  }

  const uint8_t info_version = info_data[woz::info_version_offset];
  if (info_version != woz2::info_version_2_0 &&
      info_version != woz2::info_version_2_1) {
    return disk_err_unsupported_format;
  }

  if (info_data[woz::info_disk_type_offset] != woz::disk_type_5_25) {
    return disk_err_unsupported_format;
  }

  wi_ptr->format_write_protected =
      (info_data[woz::info_write_protect_offset] != 0);

  // An INFO chunk that predates the field, or leaves it zero, is saying it
  // has no measurement to offer, which is the nominal four microseconds.
  const uint8_t* const timing =
      wi_ptr->header_at(info_ptr + woz2::info_optimal_bit_timing_offset, 1);
  if (timing != nullptr && *timing != 0) {
    wi_ptr->optimal_bit_timing = *timing;
  }

  const DiskError_e crc_err =
      woz_verify_crc32(wi_ptr->file.get(), wi_ptr->base_offset,
                       read_u32_le(wi_ptr->header.data() + woz::crc32_offset));
  if (crc_err != disk_err_none) {
    return crc_err;
  }

  *out_instance = reinterpret_cast<void*>(wi_ptr.release());
  return disk_err_none;
}

static void woz2_close(void* instance) {
  if (instance == nullptr) {
    return;
  }
  delete reinterpret_cast<Woz2Instance_t*>(instance);
}

static auto woz2_is_write_protected(void* instance) -> bool {
  if (instance == nullptr) {
    return true;
  }
  auto* wi_ptr = reinterpret_cast<Woz2Instance_t*>(instance);
  return wi_ptr->os_readonly || wi_ptr->format_write_protected;
}

static auto woz2_read_track_bits(void* instance_handle, uint32_t quarter_track,
                                 uint8_t* bits, uint32_t max_bits,
                                 uint32_t* out_bit_count,
                                 uint8_t* out_bit_timing) -> DiskError_e {
  if (instance_handle == nullptr || bits == nullptr ||
      out_bit_count == nullptr || out_bit_timing == nullptr) {
    return disk_err_invalid_argument;
  }
  *out_bit_count = 0;

  auto* wi_ptr = reinterpret_cast<Woz2Instance_t*>(instance_handle);
  *out_bit_timing = wi_ptr->optimal_bit_timing;

  if (quarter_track >= static_cast<uint32_t>(woz::tmap_entries)) {
    return disk_err_invalid_argument;
  }

  const uint8_t* const tmap_entry =
      wi_ptr->header_at(wi_ptr->tmap_offset + quarter_track, 1);
  if (tmap_entry == nullptr) {
    return disk_err_corrupt;
  }

  // An unmapped quarter track is surface the image never recorded, which the
  // card hears as noise rather than as a failure to read.
  const uint8_t trks_index = *tmap_entry;
  if (trks_index == woz::unrecorded_track) {
    return disk_err_none;
  }

  if (trks_index >= woz::tmap_entries) {
    return disk_err_corrupt;
  }

  const uint64_t entry_offset =
      static_cast<uint64_t>(wi_ptr->trks_offset) +
      (static_cast<uint64_t>(trks_index) * woz2::trks_entry_size);
  const uint8_t* const trk =
      wi_ptr->header_at(entry_offset, woz2::trks_entry_size);
  if (trk == nullptr) {
    return disk_err_corrupt;
  }
  const uint16_t starting_block = read_u16_le(&trk[0]);
  const uint16_t block_count = read_u16_le(&trk[2]);
  const uint32_t bit_count = read_u32_le(&trk[4]);

  if (block_count == 0 || starting_block < woz2::first_data_block) {
    return disk_err_corrupt;
  }

  if (block_count > woz2::max_track_blocks) {
    return disk_err_unsupported;
  }

  const uint32_t byte_count =
      static_cast<uint32_t>(block_count) * woz2::data_block_size;

  if (bit_count == 0 || bit_count > byte_count * woz::bits_per_byte) {
    return disk_err_corrupt;
  }

  if (bit_count > max_bits) {
    return disk_err_unsupported;
  }

  const int64_t total_file_size = Path::file_size(wi_ptr->file.get());
  // Block numbers count from the WOZ header, which a container may place
  // anywhere in the file, so the seek starts from where the header was read.
  const uint64_t record_offset =
      static_cast<uint64_t>(wi_ptr->base_offset) +
      (static_cast<uint64_t>(starting_block) * woz2::data_block_size);
  if (record_offset + byte_count > static_cast<uint64_t>(total_file_size)) {
    return disk_err_corrupt;
  }

  if (fseek(wi_ptr->file.get(), static_cast<long>(record_offset), SEEK_SET) !=
      0) {
    return disk_err_io;
  }

  // WOZ stores the track the way the head sees it, first cell in the most
  // significant bit, so the TRK payload is the medium with no translation.
  const uint32_t cell_bytes = (bit_count + 7U) / 8U;
  if (fread(bits, 1, cell_bytes, wi_ptr->file.get()) != cell_bytes) {
    return disk_err_io;
  }

  *out_bit_count = bit_count;
  return disk_err_none;
}

const char* const woz2_supported_exts[] = {"woz", nullptr};

extern "C" const DiskFormatDriver_t g_woz2_driver = {
    .abi_version = disk_format_abi_version,
    .capabilities = 0,
    .name = "WOZ 2",
    .supported_exts = woz2_supported_exts,
    .probe = woz2_probe,
    .open = woz2_open,
    .close = woz2_close,
    .is_write_protected = woz2_is_write_protected,
    .read_track_bits = woz2_read_track_bits,
    .write_track_bits = nullptr,
    .create = nullptr};

static const DiskFormatRegistration_t registration{&g_woz2_driver};

// NOLINTEND(google-runtime-int, cppcoreguidelines-owning-memory, bugprone-easily-swappable-parameters, modernize-make-unique)
