#include "apple2/peripherals/disk/DiskLoader.h"

#include <strings.h>
#include <unistd.h>
#include <zip.h>
#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/Common.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

// For MacBinary
const int MACBINARY_HEADER_SIZE = 128;
const int MACBINARY_FILENAME_MAX = 120;
const int MACBINARY_MAGIC_OFFSET1 = 0x7A;
const int MACBINARY_MAGIC_OFFSET2 = 0x7B;
const uint8_t MACBINARY_MAGIC_VALUE = 0x81;

static std::vector<DiskFormatDriver_t*> g_drivers;

void DiskLoader_Init() { g_drivers.clear(); }

void DiskLoader_Shutdown() { g_drivers.clear(); }

void DiskLoader_Register(DiskFormatDriver_t* driver) {
  if (driver) {
    g_drivers.push_back(driver);
  }
}

static bool DiskUnGzip(const char* gzname, FILE* dskF) {
  if (!dskF) return false;
  gzFile gzF = gzopen(gzname, "rb");
  if (!gzF) return false;

  char buffer[8192];
  int len = 0;
  while ((len = gzread(gzF, buffer, sizeof(buffer))) > 0) {
    fwrite(buffer, 1, static_cast<size_t>(len), dskF);
  }
  gzclose(gzF);
  return true;
}

static bool DiskUnZip(const char* zipname, FILE* dskF) {
  if (!dskF) return false;
  int err = 0;
  zip* arch = zip_open(zipname, 0, &err);
  if (!arch) return false;

  zip_file* zf = zip_fopen_index(arch, 0, 0);
  if (!zf) {
    zip_close(arch);
    return false;
  }

  char buffer[8192];
  zip_int64_t len = 0;
  while ((len = zip_fread(zf, buffer, sizeof(buffer))) > 0) {
    fwrite(buffer, 1, static_cast<size_t>(len), dskF);
  }
  zip_fclose(zf);
  zip_close(arch);
  return true;
}

static bool IsMacBinary(const uint8_t* header, size_t size) {
  return (size >= MACBINARY_HEADER_SIZE && header[0] == 0 && header[1] > 0 &&
          header[1] <= MACBINARY_FILENAME_MAX && header[header[1] + 2] == 0 &&
          header[MACBINARY_MAGIC_OFFSET1] == MACBINARY_MAGIC_VALUE &&
          header[MACBINARY_MAGIC_OFFSET2] == MACBINARY_MAGIC_VALUE);
}

DiskError_e DiskLoader_Open(const char* filename, bool bCreateIfNecessary,
                            bool* pWriteProtected,
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
    DiskError_e err =
        best_driver->open(load_path, file_offset, &os_readonly, out_instance);

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
