// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/Woz1Driver.h"

#include <array>
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
namespace woz1 {
constexpr char signature[] = "WOZ1\xFF\n\r\n";

// A 1.0 file lays INFO, TMAP and the TRKS chunk header out in its first 256
// bytes, and the track records follow as fixed-size cells with no index, so
// this prefix is the whole of what the driver keeps in memory.
constexpr int header_size = 256;
constexpr int trks_record_size = 6656;
constexpr int trks_bits_size = 6646;
constexpr int trks_bit_count_offset = trks_bits_size + 2;
constexpr int trks_trailer_size = 4;
}  // namespace woz1

struct Woz1Instance_t {
  FilePtr_t file{nullptr, fclose};
  std::array<uint8_t, woz1::header_size> header{};
  uint32_t tmap_offset = 0;
  uint32_t trks_offset = 0;
  uint32_t trks_record_count = 0;
  uint32_t base_offset = 0;
  bool format_write_protected = false;
  bool os_readonly = false;

  Woz1Instance_t() = default;
  ~Woz1Instance_t() = default;

  Woz1Instance_t(const Woz1Instance_t&) = delete;
  auto operator=(const Woz1Instance_t&) -> Woz1Instance_t& = delete;
  Woz1Instance_t(Woz1Instance_t&&) = default;
  auto operator=(Woz1Instance_t&&) -> Woz1Instance_t& = default;

  auto header_at(uint64_t offset, size_t len) const -> const uint8_t* {
    return woz_header_at(header.data(), header.size(), offset, len);
  }

  auto find_chunk(const char* id, uint32_t* out_size) const -> uint32_t {
    return woz_find_chunk(header.data(), header.size(), id, out_size);
  }
};
}  // namespace

static auto woz1_probe(const uint8_t* header_data, size_t header_size,
                       uint32_t file_size, const char* ext_hint)
    -> DiskProbe_e {
  (void)ext_hint;

  if (header_size < woz::signature_len || file_size < woz1::header_size) {
    return disk_probe_no;
  }

  if (memcmp(header_data, woz1::signature, woz::signature_len) == 0) {
    return disk_probe_definite;
  }

  return disk_probe_no;
}

static auto woz1_open(const char* path, uint32_t file_offset, bool read_only,
                      void** out_instance) -> DiskError_e {
  if (path == nullptr || out_instance == nullptr) {
    return disk_err_io;
  }
  auto wi_ptr = std::unique_ptr<Woz1Instance_t>(new Woz1Instance_t());

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
    return disk_err_io;
  }

  if (fseek(wi_ptr->file.get(), static_cast<long>(file_offset), SEEK_SET) !=
      0) {
    return disk_err_io;
  }

  if (fread(wi_ptr->header.data(), 1, woz1::header_size, wi_ptr->file.get()) !=
      static_cast<size_t>(woz1::header_size)) {
    return disk_err_io;
  }

  uint32_t trks_size = 0;
  const uint32_t info_ptr = wi_ptr->find_chunk("INFO", nullptr);
  wi_ptr->tmap_offset = wi_ptr->find_chunk("TMAP", nullptr);
  wi_ptr->trks_offset = wi_ptr->find_chunk("TRKS", &trks_size);

  if (info_ptr == 0 || wi_ptr->tmap_offset == 0 || wi_ptr->trks_offset == 0) {
    return disk_err_corrupt;
  }

  const uint8_t* const info_data =
      wi_ptr->header_at(info_ptr, woz::info_write_protect_offset + 1);
  if (info_data == nullptr ||
      wi_ptr->header_at(wi_ptr->tmap_offset, woz::tmap_entries) == nullptr) {
    return disk_err_corrupt;
  }

  if (info_data[woz::info_disk_type_offset] == woz::disk_type_3_5) {
    return disk_err_unsupported_format;
  }

  wi_ptr->format_write_protected =
      (info_data[woz::info_write_protect_offset] != 0);
  wi_ptr->trks_record_count = trks_size / woz1::trks_record_size;

  *out_instance = reinterpret_cast<void*>(wi_ptr.release());
  return disk_err_none;
}

static void woz1_close(void* instance) {
  if (instance == nullptr) {
    return;
  }
  delete reinterpret_cast<Woz1Instance_t*>(instance);
}

static auto woz1_is_write_protected(void* instance) -> bool {
  if (instance == nullptr) {
    return true;
  }
  auto* wi_ptr = reinterpret_cast<Woz1Instance_t*>(instance);
  return wi_ptr->os_readonly || wi_ptr->format_write_protected;
}

static auto woz1_read_track_bits(void* instance_handle, uint32_t quarter_track,
                                 uint8_t* bits, uint32_t max_bits,
                                 uint32_t* out_bit_count,
                                 uint8_t* out_bit_timing) -> DiskError_e {
  if (instance_handle == nullptr || bits == nullptr ||
      out_bit_count == nullptr || out_bit_timing == nullptr) {
    return disk_err_invalid_argument;
  }
  *out_bit_count = 0;

  auto* wi_ptr = reinterpret_cast<Woz1Instance_t*>(instance_handle);
  // A 1.0 INFO chunk carries no cell-time measurement, so every image is
  // taken at the nominal four microseconds.
  *out_bit_timing = disk_default_bit_timing;

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

  if (trks_index >= wi_ptr->trks_record_count) {
    return disk_err_corrupt;
  }

  // Track offsets are relative to the WOZ header, which a container may place
  // anywhere in the file, so every seek starts from where the header was read.
  const uint64_t record_offset =
      static_cast<uint64_t>(wi_ptr->base_offset) +
      static_cast<uint64_t>(wi_ptr->trks_offset) +
      (static_cast<uint64_t>(trks_index) * woz1::trks_record_size);

  const int64_t total_file_size = Path::file_size(wi_ptr->file.get());
  if (record_offset + woz1::trks_record_size >
      static_cast<uint64_t>(total_file_size)) {
    return disk_err_corrupt;
  }

  // The record's bit count sits after its cells rather than in an index, so
  // it takes a second seek to learn how much of the record is medium.
  std::array<uint8_t, woz1::trks_trailer_size> trailer{};
  if (fseek(wi_ptr->file.get(),
            static_cast<long>(record_offset + woz1::trks_bit_count_offset),
            SEEK_SET) != 0 ||
      fread(trailer.data(), 1, trailer.size(), wi_ptr->file.get()) !=
          trailer.size()) {
    return disk_err_io;
  }
  const uint32_t bit_count = read_u16_le(trailer.data());

  if (bit_count == 0 ||
      bit_count >
          static_cast<uint32_t>(woz1::trks_bits_size) * woz::bits_per_byte) {
    return disk_err_corrupt;
  }

  if (bit_count > max_bits) {
    return disk_err_unsupported;
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

const char* const g_woz1_supported_exts[] = {"woz", nullptr};

extern "C" const DiskFormatDriver_t g_woz1_driver = {
    .abi_version = disk_format_abi_version,
    .capabilities = 0,
    .name = "WOZ 1",
    .supported_exts = g_woz1_supported_exts,
    .probe = woz1_probe,
    .open = woz1_open,
    .close = woz1_close,
    .is_write_protected = woz1_is_write_protected,
    .read_track_bits = woz1_read_track_bits,
    .write_track_bits = nullptr,
    .create = nullptr};

static const DiskFormatRegistration_t k_reg{&g_woz1_driver};

// NOLINTEND(google-runtime-int, cppcoreguidelines-owning-memory, bugprone-easily-swappable-parameters, modernize-make-unique)
