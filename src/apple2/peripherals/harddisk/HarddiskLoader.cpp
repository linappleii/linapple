// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/harddisk/HarddiskLoader.h"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apple2/peripherals/disk/formats/DiskContainer.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables, modernize-make-unique, cppcoreguidelines-pro-type-const-cast, bugprone-easily-swappable-parameters)
// Justification: Driver registration uses
// a global registry pattern for technical consistency with the floppy loading
// subsystem. const-cast is required to register the immutable global driver
// descriptor. easily-swappable-parameters is mandated by the loader ABI
// signatures. make-unique is suppressed to maintain C++11 compatibility.

namespace {
static std::vector<HarddiskFormatDriver_t*> g_harddisk_drivers;
}  // namespace

extern "C" const HarddiskFormatDriver_t g_two_img_driver;
extern "C" const HarddiskFormatDriver_t g_raw_hd_driver;

void harddisk_loader_init(void) {
  g_harddisk_drivers.clear();
  harddisk_loader_register(
      const_cast<HarddiskFormatDriver_t*>(&g_two_img_driver));
  harddisk_loader_register(
      const_cast<HarddiskFormatDriver_t*>(&g_raw_hd_driver));
}

void harddisk_loader_shutdown(void) { g_harddisk_drivers.clear(); }

auto harddisk_loader_register(HarddiskFormatDriver_t* driver_ptr) -> void {
  if (driver_ptr == nullptr) {
    return;
  }
  const bool has_write_cap =
      (driver_ptr->capabilities & harddisk_driver_cap_write) != 0;
  const bool has_write_fn = driver_ptr->write_block != nullptr;
  if (has_write_cap != has_write_fn) {
    return;
  }
  g_harddisk_drivers.push_back(driver_ptr);
}

struct TemporaryFileGuard {
  char path[512]{};
  explicit TemporaryFileGuard(const char* p) {
    if (p != nullptr) {
      util_safe_strcpy(path, p, sizeof(path));
    }
  }
  ~TemporaryFileGuard() {
    if (path[0] != '\0') {
      unlink(path);
    }
  }
  TemporaryFileGuard(const TemporaryFileGuard&) = delete;
  auto operator=(const TemporaryFileGuard&) -> TemporaryFileGuard& = delete;
};

auto harddisk_loader_open(const char* path, bool* out_os_readonly,
                          HarddiskFormatDriver_t** out_driver,
                          void** out_instance_handle) -> HarddiskError_e {
  if (path == nullptr || out_driver == nullptr ||
      out_instance_handle == nullptr) {
    return harddisk_err_io;
  }

  char load_path[512] = {0};
  bool is_temporary = false;
  if (!disk_container_prepare_compressed_path(
          path, load_path, sizeof(load_path),
          disk_container::harddisk_decompression_threshold, &is_temporary)) {
    return harddisk_err_io;
  }

  std::unique_ptr<TemporaryFileGuard> temp_guard;
  if (is_temporary) {
    temp_guard.reset(new TemporaryFileGuard(load_path));
  }

  FilePtr_t file{fopen(load_path, "rb"), fclose};
  if (file == nullptr) {
    return harddisk_err_not_found;
  }

  const int64_t raw_file_size = Path::file_size(file.get());
  if (raw_file_size < 0) {
    return harddisk_err_io;
  }
  const uint32_t file_size = static_cast<uint32_t>(raw_file_size);

  constexpr size_t probe_header_size = 4096;
  std::array<uint8_t, probe_header_size> header{};
  const size_t header_read = fread(header.data(), 1, header.size(), file.get());
  file.reset();

  const uint32_t file_offset =
      disk_container_detect_macbinary(header.data(), header_read, file_size);

  const char* ext = strrchr(load_path, '.');
  constexpr size_t ext_hint_size = 16;
  std::array<char, ext_hint_size> ext_hint{};
  ext_hint.fill(0);

  if (ext != nullptr) {
    util_safe_strcpy(ext_hint.data(), ext, ext_hint.size());
    for (char& c : ext_hint) {
      if (c == '\0') {
        break;
      }
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }

  HarddiskFormatDriver_t* best_driver = nullptr;
  HarddiskProbe_e best_probe = harddisk_probe_no;

  const uint8_t* probe_ptr = header.data() + file_offset;
  const size_t probe_size =
      (header_read > file_offset) ? (header_read - file_offset) : 0;

  for (auto* driver : g_harddisk_drivers) {
    const HarddiskProbe_e result = driver->probe(
        probe_ptr, probe_size, file_size - file_offset, ext_hint.data());
    if (result > best_probe) {
      best_probe = result;
      best_driver = driver;
    }
    if (best_probe == harddisk_probe_definite) {
      break;
    }
  }

  if (best_driver == nullptr || best_probe == harddisk_probe_no) {
    return harddisk_err_invalid_format;
  }

  bool os_readonly = false;
  const HarddiskError_e err = best_driver->open(
      load_path, file_offset, &os_readonly, out_instance_handle);

  if (err != harddisk_err_none) {
    return err;
  }

  if (out_os_readonly != nullptr) {
    *out_os_readonly = os_readonly || is_temporary;
  }

  *out_driver = best_driver;
  return harddisk_err_none;
}

void harddisk_loader_get_supported_extensions(char* out_buffer,
                                              size_t buffer_size) {
  if (out_buffer == nullptr || buffer_size == 0) {
    return;
  }
  out_buffer[0] = '\0';

  std::vector<std::string> exts;
  for (const auto* driver : g_harddisk_drivers) {
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

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables, modernize-make-unique, cppcoreguidelines-pro-type-const-cast, bugprone-easily-swappable-parameters)
