#include "apple2/peripherals/disk/formats/Woz2Driver.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr char WOZ2_SIGNATURE[] = "WOZ2\xFF\n\r\n";
constexpr size_t WOZ2_SIGNATURE_LEN = 8;
constexpr int WOZ2_HEADER_SIZE = 1536;
constexpr int WOZ2_DATA_BLOCK_SIZE = 512;
constexpr uint8_t WOZ2_UNRECORDED_TRACK = 0xFF;
constexpr int NIBBLES_PER_TRACK = 6656;

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

  // Not copyable/movable
  Woz2Instance(const Woz2Instance&) = delete;
  auto operator=(const Woz2Instance&) -> Woz2Instance& = delete;
  Woz2Instance(Woz2Instance&&) = delete;
  auto operator=(Woz2Instance&&) -> Woz2Instance& = delete;
};

static auto FindChunk(const uint8_t* header, const char* id) -> uint32_t {
  for (uint32_t i = 12; i < WOZ2_HEADER_SIZE - 8;) {
    if (memcmp(&header[i], id, 4) == 0) {
      return i + 8;
    }
    uint32_t chunk_size = header[i + 4] | (header[i + 5] << 8) |
                          (header[i + 6] << 16) | (header[i + 7] << 24);
    i += 8 + chunk_size;
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

static auto Woz2Open(const char* path, uint32_t file_offset, uint8_t enhanced_speed,
                     bool* out_os_readonly, void** out_instance) -> DiskError_e {
  (void)enhanced_speed;
  auto* instance = new Woz2Instance();

  instance->file = fopen(path, "r+b");
  if (instance->file != nullptr) {
    *out_os_readonly = false;
  } else {
    instance->file = fopen(path, "rb");
    if (instance->file != nullptr) {
      *out_os_readonly = true;
    } else {
      delete instance;
      return DISK_ERR_IO;
    }
  }

  if (fseek(instance->file, static_cast<long>(file_offset), SEEK_SET) != 0) {
    delete instance;
    return DISK_ERR_IO;
  }

  if (fread(instance->header.data(), 1, WOZ2_HEADER_SIZE, instance->file) !=
      WOZ2_HEADER_SIZE) {
    delete instance;
    return DISK_ERR_IO;
  }

  uint32_t info_ptr = FindChunk(instance->header.data(), "INFO");
  instance->tmap_offset = FindChunk(instance->header.data(), "TMAP");
  instance->trks_offset = FindChunk(instance->header.data(), "TRKS");

  if (!info_ptr || !instance->tmap_offset || !instance->trks_offset) {
    delete instance;
    return DISK_ERR_CORRUPT;
  }

  // WOZ 2.0 INFO Data: offset 0=Version, 1=DiskType
  if (instance->header[info_ptr + 1] == 2) {
    delete instance;
    return DISK_ERR_UNSUPPORTED_FORMAT;
  }

  instance->format_write_protected = (instance->header[info_ptr + 2] != 0);

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
  (void)track;
  auto* wi = reinterpret_cast<Woz2Instance*>(instance);

  // 'phase' from Disk.cpp is 0-79 (half-tracks). TMAP needs 0-159
  // (quarter-tracks).
  uint32_t tmap_index = static_cast<uint32_t>(phase) * 2;
  if (tmap_index >= 160) {
    *nibbles_out = 0;
    return;
  }

  uint8_t trks_index = wi->header[wi->tmap_offset + tmap_index];
  if (trks_index == WOZ2_UNRECORDED_TRACK) {
    for (int i = 0; i < NIBBLES_PER_TRACK; ++i) {
      trackImageBuffer[i] = static_cast<uint8_t>(rand() & 0xFF);
    }
    *nibbles_out = NIBBLES_PER_TRACK;
    return;
  }

  if (trks_index >= 160) {
    *nibbles_out = 0;
    return;
  }

  const uint8_t* trk = &wi->header[wi->trks_offset + (trks_index * 8)];
  uint16_t starting_block = trk[0] | (static_cast<uint16_t>(trk[1]) << 8);
  uint16_t block_count = trk[2] | (static_cast<uint16_t>(trk[3]) << 8);
  uint32_t bit_count = trk[4] | (static_cast<uint32_t>(trk[5]) << 8) |
                       (static_cast<uint32_t>(trk[6]) << 16) |
                       (static_cast<uint32_t>(trk[7]) << 24);

  if (bit_count == 0 || bit_count > static_cast<uint32_t>(block_count) * WOZ2_DATA_BLOCK_SIZE * 8) {
    *nibbles_out = 0;
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
    *nibbles_out = 0;
    return;
  }

  auto fetch_bit_loop = [&](uint32_t idx) -> int {
    uint32_t bit_idx = idx % bit_count;
    return ((buffer[bit_idx / 8] & (0x80 >> (bit_idx % 8))) != 0) ? 1 : 0;
  };

  // Compact Nibblization for Fast Disk Mode
  // We extract valid 8-bit nibbles and pad the rest of the 6656-byte buffer
  // with 0xFF sync bytes to perfectly emulate a standard .nib track length.
  memset(trackImageBuffer, 0xFF, NIBBLES_PER_TRACK);
  uint32_t i = 0;
  uint32_t processed_bits = 0;
  int nibbles_done = 0;

  while (processed_bits < bit_count && nibbles_done < NIBBLES_PER_TRACK) {
    while (processed_bits < bit_count && fetch_bit_loop(i) == 0) {
      i++;
      processed_bits++;
    }
    if (processed_bits + 8 > bit_count) break;
    uint8_t nibble = 0;
    for (int b = 0; b < 8; ++b) {
      nibble = static_cast<uint8_t>((nibble << 1) | fetch_bit_loop(i + b));
    }
    trackImageBuffer[nibbles_done++] = nibble;
    i += 8;
    processed_bits += 8;
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
    nullptr,  // command
    nullptr   // read_flux_bit
};
