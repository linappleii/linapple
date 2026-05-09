/*
 * RawHdDriver.cpp - LinApple Raw Harddisk Format Driver
 */

#include <sys/stat.h>

#include <cstdio>
#include <cstring>

#include "apple2/HarddiskFormatDriver.h"

// NOLINTBEGIN(cppcoreguidelines-owning-memory,
// modernize-use-trailing-return-type)

namespace {
struct RawHdInstance {
  FILE* file;
  uint32_t total_blocks;
  bool os_readonly;

  RawHdInstance() : file(nullptr), total_blocks(0), os_readonly(false) {}
  ~RawHdInstance() {
    if (file) {
      fclose(file);
    }
  }

  // Disable copying
  RawHdInstance(const RawHdInstance&) = delete;
  RawHdInstance& operator=(const RawHdInstance&) = delete;
};
}  // namespace

static HarddiskProbe_e RawHd_Probe(const uint8_t* header, size_t header_size,
                                   uint32_t file_size, const char* ext_hint) {
  (void)header;
  (void)header_size;

  // Raw images should be a multiple of 512 bytes
  if (file_size > 0 && (file_size % 512) == 0) {
    if (strcmp(ext_hint, ".hdv") == 0 || strcmp(ext_hint, ".po") == 0 ||
        strcmp(ext_hint, ".2mg") != 0) {
      // .2mg is handled by another driver (once implemented), so we avoid
      // claiming it definitely
      return HARDDISK_PROBE_POSSIBLE;
    }
  }

  return HARDDISK_PROBE_NO;
}

static HarddiskError_e RawHd_Open(const char* path, bool* out_os_readonly,
                                  void** out_instance) {
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
  instance->total_blocks = static_cast<uint32_t>(ftell(instance->file) / 512);
  fseek(instance->file, 0, SEEK_SET);

  if (out_os_readonly) {
    *out_os_readonly = instance->os_readonly;
  }
  *out_instance = reinterpret_cast<void*>(instance);
  return HARDDISK_ERR_NONE;
}

static void RawHd_Close(void* instance) {
  delete reinterpret_cast<RawHdInstance*>(instance);
}

static bool RawHd_IsWriteProtected(void* instance) {
  auto* ri = reinterpret_cast<RawHdInstance*>(instance);
  return ri->os_readonly;
}

static HarddiskError_e RawHd_ReadBlock(void* instance, uint32_t block_num,
                                       uint8_t* buffer) {
  auto* ri = reinterpret_cast<RawHdInstance*>(instance);
  if (block_num >= ri->total_blocks) {
    return HARDDISK_ERR_IO;
  }

  if (fseek(ri->file, static_cast<long>(block_num * 512), SEEK_SET) != 0) {
    return HARDDISK_ERR_IO;
  }

  if (fread(buffer, 1, 512, ri->file) != 512) {
    return HARDDISK_ERR_IO;
  }

  return HARDDISK_ERR_NONE;
}

static HarddiskError_e RawHd_WriteBlock(void* instance, uint32_t block_num,
                                        const uint8_t* buffer) {
  auto* ri = reinterpret_cast<RawHdInstance*>(instance);
  if (ri->os_readonly) {
    return HARDDISK_ERR_READ_ONLY;
  }

  if (block_num >= ri->total_blocks) {
    // Attempt to expand the file if writing beyond current end
    fseek(ri->file, 0, SEEK_END);
    uint32_t current_blocks = static_cast<uint32_t>(ftell(ri->file) / 512);
    if (block_num > current_blocks) {
      // Fill gap with zeros
      uint8_t zero[512] = {0};
      for (uint32_t i = current_blocks; i < block_num; ++i) {
        fwrite(zero, 1, 512, ri->file);
      }
    }
    ri->total_blocks = block_num + 1;
  }

  if (fseek(ri->file, static_cast<long>(block_num * 512), SEEK_SET) != 0) {
    return HARDDISK_ERR_IO;
  }

  if (fwrite(buffer, 1, 512, ri->file) != 512) {
    return HARDDISK_ERR_IO;
  }

  return HARDDISK_ERR_NONE;
}

static uint32_t RawHd_GetTotalBlocks(void* instance) {
  auto* ri = reinterpret_cast<RawHdInstance*>(instance);
  return ri->total_blocks;
}

extern "C" HarddiskFormatDriver_t g_raw_hd_driver = {
    LINAPPLE_HARDDISK_ABI_VERSION,
    HARDDISK_DRIVER_CAP_WRITE,
    "Raw",
    RawHd_Probe,
    RawHd_Open,
    RawHd_Close,
    RawHd_IsWriteProtected,
    RawHd_ReadBlock,
    RawHd_WriteBlock,
    RawHd_GetTotalBlocks};

// NOLINTEND(cppcoreguidelines-owning-memory,
// modernize-use-trailing-return-type)
