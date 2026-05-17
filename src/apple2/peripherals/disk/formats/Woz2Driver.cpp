// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/Woz2Driver.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "apple2/peripherals/disk/DiskCommands.h"

namespace {
constexpr char WOZ2_SIGNATURE[] = "WOZ2\xFF\n\r\n";
constexpr size_t WOZ2_SIGNATURE_LEN = 8;
constexpr int WOZ2_HEADER_SIZE = 1536;
constexpr int WOZ2_DATA_BLOCK_SIZE = 512;
constexpr uint8_t WOZ2_UNRECORDED_TRACK = 0xFF;

constexpr int WOZ2_CHUNK_ID_SIZE = 4;
constexpr int WOZ2_CHUNK_HEADER_SIZE = 8;
constexpr int WOZ2_FILE_HEADER_SIZE = 12;
constexpr int WOZ2_TMAP_ENTRIES = 160;
constexpr int WOZ2_TRKS_ENTRY_SIZE = 8;

constexpr int WOZ2_INFO_DISK_TYPE_OFFSET = 1;
constexpr int WOZ2_INFO_WRITE_PROTECT_OFFSET = 2;
constexpr int WOZ2_DISK_TYPE_3_5 = 2;

constexpr int BITS_PER_BYTE = 8;
constexpr int SHIFT_16 = 16;
constexpr int SHIFT_24 = 24;
constexpr uint8_t BIT_HIGH_MASK = 0x80;
constexpr uint8_t BYTE_MASK = 0xFF;

constexpr int WOZ2_CHUNK_SIZE_OFFSET_0 = 4;
constexpr int WOZ2_CHUNK_SIZE_OFFSET_1 = 5;
constexpr int WOZ2_CHUNK_SIZE_OFFSET_2 = 6;
constexpr int WOZ2_CHUNK_SIZE_OFFSET_3 = 7;

constexpr int WOZ2_TRKS_START_BLOCK_OFFSET = 0;
constexpr int WOZ2_TRKS_BLOCK_COUNT_OFFSET = 2;
constexpr int WOZ2_TRKS_BIT_COUNT_OFFSET = 4;

struct Woz2Instance {
  FILE* file = nullptr;
  std::array<uint8_t, WOZ2_HEADER_SIZE> header{};
  uint32_t tmap_offset = 0;
  uint32_t trks_offset = 0;
  bool format_write_protected = false;

  Woz2Instance() = default;
  virtual ~Woz2Instance() {
    if (file) {
      fclose(file);
    }
  }

  Woz2Instance(const Woz2Instance&) = delete;
  auto operator=(const Woz2Instance&) -> Woz2Instance& = delete;
  Woz2Instance(Woz2Instance&&) = delete;
  auto operator=(Woz2Instance&&) -> Woz2Instance& = delete;
};

static auto FindChunk(const uint8_t* header, const char* id) -> uint32_t {
  for (uint32_t i = WOZ2_FILE_HEADER_SIZE;
       i < static_cast<uint32_t>(WOZ2_HEADER_SIZE) - WOZ2_CHUNK_HEADER_SIZE;) {
    if (memcmp(&header[i], id, WOZ2_CHUNK_ID_SIZE) == 0) {
      return i + WOZ2_CHUNK_HEADER_SIZE;
    }
    uint32_t chunk_size =
        static_cast<uint32_t>(header[i + WOZ2_CHUNK_SIZE_OFFSET_0]) |
        (static_cast<uint32_t>(header[i + WOZ2_CHUNK_SIZE_OFFSET_1])
         << BITS_PER_BYTE) |
        (static_cast<uint32_t>(header[i + WOZ2_CHUNK_SIZE_OFFSET_2])
         << SHIFT_16) |
        (static_cast<uint32_t>(header[i + WOZ2_CHUNK_SIZE_OFFSET_3])
         << SHIFT_24);
    i += WOZ2_CHUNK_HEADER_SIZE + chunk_size;
  }
  return 0;
}

}  // namespace

static auto Woz2Probe(const uint8_t* header_data, size_t header_size,
                      uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
  (void)ext_hint;

  if (header_size >= WOZ2_SIGNATURE_LEN && file_size >= WOZ2_HEADER_SIZE) {
    if (memcmp(header_data, WOZ2_SIGNATURE, WOZ2_SIGNATURE_LEN) == 0) {
      return disk_probe_definite;
    }
  }

  return disk_probe_no;
}

static auto Woz2Open(const char* path, uint32_t file_offset,
                     uint8_t enhanced_speed, bool* out_is_read_only,
                     void** out_instance) -> DiskError_e {
  (void)enhanced_speed;
  auto instance = std::unique_ptr<Woz2Instance>(new Woz2Instance());

  bool is_readonly = false;
  instance->file = fopen(path, "r+b");
  if (instance->file != nullptr) {
    is_readonly = false;
  } else {
    instance->file = fopen(path, "rb");
    if (instance->file != nullptr) {
      is_readonly = true;
    } else {
      return disk_err_io;
    }
  }

  if (out_is_read_only != nullptr) {
    *out_is_read_only = is_readonly;
  }

  if (fseek(instance->file, static_cast<long>(file_offset), SEEK_SET) != 0) {
    return disk_err_io;
  }

  if (fread(instance->header.data(), 1, WOZ2_HEADER_SIZE, instance->file) !=
      static_cast<size_t>(WOZ2_HEADER_SIZE)) {
    return disk_err_io;
  }

  uint32_t info_ptr = FindChunk(instance->header.data(), "INFO");
  instance->tmap_offset = FindChunk(instance->header.data(), "TMAP");
  instance->trks_offset = FindChunk(instance->header.data(), "TRKS");

  if (info_ptr == 0 || instance->tmap_offset == 0 ||
      instance->trks_offset == 0) {
    return disk_err_corrupt;
  }

  if (instance->header[info_ptr + WOZ2_INFO_DISK_TYPE_OFFSET] ==
      WOZ2_DISK_TYPE_3_5) {
    return disk_err_unsupported_format;
  }

  instance->format_write_protected =
      (instance->header[info_ptr + WOZ2_INFO_WRITE_PROTECT_OFFSET] != 0);

  *out_instance = reinterpret_cast<void*>(instance.release());
  return disk_err_none;
}

