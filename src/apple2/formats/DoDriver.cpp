#include "apple2/formats/DoDriver.h"

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "apple2/DiskCommands.h"
#include "apple2/DiskGCR.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

// TODO(davidbaucum): pass via driver config, not global
extern bool enhancedisk;

// TODO(davidbaucum): share between DO/PO
namespace {
constexpr int DISK_SIZE_140K = 143360;
constexpr int MIN_140K_DISK_SIZE = 143105;
constexpr int MAX_140K_DISK_SIZE = 143364;
constexpr int DISK_SIZE_140K_ALT1 = 143403;
constexpr int DISK_SIZE_140K_ALT2 = 143488;
constexpr int DOS_TRACK_SIZE = 4096;
constexpr int VTOC_OFFSET = 0x11000;
constexpr int PAGE_SIZE = 0x0100;
constexpr int PRODOS_BLOCK_SIZE = 512;

struct DoInstance {
  FILE* file = nullptr;
  uint32_t macbinary_offset = 0;
  bool os_readonly = false;
  std::array<uint8_t, GCR_WORKBUF_SIZE> work_buffer{};

  DoInstance() = default;
  virtual ~DoInstance() {
    if (file) {
      fclose(file);
    }
  }

  DoInstance(const DoInstance&) = delete;
  auto operator=(const DoInstance&) -> DoInstance& = delete;
  DoInstance(DoInstance&&) = delete;
  auto operator=(DoInstance&&) -> DoInstance& = delete;
};
}  // namespace

static auto DoProbe(const uint8_t* header, size_t header_size,
                           uint32_t file_size, const char* ext_hint) -> DiskProbe_e {
  (void)header;
  (void)ext_hint;
  if (file_size < MIN_140K_DISK_SIZE || file_size > MAX_140K_DISK_SIZE) {
    if (file_size != DISK_SIZE_140K_ALT1 && file_size != DISK_SIZE_140K_ALT2) {
      return DISK_PROBE_NO;
    }
  }

  // DOS VTOC structure check (track 17 sector-order byte sequence)
  if (header_size >= static_cast<size_t>(VTOC_OFFSET + 2 + (15 * PAGE_SIZE))) {
    bool mismatch = false;
    for (int loop = 1; loop <= 15; ++loop) {
      if (header[VTOC_OFFSET + 2 + (loop * PAGE_SIZE)] != loop - 1) {
        mismatch = true;
        break;
      }
    }
    if (!mismatch) {
      return DISK_PROBE_DEFINITE;
    }
  }

  // ProDOS bitmap chain check as secondary heuristic
  if (header_size >= static_cast<size_t>((5 * PRODOS_BLOCK_SIZE) + PAGE_SIZE + 2)) {
    bool mismatch = false;
    for (int loop = 2; loop <= 5; ++loop) {
      uint16_t next = *reinterpret_cast<const uint16_t*>(
          header + (loop * PRODOS_BLOCK_SIZE) + PAGE_SIZE);
      uint16_t prev = *reinterpret_cast<const uint16_t*>(
          header + (loop * PRODOS_BLOCK_SIZE) + PAGE_SIZE + 2);
      if ((next != ((loop == 5) ? 0 : 6 - loop)) ||
          (prev != ((loop == 2) ? 0 : 8 - loop))) {
        mismatch = true;
        break;
      }
    }
    if (!mismatch) {
      return DISK_PROBE_DEFINITE;
    }
  }

  return DISK_PROBE_POSSIBLE;
}

static auto DoOpen(const char* path, uint32_t file_offset,
                           bool* out_os_readonly, void** out_instance) -> DiskError_e {
  auto* instance = new DoInstance();
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

static void DoClose(void* instance) {
  delete reinterpret_cast<DoInstance*>(instance);
}

static auto DoIsWriteProtected(void* instance) -> bool {
  (void)instance;
  return false;
}

static void DoReadTrack(void* instance, int track, int phase,
                        uint8_t* trackImageBuffer, int* nibbles_out) {
  (void)phase;
  auto* di = reinterpret_cast<DoInstance*>(instance);
  if (track < 0 || track >= TRACKS) {
    *nibbles_out = 0;
    return;
  }
  std::fill(di->work_buffer.begin(), di->work_buffer.end(), 0);
  if (fseek(di->file,
            static_cast<long>(di->macbinary_offset + (track * DOS_TRACK_SIZE)),
            SEEK_SET) != 0) {
    *nibbles_out = 0;
    return;
  }
  if (fread(di->work_buffer.data(), 1, DOS_TRACK_SIZE, di->file) != DOS_TRACK_SIZE) {
    *nibbles_out = 0;
    return;
  }

  uint32_t nibbles =
      GCR_NibblizeTrack(di->work_buffer.data(), trackImageBuffer, true, track);
  *nibbles_out = static_cast<int>(nibbles);

  if (!enhancedisk) {
    GCR_SkewTrack(di->work_buffer.data(), track, *nibbles_out, trackImageBuffer);
  }
}

static void DoWriteTrack(void* instance, int track, int phase,
                         const uint8_t* trackImage, int nibbles) {
  (void)phase;
  auto* di = reinterpret_cast<DoInstance*>(instance);
  if (di->os_readonly || track < 0 || track >= TRACKS) return;

  std::fill(di->work_buffer.begin(), di->work_buffer.end(), 0);
  GCR_DenibblizeTrack(di->work_buffer.data(), const_cast<uint8_t*>(trackImage), true,
                      nibbles);
  if (fseek(di->file,
            static_cast<long>(di->macbinary_offset + (track * DOS_TRACK_SIZE)),
            SEEK_SET) == 0) {
    (void)fwrite(di->work_buffer.data(), 1, DOS_TRACK_SIZE, di->file);
  }
}

static auto DoCreate(const char* path) -> DiskError_e {
  FILE* f = fopen(path, "wb");
  if (!f) return DISK_ERR_IO;

  std::array<uint8_t, 1024> zero{};
  zero.fill(0);
  for (int i = 0; i < DISK_SIZE_140K / 1024; ++i) {
    fwrite(zero.data(), 1, zero.size(), f);
  }
  fclose(f);
  return DISK_ERR_NONE;
}

static const char* const g_do_creatable_exts[] = {".do", ".dsk", nullptr};

extern "C" const DiskFormatDriver_t g_do_driver = {
    LINAPPLE_DISK_ABI_VERSION,
    DRIVER_CAP_WRITE,
    "DOS Order",
    g_do_creatable_exts,
    DoProbe,
    DoOpen,
    DoClose,
    DoIsWriteProtected,
    DoReadTrack,
    DoWriteTrack,
    DoCreate,
    nullptr  // read_flux_bit
};
