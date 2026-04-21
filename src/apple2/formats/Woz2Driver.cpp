#include "apple2/formats/Woz2Driver.h"

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr char WOZ2_SIGNATURE[] = "WOZ2\xFF\n\r\n";
constexpr size_t WOZ2_SIGNATURE_LEN = 8;
constexpr int WOZ2_HEADER_SIZE = 1536;
constexpr int WOZ2_DATA_BLOCK_SIZE = 512;
constexpr int WOZ2_TMAP_OFFSET = 88;
constexpr int WOZ2_TMAP_SIZE = 160;
constexpr int WOZ2_TRKS_OFFSET = 256;
constexpr int WOZ2_TRK_SIZE = 8;
constexpr uint8_t WOZ2_UNRECORDED_TRACK = 0xFF;

constexpr int WOZ2_INFO_DISK_TYPE_OFFSET = 21;
constexpr int WOZ2_INFO_WRITE_PROTECT_OFFSET = 22;

constexpr int WOZ2_BITS_PER_BYTE = 8;

constexpr int WOZ2_QUARTER_TRACKS_PER_TRACK = 4;
constexpr int WOZ2_SYNC_BYTES_16SECTOR = 4;
constexpr int WOZ2_TRAILING_ZEROS_16SECTOR = 2;
constexpr int WOZ2_SYNC_BYTES_13SECTOR = 7;

constexpr int NIBBLES_PER_TRACK = 6656;

struct Woz2Instance {
  FILE* file = nullptr;
  std::array<uint8_t, WOZ2_HEADER_SIZE> header{};
  bool format_write_protected = false;
  bool os_readonly = false;

  Woz2Instance() = default;
  virtual ~Woz2Instance() {
    if (file) {
      fclose(file);
    }
  }

  // Not copyable/movable
  Woz2Instance(const Woz2Instance&) = delete;
  auto operator=(const Woz2Instance&) -> Woz2Instance& = delete;
  Woz2Instance(Woz2Instance&&) = delete;
  auto operator=(Woz2Instance&&) -> Woz2Instance& = delete;
};

static uint32_t woz2_scan_sync_bytes(const uint8_t* buffer, uint32_t bit_count,
                                     uint32_t sync_bytes_needed,
                                     uint32_t trailing_0_count) {
  auto fetch_bit = [&](uint32_t idx) -> int {
    return ((buffer[idx / 8] & (0x80 >> (idx % 8))) != 0) ? 1 : 0;
  };

  uint32_t i = 0;
  while (i < bit_count) {
    while (i < bit_count && fetch_bit(i) == 0) {
      ++i;
    }
    if (i >= bit_count) {
      break;
    }
    uint32_t nr_FFs = 0;
    uint32_t start_i = i;
    while (i < bit_count) {
      uint32_t nr_1s = 0;
      while (i < bit_count && fetch_bit(i) == 1) {
        ++nr_1s;
        ++i;
      }
      if (nr_1s < 8) {
        break;
      }
      if (nr_1s > 8) {
        nr_FFs = 1;
      }
      uint32_t nr_0s = 0;
      while (i < bit_count && fetch_bit(i) == 0) {
        ++nr_0s;
        ++i;
      }
      if (nr_0s != trailing_0_count) {
        break;
      }
      if (++nr_FFs == sync_bytes_needed) {
        return i;
      }
    }
    i = start_i + 1;
  }
  return 0;
}
}  // namespace

static auto Woz2Probe(const uint8_t* header, size_t header_size,
                            uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
  (void)ext_hint;

  if (header_size >= WOZ2_SIGNATURE_LEN && file_size >= WOZ2_HEADER_SIZE) {
    if (memcmp(header, WOZ2_SIGNATURE, WOZ2_SIGNATURE_LEN) == 0) {
      return DISK_PROBE_DEFINITE;
    }
  }

  return DISK_PROBE_NO;
}

static auto Woz2Open(const char* path, uint32_t file_offset,
                            bool* out_os_readonly, void** out_instance) -> DiskError_e {
  auto* instance = new Woz2Instance();
  instance->file = fopen(path, "r+b");
  if (instance->file != nullptr) {
    instance->os_readonly = false;
  } else {
    instance->file = fopen(path, "rb");
    if (instance->file != nullptr) {
      instance->os_readonly = true;
    } else {
      delete instance;
      return DISK_ERR_IO;
    }
  }

  if (out_os_readonly != nullptr) {
    *out_os_readonly = instance->os_readonly;
  }

  // WOZ2 files are usually not MacBinary-wrapped, but we support the offset
  // just in case.
  if (fseek(instance->file, static_cast<long>(file_offset), SEEK_SET) != 0) {
    fclose(instance->file);
    instance->file = nullptr;
    delete instance;
    return DISK_ERR_IO;
  }
  if (fread(instance->header.data(), 1, WOZ2_HEADER_SIZE, instance->file) !=
      WOZ2_HEADER_SIZE) {
    fclose(instance->file);
    instance->file = nullptr;
    delete instance;
    return DISK_ERR_CORRUPT;
  }

  // Parse INFO chunk (starts at offset 12)
  // header[21] = disk type (1 = 5.25", 2 = 3.5")
  if (instance->header[WOZ2_INFO_DISK_TYPE_OFFSET] == 2) {
    fclose(instance->file);
    instance->file = nullptr;
    delete instance;
    return DISK_ERR_UNSUPPORTED_FORMAT;
  }

  // header[22] = write protected
  instance->format_write_protected =
      (instance->header[WOZ2_INFO_WRITE_PROTECT_OFFSET] != 0);

  *out_instance = reinterpret_cast<void*>(instance);
  return DISK_ERR_NONE;
}

