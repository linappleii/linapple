#include "apple2/peripherals/disk/formats/NibDriver.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// NOLINTBEGIN(bugprone-easily-swappable-parameters,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,cppcoreguidelines-pro-type-static-cast-downcast,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,google-runtime-int,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// Rationale: These functions implement a C-compatible ABI defined in
// DiskFormatDriver.h. Swappable parameters are mandated by that interface.
// Manual memory management for NibInstance and FILE* is handled within the
// procedural driver lifecycle. Creatable extensions array is part of the ABI.
// fseek requires 'long'.

namespace {
constexpr int NIB_TRACK_SIZE = 6656;
constexpr int NIB_TRACKS = 35;
constexpr int NIB_DISK_SIZE = NIB_TRACKS * NIB_TRACK_SIZE;  // 232960
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

static auto NibProbe(const uint8_t* header, size_t header_size,
                     uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
  (void)header;
  (void)header_size;
  (void)ext_hint;

  if (file_size == static_cast<uint32_t>(NIB_DISK_SIZE)) {
    return DISK_PROBE_DEFINITE;
  }

  return DISK_PROBE_NO;
}

static auto NibOpen(const char* path, uint32_t file_offset, uint8_t enhanced_speed,
                    bool* out_os_readonly, void** out_instance) -> DiskError_e {
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
      return DISK_ERR_IO;
    }
  }

  if (out_os_readonly != nullptr) {
    *out_os_readonly = instance->os_readonly;
  }
  instance->macbinary_offset = file_offset;

  *out_instance = static_cast<void*>(instance);
  return DISK_ERR_NONE;
}

static void NibClose(void* instance) {
  delete static_cast<NibInstance*>(instance);
}

static auto NibIsWriteProtected(void* instance) -> bool {
  return static_cast<NibInstance*>(instance)->os_readonly;
}

static void NibReadTrack(void* instance, int track, int phase,
                         uint8_t* trackImageBuffer, int* nibbles_out) {
  (void)phase;
  auto* ni = static_cast<NibInstance*>(instance);
  if (track < 0 || track >= NIB_TRACKS) {
    *nibbles_out = 0;
    return;
  }

  auto offset = static_cast<int64_t>(ni->macbinary_offset) +
                (static_cast<int64_t>(track) * NIB_TRACK_SIZE);

  if (fseek(ni->file, static_cast<long>(offset), SEEK_SET) != 0) {
    *nibbles_out = 0;
    return;
  }

  if (fread(trackImageBuffer, 1, NIB_TRACK_SIZE, ni->file) != NIB_TRACK_SIZE) {
    *nibbles_out = 0;
    return;
  }

  *nibbles_out = NIB_TRACK_SIZE;
}

static void NibWriteTrack(void* instance, int track, int phase,
                          const uint8_t* trackImage, int nibbles) {
  (void)phase;
  (void)nibbles;
  auto* ni = static_cast<NibInstance*>(instance);
  if (ni->os_readonly || track < 0 || track >= NIB_TRACKS) {
    return;
  }

  auto offset = static_cast<int64_t>(ni->macbinary_offset) +
                (static_cast<int64_t>(track) * NIB_TRACK_SIZE);

  if (fseek(ni->file, static_cast<long>(offset), SEEK_SET) == 0) {
    (void)fwrite(trackImage, 1, NIB_TRACK_SIZE, ni->file);
  }
}

static auto NibCreate(const char* path) -> DiskError_e {
  FILE* f = fopen(path, "wb");
  if (!f) {
    return DISK_ERR_IO;
  }

  std::array<uint8_t, CREATE_BUFFER_SIZE> zero{};
  zero.fill(0);

  for (int i = 0; i < NIB_DISK_SIZE / CREATE_BUFFER_SIZE; ++i) {
    fwrite(zero.data(), 1, zero.size(), f);
  }

  if (NIB_DISK_SIZE % CREATE_BUFFER_SIZE != 0) {
    fwrite(zero.data(), 1, NIB_DISK_SIZE % CREATE_BUFFER_SIZE, f);
  }

  fclose(f);
  return DISK_ERR_NONE;
}

static const char* const g_nib_creatable_exts[] = {".nib", nullptr};

extern "C" const DiskFormatDriver_t g_nib_driver = {
    LINAPPLE_DISK_ABI_VERSION,
    DRIVER_CAP_WRITE,
    "NIB (6656-nibble)",
    g_nib_creatable_exts,
    NibProbe,
    NibOpen,
    NibClose,
    NibIsWriteProtected,
    NibReadTrack,
    NibWriteTrack,
    NibCreate,
    nullptr,  // command
    nullptr   // read_flux_bit
};

// NOLINTEND(bugprone-easily-swappable-parameters,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,cppcoreguidelines-pro-type-static-cast-downcast,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,google-runtime-int,cppcoreguidelines-pro-bounds-array-to-pointer-decay)
