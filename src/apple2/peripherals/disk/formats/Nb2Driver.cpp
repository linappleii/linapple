// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/Nb2Driver.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "apple2/peripherals/disk/DiskCommands.h"

// NOLINTBEGIN(cppcoreguidelines-owning-memory,google-runtime-int,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,cppcoreguidelines-pro-bounds-array-to-pointer-decay,modernize-use-trailing-return-type)

namespace {
constexpr int NB2_TRACK_SIZE = 6384;
constexpr int NB2_TRACKS = 35;
constexpr int NB2_DISK_SIZE = NB2_TRACKS * NB2_TRACK_SIZE;
constexpr size_t NB2_CHUNK_SIZE = 1024;

struct Nb2Instance {
  FILE* file = nullptr;
  uint32_t macbinary_offset = 0;
  bool os_readonly = false;

  Nb2Instance() = default;
  ~Nb2Instance() {
    if (file != nullptr) {
      fclose(file);
    }
  }

  Nb2Instance(const Nb2Instance&) = delete;
  auto operator=(const Nb2Instance&) -> Nb2Instance& = delete;
  Nb2Instance(Nb2Instance&&) = delete;
  auto operator=(Nb2Instance&&) -> Nb2Instance& = delete;
};
}  // namespace

static auto Nb2Probe(const uint8_t* header_data, size_t header_size,
                     uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
  (void)header_data;
  (void)header_size;
  (void)ext_hint;

  if (file_size == static_cast<uint32_t>(NB2_DISK_SIZE)) {
    return disk_probe_definite;
  }

  return disk_probe_no;
}

static auto Nb2Open(const char* path, uint32_t file_offset,
                    uint8_t enhanced_speed, bool* out_is_read_only,
                    void** out_instance) -> DiskError_e {
  (void)enhanced_speed;
  auto* instance = new Nb2Instance();
  instance->file = fopen(path, "r+b");
  if (instance->file != nullptr) {
    instance->os_readonly = false;
  } else {
    instance->file = fopen(path, "rb");
    if (instance->file != nullptr) {
      instance->os_readonly = true;
    } else {
      delete instance;
      return disk_err_io;
    }
  }

  if (out_is_read_only != nullptr) {
    *out_is_read_only = instance->os_readonly;
  }
  instance->macbinary_offset = file_offset;

  *out_instance = reinterpret_cast<void*>(instance);
  return disk_err_none;
}

static auto Nb2Close(void* instance) -> void {
  delete reinterpret_cast<Nb2Instance*>(instance);
}

static auto Nb2IsWriteProtected(void* instance) -> bool {
  return reinterpret_cast<Nb2Instance*>(instance)->os_readonly;
}

static auto Nb2ReadTrack(void* instance, int track, int phase,
                         uint8_t* track_buffer, int* out_nibbles) -> void {
  (void)phase;
  auto* ni = reinterpret_cast<Nb2Instance*>(instance);
  if (track < 0 || track >= NB2_TRACKS) {
    *out_nibbles = 0;
    return;
  }

  auto offset = static_cast<long>(ni->macbinary_offset) +
                static_cast<long>(track) * NB2_TRACK_SIZE;
  if (fseek(ni->file, offset, SEEK_SET) != 0) {
    *out_nibbles = 0;
    return;
  }

  if (fread(track_buffer, 1, NB2_TRACK_SIZE, ni->file) != NB2_TRACK_SIZE) {
    *out_nibbles = 0;
    return;
  }

  *out_nibbles = NB2_TRACK_SIZE;
}

static auto Nb2WriteTrack(void* instance, int track, int phase,
                          const uint8_t* track_buffer, int nibbles) -> void {
  (void)phase;
  (void)nibbles;
  auto* ni = reinterpret_cast<Nb2Instance*>(instance);
  if (ni->os_readonly || track < 0 || track >= NB2_TRACKS) return;

  auto offset = static_cast<long>(ni->macbinary_offset) +
                static_cast<long>(track) * NB2_TRACK_SIZE;
  if (fseek(ni->file, offset, SEEK_SET) == 0) {
    (void)fwrite(track_buffer, 1, NB2_TRACK_SIZE, ni->file);
  }
}

static auto Nb2Create(const char* path) -> DiskError_e {
  FILE* f = fopen(path, "wb");
  if (f == nullptr) return disk_err_io;

  std::array<uint8_t, NB2_CHUNK_SIZE> zero{};
  zero.fill(0);
  for (int i = 0; i < NB2_DISK_SIZE / static_cast<int>(NB2_CHUNK_SIZE); ++i) {
    fwrite(zero.data(), 1, zero.size(), f);
  }
  if (NB2_DISK_SIZE % static_cast<int>(NB2_CHUNK_SIZE) != 0) {
    fwrite(zero.data(), 1, static_cast<size_t>(NB2_DISK_SIZE % NB2_CHUNK_SIZE),
           f);
  }

  fclose(f);
  return disk_err_none;
}

static const char* const g_nb2_creatable_exts[] = {".nb2", nullptr};

extern "C" const DiskFormatDriver_t g_nb2_driver = {disk_format_abi_version,
                                                    disk_driver_cap_write,
                                                    "NB2 (6384-nibble)",
                                                    g_nb2_creatable_exts,
                                                    Nb2Probe,
                                                    Nb2Open,
                                                    Nb2Close,
                                                    Nb2IsWriteProtected,
                                                    Nb2ReadTrack,
                                                    Nb2WriteTrack,
                                                    Nb2Create,
                                                    nullptr,
                                                    nullptr};

// NOLINTEND(cppcoreguidelines-owning-memory,google-runtime-int,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,cppcoreguidelines-pro-bounds-array-to-pointer-decay,modernize-use-trailing-return-type)