static void Woz2Close(void* instance) {
  delete reinterpret_cast<Woz2Instance*>(instance);
}

static auto Woz2IsWriteProtected(void* instance) -> bool {
  return reinterpret_cast<Woz2Instance*>(instance)->format_write_protected;
}

static void Woz2ReadTrack(void* instance, int track, int phase,
                         uint8_t* trackImageBuffer, int* nibbles_out) {
  (void)phase;
  auto* wi = reinterpret_cast<Woz2Instance*>(instance);

  // TODO(davidbaucum): Implement half-track support for accurate head-positioning
  // Currently we only read integral tracks using the base quarter-track index.
  uint32_t tmap_index =
      static_cast<uint32_t>(track) * WOZ2_QUARTER_TRACKS_PER_TRACK;
  if (tmap_index >= static_cast<uint32_t>(WOZ2_TMAP_SIZE)) {
    *nibbles_out = 0;
    return;
  }

  uint8_t trks_index = wi->header[WOZ2_TMAP_OFFSET + tmap_index];
  if (trks_index == WOZ2_UNRECORDED_TRACK) {
    for (int i = 0; i < NIBBLES_PER_TRACK; ++i) {
      trackImageBuffer[i] = static_cast<uint8_t>(rand() & 0xFF);
    }
    *nibbles_out = NIBBLES_PER_TRACK;
    return;
  }

  // Defensive check: ensure trks_index points within the header buffer.
  // TRKS chunk starts at 256, each entry is 8 bytes. Header is 1536 bytes.
  // (1536 - 256) / 8 = 160 entries max.
  if (trks_index >= 160) {
    *nibbles_out = 0;
    return;
  }

  const uint8_t* trk =
      &wi->header[WOZ2_TRKS_OFFSET + (trks_index * WOZ2_TRK_SIZE)];
  uint16_t starting_block = trk[0] | (static_cast<uint16_t>(trk[1]) << 8);
  uint16_t block_count = trk[2] | (static_cast<uint16_t>(trk[3]) << 8);
  uint32_t bit_count = trk[4] | (static_cast<uint32_t>(trk[5]) << 8) |
                       (static_cast<uint32_t>(trk[6]) << 16) |
                       (static_cast<uint32_t>(trk[7]) << 24);

  if (bit_count == 0) {
    *nibbles_out = 0;
    return;
  }

  uint32_t byte_count =
      static_cast<uint32_t>(block_count) * WOZ2_DATA_BLOCK_SIZE;

  // Defensive check: ensure bit_count does not exceed the available data bytes.
  if (bit_count > byte_count * 8) {
    *nibbles_out = 0;
    return;
  }

  std::vector<uint8_t> buffer(byte_count);
  fseek(wi->file,
        static_cast<long>(static_cast<uint32_t>(starting_block) *
                          WOZ2_DATA_BLOCK_SIZE),
        SEEK_SET);
  if (fread(buffer.data(), 1, byte_count, wi->file) != byte_count) {
    *nibbles_out = 0;
    return;
  }

  uint32_t i =
      woz2_scan_sync_bytes(buffer.data(), bit_count, WOZ2_SYNC_BYTES_16SECTOR,
                           WOZ2_TRAILING_ZEROS_16SECTOR);
  if (i == 0) {
    i = woz2_scan_sync_bytes(buffer.data(), bit_count, WOZ2_SYNC_BYTES_13SECTOR,
                             0);
  }

  if (i == 0) {
    *nibbles_out = 0;
    return;
  }

  auto fetch_bit_loop = [&](uint32_t idx) -> int {
    uint32_t bit_idx = idx % bit_count;
    return ((buffer[bit_idx / WOZ2_BITS_PER_BYTE] &
             (0x80 >> (bit_idx % WOZ2_BITS_PER_BYTE))) != 0)
               ? 1
               : 0;
  };

  uint32_t bits_left = bit_count;
  int nibbles_done = 0;
  while (nibbles_done < NIBBLES_PER_TRACK && bits_left >= 8) {
    while (bits_left > 0 && fetch_bit_loop(i) == 0) {
      ++i;
      --bits_left;
    }
    if (bits_left < 8) {
      break;
    }
    uint8_t nibble = 0;
    for (int b = 0; b < 8; ++b) {
      nibble = static_cast<uint8_t>((nibble << 1) | fetch_bit_loop(i));
      ++i;
    }
    bits_left -= 8;
    trackImageBuffer[nibbles_done++] = nibble;
  }
  *nibbles_out = nibbles_done;
}

extern "C" const DiskFormatDriver_t g_woz2_driver = {
    LINAPPLE_DISK_ABI_VERSION,
    0x01,  // capabilities (bit 0 = write)
    "WOZ 2",
    nullptr,  // creatable_exts
    Woz2Probe,
    Woz2Open,
    Woz2Close,
    Woz2IsWriteProtected,
    Woz2ReadTrack,
    nullptr,  // write_track
    nullptr,  // create
    nullptr   // read_flux_bit
};
