// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//             cppcoreguidelines-owning-memory)
#include "apple2/peripherals/disk/DiskLoader.h"

#include <strings.h>
#include <unistd.h>
#include <zip.h>
#include <zlib.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/formats/DiskContainer.h"
#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

namespace {

static std::vector<DiskFormatDriver_t*> g_drivers;

// Disk Loading & Decompression parameters
constexpr size_t decompression_buffer_size = 8192;
constexpr size_t probe_header_size = 4096;
constexpr size_t extension_hint_size = 16;

/**
 * @brief Ensures a temporary file is unlinked when it goes out of scope.
 */
struct TemporaryFileGuard {
  char path[path_max_len];
  explicit TemporaryFileGuard(const char* p) {
    if (p != nullptr) {
      Util_SafeStrCpy(path, p, path_max_len);
    } else {
      path[0] = '\0';
    }
  }
  ~TemporaryFileGuard() {
    if (path[0] != '\0') {
      unlink(path);
    }
  }
  // Not copyable or movable
  TemporaryFileGuard(const TemporaryFileGuard&) = delete;
  auto operator=(const TemporaryFileGuard&) -> TemporaryFileGuard& = delete;
};

// Why: Some legacy Apple II disk images are wrapped in a MacBinary header
// (128 bytes) by Macintosh-based transfer utilities. This identifies them
// so we can skip the wrapper and find the real disk image data.
auto decompress_gzip(const char* compressed_path, FILE* output_file) -> bool {
  gzFile compressed_file = gzopen(compressed_path, "rb");
  if (compressed_file == nullptr) {
    return false;
  }

  auto closer = [](void* f) {
    if (f != nullptr) {
      gzclose(static_cast<gzFile>(f));
    }
  };
  std::unique_ptr<void, void (*)(void*)> guard(compressed_file, closer);

  std::array<uint8_t, decompression_buffer_size> buffer{};
  int bytes_read = 0;
  while ((bytes_read = gzread(compressed_file, buffer.data(),
                              static_cast<unsigned int>(buffer.size()))) > 0) {
    if (fwrite(buffer.data(), 1, static_cast<size_t>(bytes_read),
               output_file) != static_cast<size_t>(bytes_read)) {
      return false;
    }
  }

  return bytes_read == 0;
}

auto decompress_zip(const char* compressed_path, FILE* output_file) -> bool {
  int err = 0;
  zip* zip_archive = zip_open(compressed_path, ZIP_RDONLY, &err);
  if (zip_archive == nullptr) {
    return false;
  }
  std::unique_ptr<zip, int (*)(zip*)> zip_closer(zip_archive, zip_close);

  if (zip_get_num_entries(zip_archive, 0) <= 0) {
    return false;
  }

  zip_file* file_in_zip = zip_fopen_index(zip_archive, 0, 0);
  if (file_in_zip == nullptr) {
    return false;
  }
  std::unique_ptr<zip_file, int (*)(zip_file*)> file_closer(file_in_zip,
                                                            zip_fclose);

  std::array<uint8_t, decompression_buffer_size> buffer{};
  zip_int64_t bytes_read = 0;
  while ((bytes_read = zip_fread(file_in_zip, buffer.data(), buffer.size())) >
         0) {
    if (fwrite(buffer.data(), 1, static_cast<size_t>(bytes_read),
               output_file) != static_cast<size_t>(bytes_read)) {
      return false;
    }
  }

  return bytes_read == 0;
}

// Why: Extracts compressed images to a temporary path. Returns true if a
// temporary file was created.
auto prepare_compressed_path(const char* image_path, char* out_load_path,
                             bool* out_is_temporary) -> bool {
  const size_t name_len = strlen(image_path);
  constexpr size_t GZ_EXT_LEN = 3;
  constexpr size_t ZIP_EXT_LEN = 4;

  const bool is_gz =
      (name_len > GZ_EXT_LEN &&
       strcasecmp(image_path + name_len - GZ_EXT_LEN, ".gz") == 0);
  const bool is_zip =
      (name_len > ZIP_EXT_LEN &&
       strcasecmp(image_path + name_len - ZIP_EXT_LEN, ".zip") == 0);

  if (!is_gz && !is_zip) {
    Util_SafeStrCpy(out_load_path, image_path, path_max_len);
    *out_is_temporary = false;
    return true;
  }

  Util_SafeStrCpy(out_load_path, "/tmp/linapple_XXXXXX", path_max_len);
  int fd = mkstemp(out_load_path);
  if (fd == -1) {
    return false;
  }

  FilePtr temp_stream(fdopen(fd, "wb"), fclose);
  if (temp_stream == nullptr) {
    close(fd);
    unlink(out_load_path);
    return false;
  }

  bool success = is_gz ? decompress_gzip(image_path, temp_stream.get())
                       : decompress_zip(image_path, temp_stream.get());

  if (!success) {
    unlink(out_load_path);
    return false;
  }

  *out_is_temporary = true;
  return true;
}

// Why: Scans registered drivers to find the one that definitively or possibly
// claims the disk image based on header content and extension.
auto find_best_driver(const uint8_t* header_ptr, size_t header_size,
                      uint32_t file_size, const char* image_path)
    -> DiskFormatDriver_t* {
  char ext_hint[extension_hint_size] = {0};
  const char* dot = strrchr(image_path, '.');
  if (dot != nullptr) {
    Util_SafeStrCpy(ext_hint, dot, sizeof(ext_hint));
    for (char* p = ext_hint; *p != '\0'; ++p) {
      *p = static_cast<char>(tolower(static_cast<uint8_t>(*p)));
    }
  }

  DiskFormatDriver_t* possible_driver = nullptr;
  for (auto* driver : g_drivers) {
    const DiskProbe_e result =
        driver->probe(header_ptr, header_size, file_size, ext_hint);
    if (result == disk_probe_definite) {
      return driver;
    }
    if (result == disk_probe_possible && possible_driver == nullptr) {
      possible_driver = driver;
    }
  }
  return possible_driver;
}

}  // namespace