static void Woz2Close(void* instance) {
  delete reinterpret_cast<Woz2Instance*>(instance);
}

static auto Woz2IsWriteProtected(void* instance) -> bool {
  return reinterpret_cast<Woz2Instance*>(instance)->format_write_protected;
}

static void Woz2ReadTrack(void* instance, int track, int phase,
                          uint8_t* track_buffer, int* out_nibbles) {
  (void)track;
  auto* wi = reinterpret_cast<Woz2Instance*>(instance);

  const uint32_t tmap_index =
      static_cast<uint32_t>(phase) * (4 / phases_per_track);
  if (tmap_index >= static_cast<uint32_t>(WOZ2_TMAP_ENTRIES)) {
    *out_nibbles = 0;
    return;
  }

  uint8_t trks_index = wi->header[wi->tmap_offset + tmap_index];
  if (trks_index == WOZ2_UNRECORDED_TRACK) {
    for (int i = 0; i < static_cast<int>(nibbles_per_track); ++i) {
      track_buffer[i] = static_cast<uint8_t>(rand() & BYTE_MASK);
    }
    *out_nibbles = static_cast<int>(nibbles_per_track);
    return;
  }

  if (trks_index >= WOZ2_TMAP_ENTRIES) {
    *out_nibbles = 0;
    return;
  }

  const uint8_t* trk =
      &wi->header[wi->trks_offset +
                  (static_cast<uint32_t>(trks_index) * WOZ2_TRKS_ENTRY_SIZE)];
  uint16_t starting_block = static_cast<uint16_t>(trk[0]) |
                            (static_cast<uint16_t>(trk[1]) << BITS_PER_BYTE);
  uint16_t block_count = static_cast<uint16_t>(trk[2]) |
                         (static_cast<uint16_t>(trk[3]) << BITS_PER_BYTE);
  uint32_t bit_count = static_cast<uint32_t>(trk[4]) |
                       (static_cast<uint32_t>(trk[5]) << BITS_PER_BYTE) |
                       (static_cast<uint32_t>(trk[6]) << 16) |
                       (static_cast<uint32_t>(trk[7]) << 24);

  if (bit_count == 0 || bit_count > static_cast<uint32_t>(block_count) *
                                        WOZ2_DATA_BLOCK_SIZE * BITS_PER_BYTE) {
    *out_nibbles = 0;
    return;
  }

  uint32_t byte_count =
      static_cast<uint32_t>(block_count) * WOZ2_DATA_BLOCK_SIZE;
  std::vector<uint8_t> buffer(byte_count);
  fseek(wi->file,
        static_cast<long>(static_cast<uint32_t>(starting_block) *
                          WOZ2_DATA_BLOCK_SIZE),
        SEEK_SET);
  if (fread(buffer.data(), 1, byte_count, wi->file) != byte_count) {
    *out_nibbles = 0;
    return;
  }

  auto fetch_bit_loop = [&](uint32_t idx) -> int {
    uint32_t bit_idx = idx % bit_count;
    return ((buffer[bit_idx / BITS_PER_BYTE] &
             (BIT_HIGH_MASK >> (bit_idx % BITS_PER_BYTE))) != 0)
               ? 1
               : 0;
  };

  memset(track_buffer, BYTE_MASK, nibbles_per_track);
  uint32_t i = 0;
  uint32_t processed_bits = 0;
  int nibbles_done = 0;

  while (processed_bits < bit_count &&
         nibbles_done < static_cast<int>(nibbles_per_track)) {
    while (processed_bits < bit_count && fetch_bit_loop(i) == 0) {
      i++;
      processed_bits++;
    }
    if (processed_bits + BITS_PER_BYTE > bit_count) {
      break;
    }
    uint8_t nibble = 0;
    for (int b = 0; b < BITS_PER_BYTE; ++b) {
      nibble = static_cast<uint8_t>((nibble << 1) | fetch_bit_loop(i + b));
    }
    track_buffer[nibbles_done++] = nibble;
    i += BITS_PER_BYTE;
    processed_bits += BITS_PER_BYTE;
  }

  *out_nibbles = nibbles_done;
}

extern "C" const DiskFormatDriver_t g_woz2_driver = {disk_format_abi_version,
                                                     disk_driver_cap_write,
                                                     "WOZ 2",
                                                     nullptr,
                                                     Woz2Probe,
                                                     Woz2Open,
                                                     Woz2Close,
                                                     Woz2IsWriteProtected,
                                                     Woz2ReadTrack,
                                                     nullptr,
                                                     nullptr,
                                                     nullptr,
                                                     nullptr};
