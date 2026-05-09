#include "apple2/peripherals/disk/formats/NibDriver.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr int NIB_TRACK_SIZE = 6656;
constexpr int NIB_TRACKS = 35;
constexpr int NIB_DISK_SIZE = NIB_TRACKS * NIB_TRACK_SIZE;  // 232960

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
  NibInstance& operator=(const NibInstance&) = delete;
  NibInstance(NibInstance&&) = delete;
  NibInstance& operator=(NibInstance&&) = delete;
};
}  // namespace

static DiskProbe_e NibProbe(const uint8_t* header, size_t header_size,
                            uint32_t file_size, const char* ext_hint) {
  (void)header;
  (void)header_size;
  (void)ext_hint;

  if (file_size == NIB_DISK_SIZE) {
    return DISK_PROBE_DEFINITE;
  }

  return DISK_PROBE_NO;
}

static DiskError_e NibOpen(const char* path, uint32_t file_offset,
                           bool* out_os_readonly, void** out_instance) {
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
  *out_instance = reinterpret_cast<void*>(instance);
  return DISK_ERR_NONE;
}

static void NibClose(void* instance) {
  delete reinterpret_cast<NibInstance*>(instance);
}

static bool NibIsWriteProtected(void* instance) {
  (void)instance;
  return false;
}

static void NibReadTrack(void* instance, int track, int phase,
                         uint8_t* trackImageBuffer, int* nibbles_out) {
  (void)phase;
  auto* ni = reinterpret_cast<NibInstance*>(instance);
  if (track < 0 || track >= NIB_TRACKS) {
    *nibbles_out = 0;
    return;
  }

  fseek(ni->file,
        static_cast<long>(ni->macbinary_offset + (track * NIB_TRACK_SIZE)),
        SEEK_SET);
  *nibbles_out =
      static_cast<int>(fread(trackImageBuffer, 1, NIB_TRACK_SIZE, ni->file));
}

static void NibWriteTrack(void* instance, int track, int phase,
                          const uint8_t* trackImage, int nibbles) {
  (void)phase;
  auto* ni = reinterpret_cast<NibInstance*>(instance);
  if (ni->os_readonly || track < 0 || track >= NIB_TRACKS) {
    return;
  }

  int to_write = std::min(nibbles, NIB_TRACK_SIZE);
  fseek(ni->file,
        static_cast<long>(ni->macbinary_offset + (track * NIB_TRACK_SIZE)),
        SEEK_SET);
  fwrite(trackImage, 1, static_cast<size_t>(to_write), ni->file);
}

static DiskError_e NibCreate(const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) {
    return DISK_ERR_IO;
  }

  uint8_t zero[1024];
  memset(zero, 0, sizeof(zero));
  for (int i = 0; i < NIB_DISK_SIZE / 1024; ++i) {
    fwrite(zero, 1, 1024, f);
  }
  // Handle remainder if any (though NIB_DISK_SIZE is a multiple of 1024)
  if (NIB_DISK_SIZE % 1024 != 0) {
    fwrite(zero, 1, NIB_DISK_SIZE % 1024, f);
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
    nullptr  // read_flux_bit
};
