// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/NibDriver.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "apple2/peripherals/disk/DiskCommands.h"

// NOLINTBEGIN(bugprone-easily-swappable-parameters,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,cppcoreguidelines-pro-type-static-cast-downcast,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,google-runtime-int,cppcoreguidelines-pro-bounds-array-to-pointer-decay)

namespace {
constexpr int NIB_TRACKS = 35;
constexpr int NIB_DISK_SIZE = NIB_TRACKS * static_cast<int>(nibbles_per_track);
constexpr int CREATE_BUFFER_SIZE = 1024;

struct NibInstance {
  FILE* file = nullptr;
  uint32_t macbinary_offset = 0;
  bool os_readonly = false;

  NibInstance() = default;
  virtual ~NibInstance() {
    if (file) {
      fclose(file);
    }
  }

  NibInstance(const NibInstance&) = delete;
  auto operator=(const NibInstance&) -> NibInstance& = delete;
  NibInstance(NibInstance&&) = delete;
  auto operator=(NibInstance&&) -> NibInstance& = delete;
};
}  // namespace

static auto NibProbe(const uint8_t* header_data, size_t header_size,
                     uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
  (void)header_data;
  (void)header_size;
  (void)ext_hint;

  if (file_size == static_cast<uint32_t>(NIB_DISK_SIZE)) {
    return disk_probe_definite;
  }

  return disk_probe_no;
}

static auto NibOpen(const char* path, uint32_t file_offset,
                    uint8_t enhanced_speed, bool* out_is_read_only,
                    void** out_instance) -> DiskError_e {
  (void)enhanced_speed;
  auto* instance = new NibInstance();
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

  *out_instance = static_cast<void*>(instance);
  return disk_err_none;
}

static void NibClose(void* instance) {
  delete static_cast<NibInstance*>(instance);
}

static auto NibIsWriteProtected(void* instance) -> bool {
  return static_cast<NibInstance*>(instance)->os_readonly;
}

static void NibReadTrack(void* instance, int track, int phase,
                         uint8_t* track_buffer, int* out_nibbles) {
  (void)phase;
  auto* ni = static_cast<NibInstance*>(instance);
  if (track < 0 || track >= NIB_TRACKS) {
    *out_nibbles = 0;
    return;
  }

  auto offset =
      static_cast<int64_t>(ni->macbinary_offset) +
      (static_cast<int64_t>(track) * static_cast<int64_t>(nibbles_per_track));

  if (fseek(ni->file, static_cast<long>(offset), SEEK_SET) != 0) {
    *out_nibbles = 0;
    return;
  }

  if (fread(track_buffer, 1, nibbles_per_track, ni->file) !=
      nibbles_per_track) {
    *out_nibbles = 0;
    return;
  }

  *out_nibbles = static_cast<int>(nibbles_per_track);
}

static void NibWriteTrack(void* instance, int track, int phase,
                          const uint8_t* track_buffer, int nibbles) {
  (void)phase;
  (void)nibbles;
  auto* ni = static_cast<NibInstance*>(instance);
  if (ni->os_readonly || track < 0 || track >= NIB_TRACKS) {
    return;
  }

  auto offset =
      static_cast<int64_t>(ni->macbinary_offset) +
      (static_cast<int64_t>(track) * static_cast<int64_t>(nibbles_per_track));

  if (fseek(ni->file, static_cast<long>(offset), SEEK_SET) == 0) {
    (void)fwrite(track_buffer, 1, nibbles_per_track, ni->file);
  }
}

static auto NibCreate(const char* path) -> DiskError_e {
  FILE* f = fopen(path, "wb");
  if (!f) {
    return disk_err_io;
  }

  std::array<uint8_t, CREATE_BUFFER_SIZE> zero{};
  zero.fill(0);

  for (int i = 0; i < NIB_DISK_SIZE / CREATE_BUFFER_SIZE; ++i) {
    fwrite(zero.data(), 1, zero.size(), f);
  }

  if (NIB_DISK_SIZE % CREATE_BUFFER_SIZE != 0) {
    fwrite(zero.data(), 1,
           static_cast<size_t>(NIB_DISK_SIZE % CREATE_BUFFER_SIZE), f);
  }

  fclose(f);
  return disk_err_none;
}

static const char* const g_nib_creatable_exts[] = {".nib", nullptr};

extern "C" const DiskFormatDriver_t g_nib_driver = {disk_format_abi_version,
                                                    disk_driver_cap_write,
                                                    "NIB (6656-nibble)",
                                                    g_nib_creatable_exts,
                                                    NibProbe,
                                                    NibOpen,
                                                    NibClose,
                                                    NibIsWriteProtected,
                                                    NibReadTrack,
                                                    NibWriteTrack,
                                                    NibCreate,
                                                    nullptr,
                                                    nullptr};

// NOLINTEND(bugprone-easily-swappable-parameters,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,cppcoreguidelines-pro-type-static-cast-downcast,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,google-runtime-int,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