auto disk_loader_init() -> void { g_drivers.clear(); }

auto disk_loader_shutdown() -> void { g_drivers.clear(); }

auto disk_loader_register(DiskFormatDriver_t* driver) -> void {
  if (driver != nullptr) {
    g_drivers.push_back(driver);
  }
}

auto disk_loader_open(const char* image_path, bool create_if_necessary,
                      uint8_t enhanced_speed, bool* out_is_read_only,
                      DiskFormatDriver_t** out_driver, void** out_instance)
    -> DiskError_e {
  if (image_path == nullptr || out_driver == nullptr ||
      out_instance == nullptr) {
    return disk_err_io;
  }

  char load_path[path_max_len] = {0};
  bool is_temporary = false;
  if (!prepare_compressed_path(image_path, load_path, &is_temporary)) {
    return disk_err_io;
  }

  std::unique_ptr<TemporaryFileGuard> temp_guard;
  if (is_temporary) {
    temp_guard.reset(new TemporaryFileGuard(load_path));
  }

  FilePtr image_file(fopen(load_path, "rb"), fclose);
  if (image_file == nullptr) {
    if (!create_if_necessary || is_temporary) {
      return disk_err_file_not_found;
    }
    FilePtr create_file(fopen(load_path, "wb"), fclose);
    if (create_file == nullptr) {
      return disk_err_file_not_found;
    }
    create_file.reset();
    image_file.reset(fopen(load_path, "rb"));
  }

  if (image_file == nullptr) {
    return disk_err_file_not_found;
  }

  fseek(image_file.get(), 0, SEEK_END);
  const auto file_size = static_cast<uint32_t>(ftell(image_file.get()));
  fseek(image_file.get(), 0, SEEK_SET);

  uint8_t header[probe_header_size];
  const size_t header_read = fread(header, 1, sizeof(header), image_file.get());
  image_file.reset();

  const uint32_t file_offset =
      disk_container_detect_macbinary(header, header_read, file_size);
  const uint8_t* probe_ptr = header + file_offset;
  const size_t probe_size =
      (header_read > file_offset) ? (header_read - file_offset) : 0;

  *out_driver = find_best_driver(probe_ptr, probe_size, file_size - file_offset,
                                 image_path);

  if (*out_driver == nullptr) {
    return disk_err_unsupported_format;
  }

  bool os_readonly = false;
  const DiskError_e err = (*out_driver)
                              ->open(load_path, file_offset, enhanced_speed,
                                     &os_readonly, out_instance);

  if (err == disk_err_none && out_is_read_only != nullptr) {
    *out_is_read_only = os_readonly;
  }

  return err;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//           cppcoreguidelines-owning-memory)
