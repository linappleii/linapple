/*
 * RawHdDriver.cpp - LinApple Raw Harddisk Format Driver
 */

#include <sys/stat.h>

#include <array>
#include <cstdio>
#include <cstring>

#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"

namespace {
constexpr uint32_t kBlockSize = 512;

struct RawHdInstance {
  FILE* file{nullptr};
  uint32_t total_blocks{0};
  bool os_readonly{false};

  RawHdInstance() = default;
  ~RawHdInstance() {
    if (file) {
      fclose(file);
    }
  }

  // Disable copying and moving
  RawHdInstance(const RawHdInstance&) = delete;
  RawHdInstance& operator=(const RawHdInstance&) = delete;
  RawHdInstance(RawHdInstance&&) = delete;
  RawHdInstance& operator=(RawHdInstance&&) = delete;
};
}  // namespace

static auto RawHd_Probe(const uint8_t* header, size_t header_size,
                        uint32_t file_size, const char* ext_hint)
    -> HarddiskProbe_e {
  (void)header;
  (void)header_size;

  // Raw images should be a multiple of kBlockSize bytes
  if (file_size > 0 && (file_size % kBlockSize) == 0) {
    if (strcmp(ext_hint, ".hdv") == 0 || strcmp(ext_hint, ".po") == 0 ||
        strcmp(ext_hint, ".2mg") != 0) {
      // .2mg is handled by another driver (once implemented), so we avoid
      // claiming it definitely
      return HARDDISK_PROBE_POSSIBLE;
    }
  }

  return HARDDISK_PROBE_NO;
}

static auto RawHd_Open(const char* path, bool* out_os_readonly,
                       void** out_instance) -> HarddiskError_e {
  auto* instance = new RawHdInstance();

  instance->file = fopen(path, "r+b");
  if (instance->file) {
    instance->os_readonly = false;
  } else {
    instance->file = fopen(path, "rb");
    if (instance->file) {
      instance->os_readonly = true;
    } else {
      delete instance;
      return HARDDISK_ERR_NOT_FOUND;
    }
  }

  fseek(instance->file, 0, SEEK_END);
  instance->total_blocks =
      static_cast<uint32_t>(ftell(instance->file) / kBlockSize);
  fseek(instance->file, 0, SEEK_SET);

  if (out_os_readonly) {
    *out_os_readonly = instance->os_readonly;
  }
  *out_instance = static_cast<void*>(instance);
  return HARDDISK_ERR_NONE;
}

static auto RawHd_Close(void* instance) -> void {
  delete static_cast<RawHdInstance*>(instance);
}

static auto RawHd_IsWriteProtected(void* instance) -> bool {
  auto* ri = static_cast<RawHdInstance*>(instance);
  return ri->os_readonly;
}

static auto RawHd_ReadBlock(void* instance, uint32_t block_num, uint8_t* buffer)
    -> HarddiskError_e {
  auto* ri = static_cast<RawHdInstance*>(instance);
  if (block_num >= ri->total_blocks) {
    return HARDDISK_ERR_IO;
  }

  if (fseek(ri->file, static_cast<long>(block_num) * kBlockSize, SEEK_SET) !=
      0) {
    return HARDDISK_ERR_IO;
  }

  if (fread(buffer, 1, kBlockSize, ri->file) != kBlockSize) {
    return HARDDISK_ERR_IO;
  }

  return HARDDISK_ERR_NONE;
}

static auto RawHd_WriteBlock(void* instance, uint32_t block_num,
                             const uint8_t* buffer) -> HarddiskError_e {
  auto* ri = static_cast<RawHdInstance*>(instance);
  if (ri->os_readonly) {
    return HARDDISK_ERR_READ_ONLY;
  }

  if (block_num >= ri->total_blocks) {
    // Attempt to expand the file if writing beyond current end
    fseek(ri->file, 0, SEEK_END);
    auto current_blocks = static_cast<uint32_t>(ftell(ri->file) / kBlockSize);
    if (block_num > current_blocks) {
      // Fill gap with zeros
      std::array<uint8_t, kBlockSize> zero{};
      for (uint32_t i = current_blocks; i < block_num; ++i) {
        fwrite(zero.data(), 1, kBlockSize, ri->file);
      }
    }
    ri->total_blocks = block_num + 1;
  }

  if (fseek(ri->file, static_cast<long>(block_num) * kBlockSize, SEEK_SET) !=
      0) {
    return HARDDISK_ERR_IO;
  }

  if (fwrite(buffer, 1, kBlockSize, ri->file) != kBlockSize) {
    return HARDDISK_ERR_IO;
  }

  return HARDDISK_ERR_NONE;
}

static auto RawHd_GetTotalBlocks(void* instance) -> uint32_t {
  auto* ri = static_cast<RawHdInstance*>(instance);
  return ri->total_blocks;
}

extern "C" {
HarddiskFormatDriver_t g_raw_hd_driver = {LINAPPLE_HARDDISK_ABI_VERSION,
                                          HARDDISK_DRIVER_CAP_WRITE,
                                          "Raw",
                                          RawHd_Probe,
                                          RawHd_Open,
                                          RawHd_Close,
                                          RawHd_IsWriteProtected,
                                          RawHd_ReadBlock,
                                          RawHd_WriteBlock,
                                          RawHd_GetTotalBlocks};
}
