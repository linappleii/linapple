#include "apple2/peripherals/disk/DiskLoader.h"

#include <strings.h>
#include <unistd.h>
#include <zip.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/Common.h"
#include "core/Util_Text.h"

namespace {
static std::vector<DiskFormatDriver_t*> g_drivers;

constexpr size_t MACBINARY_HEADER_SIZE = 128;
constexpr size_t MACBINARY_MAGIC_OFFSET1 = 0;
constexpr size_t MACBINARY_MAGIC_OFFSET2 = 74;
constexpr uint8_t MACBINARY_MAGIC_VALUE = 0;

auto IsMacBinary(const uint8_t* header, size_t size) -> bool {
  if (size < 128) return false;
  return (header[MACBINARY_MAGIC_OFFSET1] == MACBINARY_MAGIC_VALUE &&
          header[MACBINARY_MAGIC_OFFSET2] == MACBINARY_MAGIC_VALUE);
}

auto DiskUnGzip(const char* filename, FILE* out) -> bool {
  gzFile f = gzopen(filename, "rb");
  if (!f) return false;

  std::array<uint8_t, 8192> buffer;
  int bytes_read;
  while ((bytes_read = gzread(f, buffer.data(), buffer.size())) > 0) {
    if (fwrite(buffer.data(), 1, static_cast<size_t>(bytes_read), out) !=
        static_cast<size_t>(bytes_read)) {
      gzclose(f);
      return false;
    }
  }
  gzclose(f);
  return true;
}

auto DiskUnZip(const char* filename, FILE* out) -> bool {
  int err = 0;
  zip* z = zip_open(filename, ZIP_RDONLY, &err);
  if (!z) return false;

  zip_int64_t num_entries = zip_get_num_entries(z, 0);
  if (num_entries <= 0) {
    zip_close(z);
    return false;
  }

  // Just take the first entry for now
  zip_file* f = zip_fopen_index(z, 0, 0);
  if (!f) {
    zip_close(z);
    return false;
  }

  std::array<uint8_t, 8192> buffer;
  zip_int64_t bytes_read;
  while ((bytes_read = zip_fread(f, buffer.data(), buffer.size())) > 0) {
    if (fwrite(buffer.data(), 1, static_cast<size_t>(bytes_read), out) !=
        static_cast<size_t>(bytes_read)) {
      zip_fclose(f);
      zip_close(z);
      return false;
    }
  }
  zip_fclose(f);
  zip_close(z);
  return true;
}
}  // namespace

void DiskLoader_Init() { g_drivers.clear(); }

void DiskLoader_Shutdown() { g_drivers.clear(); }

void DiskLoader_Register(DiskFormatDriver_t* driver) {
  if (driver) {
    g_drivers.push_back(driver);
  }
}

DiskError_e DiskLoader_Open(const char* filename, bool bCreateIfNecessary,
                            uint8_t enhanced_speed, bool* pWriteProtected,
                            DiskFormatDriver_t** out_driver,
                            void** out_instance) {
  if (!filename || !out_driver || !out_instance) return DISK_ERR_IO;

  const char* load_path = filename;
  char temp_path[PATH_MAX_LEN] = {0};
  bool is_temporary = false;

  size_t name_len = strlen(filename);
  if ((name_len > 3 && strcasecmp(filename + name_len - 3, ".gz") == 0) ||
      (name_len > 4 && strcasecmp(filename + name_len - 4, ".zip") == 0)) {
    snprintf(temp_path, sizeof(temp_path), "/tmp/linapple_XXXXXX");
    int fd = mkstemp(temp_path);
    if (fd != -1) {
      FILE* dskF = fdopen(fd, "wb");
      if (dskF) {
        bool success = false;
        if (strcasecmp(filename + name_len - 3, ".gz") == 0) {
          success = DiskUnGzip(filename, dskF);
        } else {
          success = DiskUnZip(filename, dskF);
        }
        fclose(dskF);

        if (success) {
          load_path = temp_path;
          is_temporary = true;
        } else {
          unlink(temp_path);
          return DISK_ERR_IO;
        }
      } else {
        close(fd);
        unlink(temp_path);
        return DISK_ERR_IO;
      }
    } else {
      return DISK_ERR_IO;
    }
  }

  FILE* f = fopen(load_path, "rb");
  if (!f) {
    if (bCreateIfNecessary && !is_temporary) {
      f = fopen(load_path, "wb");
      if (f) {
        fclose(f);
        f = fopen(load_path, "rb");
      }
    }
    if (!f) {
      if (is_temporary) unlink(temp_path);
      return DISK_ERR_FILE_NOT_FOUND;
    }
  }

  fseek(f, 0, SEEK_END);
  uint32_t file_size = static_cast<uint32_t>(ftell(f));
  fseek(f, 0, SEEK_SET);

  uint8_t header[4096];
  size_t header_read = fread(header, 1, sizeof(header), f);
  fclose(f);

  uint32_t file_offset = 0;
  if (IsMacBinary(header, header_read)) {
    file_offset = MACBINARY_HEADER_SIZE;
  }

  char ext_hint[16] = {0};
  const char* dot = strrchr(filename, '.');
  if (dot) {
    Util_SafeStrCpy(ext_hint, dot, sizeof(ext_hint));
    for (char* p = ext_hint; *p; ++p) {
      *p = static_cast<char>(tolower(static_cast<uint8_t>(*p)));
    }
  }

  DiskFormatDriver_t* best_driver = nullptr;
  DiskFormatDriver_t* possible_driver = nullptr;

  const uint8_t* probe_ptr = header + file_offset;
  size_t probe_size =
      (header_read > file_offset) ? (header_read - file_offset) : 0;

  for (auto* driver : g_drivers) {
    DiskProbe_e result =
        driver->probe(probe_ptr, probe_size, file_size - file_offset, ext_hint);
    if (result == DISK_PROBE_DEFINITE) {
      best_driver = driver;
      break;
    } else if (result == DISK_PROBE_POSSIBLE && !possible_driver) {
      possible_driver = driver;
    }
  }

  if (!best_driver) best_driver = possible_driver;

  if (best_driver) {
    bool os_readonly = false;
    DiskError_e err = best_driver->open(load_path, file_offset, enhanced_speed,
                                        &os_readonly, out_instance);

    // If it was a temporary file, we can unlink it now if the driver has its
    // own handle or if we just want to clean up. Most drivers in LinApple read
    // the whole thing anyway.
    if (is_temporary) {
      unlink(temp_path);
    }

    if (err == DISK_ERR_NONE) {
      *out_driver = best_driver;
      if (pWriteProtected != nullptr) {
        *pWriteProtected = os_readonly;
      }
      return DISK_ERR_NONE;
    }
    return err;
  }

  if (is_temporary) unlink(temp_path);
  return DISK_ERR_UNSUPPORTED_FORMAT;
}
