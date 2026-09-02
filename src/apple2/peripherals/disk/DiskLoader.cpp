// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-owning-memory)
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
#include <string>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/formats/DiskContainer.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

namespace {

static std::vector<DiskFormatDriver_t*> g_drivers;

// Disk Loading & Decompression parameters
constexpr size_t decompression_buffer_size = 8192;
// Why: 80 KB covers the DOS 3.3 Track 17 VTOC/catalog chain (73.5 KB) +
// optional MacBinary header (128 bytes) so sector image probing can inspect
// filesystem signatures definitively.
constexpr size_t probe_header_size = 80 * 1024;
constexpr size_t extension_hint_size = 16;

/**
 * @brief Ensures a temporary file is unlinked when it goes out of scope.
 */
struct TemporaryFileGuard {
  char path[path_max_len];
  explicit TemporaryFileGuard(const char* p) {
    if (p != nullptr) {
      util_safe_strcpy(path, p, path_max_len);
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

// Why: Scans registered drivers to find the one that definitively or possibly
// claims the disk image based on header content and extension.
auto find_best_driver(const uint8_t* header_ptr, size_t header_size,
                      uint32_t file_size, const char* image_path)
    -> DiskFormatDriver_t* {
  char ext_hint[extension_hint_size] = {0};
  const char* dot = strrchr(image_path, '.');
  if (dot != nullptr) {
    util_safe_strcpy(ext_hint, dot, sizeof(ext_hint));
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
  if (driver == nullptr) {
    return;
  }
  const bool has_write_cap =
      (driver->capabilities & disk_driver_cap_write) != 0;
  const bool has_write_fn = driver->write_track != nullptr;
  if (has_write_cap != has_write_fn) {
    return;
  }
  g_drivers.push_back(driver);
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
  if (!disk_container_prepare_compressed_path(
          image_path, load_path, sizeof(load_path),
          disk_container::floppy_decompression_threshold, &is_temporary)) {
    return disk_err_io;
  }

  std::unique_ptr<TemporaryFileGuard> temp_guard;
  if (is_temporary) {
    temp_guard.reset(new TemporaryFileGuard(load_path));
  }

  FilePtr_t image_file(fopen(load_path, "rb"), fclose);
  if (image_file == nullptr) {
    const char* base = strrchr(load_path, '/');
    const char* filename = (base != nullptr) ? (base + 1) : load_path;
    std::string found = Path::find_data_file(filename);
    if (!found.empty()) {
      image_file.reset(fopen(found.c_str(), "rb"));
    }
  }
  if (image_file == nullptr) {
    if (!create_if_necessary || is_temporary) {
      return disk_err_file_not_found;
    }
    FilePtr_t create_file(fopen(load_path, "wb"), fclose);
    if (create_file == nullptr) {
      return disk_err_file_not_found;
    }
    create_file.reset();
    image_file.reset(fopen(load_path, "rb"));
  }

  if (image_file == nullptr) {
    return disk_err_file_not_found;
  }

  if (fseek(image_file.get(), 0, SEEK_END) != 0) {
    return disk_err_io;
  }
  const long raw_file_size = ftell(image_file.get());
  if (raw_file_size < 0) {
    return disk_err_io;
  }
  const auto file_size = static_cast<uint32_t>(raw_file_size);
  if (fseek(image_file.get(), 0, SEEK_SET) != 0) {
    return disk_err_io;
  }

  std::vector<uint8_t> header(probe_header_size, 0);
  const size_t header_read =
      fread(header.data(), 1, header.size(), image_file.get());
  image_file.reset();

  const uint32_t file_offset =
      disk_container_detect_macbinary(header.data(), header_read, file_size);
  const uint8_t* probe_ptr = header.data() + file_offset;
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
    *out_is_read_only = os_readonly || is_temporary;
  }

  return err;
}

auto disk_loader_get_supported_extensions(char* out_buffer, size_t buffer_size)
    -> void {
  if (out_buffer == nullptr || buffer_size == 0) {
    return;
  }
  out_buffer[0] = '\0';

  std::vector<std::string> exts;
  for (const auto* driver : g_drivers) {
    if (driver != nullptr && driver->supported_exts != nullptr) {
      for (const char* const* ext = driver->supported_exts; *ext != nullptr;
           ++ext) {
        if (std::find(exts.begin(), exts.end(), *ext) == exts.end()) {
          exts.emplace_back(*ext);
        }
      }
    }
  }

  if (std::find(exts.begin(), exts.end(), "zip") == exts.end()) {
    exts.emplace_back("zip");
  }
  if (std::find(exts.begin(), exts.end(), "gz") == exts.end()) {
    exts.emplace_back("gz");
  }

  std::string result;
  for (size_t i = 0; i < exts.size(); ++i) {
    if (i > 0) {
      result += ";";
    }
    result += exts[i];
  }

  util_safe_strcpy(out_buffer, result.c_str(), buffer_size);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-owning-memory)
