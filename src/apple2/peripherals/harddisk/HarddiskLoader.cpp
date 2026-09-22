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

#include "apple2/media/image_container/ImageContainer.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "core/Log.h"
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
static int g_loader_ref_count = 0;

constexpr size_t load_path_len = 512;

// A ProDOS volume tops out at 65,535 blocks, just under 32 MiB, because its
// block count is a 16-bit field (ProDOS 8 Technical Reference), so an all-zero
// blank of the largest volume passes on size alone and only an archive that
// claims more has to satisfy the library's ratio as well.
constexpr size_t harddisk_decompression_threshold = 32 * 1024 * 1024;

auto container_error_to_harddisk_error(ImageContainerError_e error)
    -> HarddiskError_e {
  switch (error) {
    case image_container_ok:
      return harddisk_err_none;
    case image_container_not_found:
      return harddisk_err_not_found;
    case image_container_corrupt:
      return harddisk_err_invalid_format;
    // This card has no code for a refused size or a bad argument; both are
    // "could not open", which is what io means to its callers.
    case image_container_too_large:
    case image_container_invalid_argument:
    case image_container_io:
    default:
      return harddisk_err_io;
  }
}
}  // namespace

extern "C" const HarddiskFormatDriver_t g_two_img_driver;
extern "C" const HarddiskFormatDriver_t g_raw_hd_driver;

void harddisk_loader_init(void) {
  if (g_loader_ref_count++ == 0) {
    g_harddisk_drivers.clear();
    harddisk_loader_register(
        const_cast<HarddiskFormatDriver_t*>(&g_two_img_driver));
    harddisk_loader_register(
        const_cast<HarddiskFormatDriver_t*>(&g_raw_hd_driver));
  }
}

void harddisk_loader_shutdown(void) {
  if (g_loader_ref_count > 0) {
    --g_loader_ref_count;
    if (g_loader_ref_count == 0) {
      g_harddisk_drivers.clear();
    }
  }
}

auto harddisk_loader_register(HarddiskFormatDriver_t* driver_ptr) -> void {
  if (driver_ptr == nullptr) {
    Logger::error("HarddiskLoader: Attempted to register null driver");
    return;
  }
  if (driver_ptr->probe == nullptr || driver_ptr->open == nullptr ||
      driver_ptr->close == nullptr) {
    Logger::error(
        "HarddiskLoader: Driver '%s' missing required functions "
        "(probe/open/close)",
        driver_ptr->name != nullptr ? driver_ptr->name : "<unnamed>");
    return;
  }
  const bool has_write_cap =
      (driver_ptr->capabilities & harddisk_driver_cap_write) != 0;
  const bool has_write_fn = driver_ptr->write_block != nullptr;
  if (has_write_cap != has_write_fn) {
    Logger::error("HarddiskLoader: Driver '%s' write capability mismatch",
                  driver_ptr->name != nullptr ? driver_ptr->name : "<unnamed>");
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
      (void)unlink(path);
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

  char load_path[load_path_len] = {0};
  bool is_temporary = false;
  const ImageContainerError_e prepared =
      image_container_prepare_compressed_path(
          path, load_path, sizeof(load_path), harddisk_decompression_threshold,
          &is_temporary);
  if (prepared != image_container_ok) {
    return container_error_to_harddisk_error(prepared);
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
      image_container_detect_macbinary(header.data(), header_read, file_size);

  // The extension that names the format is the payload's, not the archive's
  // and never the extracted temporary's; a name the library cannot give leaves
  // the hint empty and the probes deciding by content alone.
  std::array<char, load_path_len> payload_name{};
  if (image_container_payload_name(path, payload_name.data(),
                                   payload_name.size()) != image_container_ok) {
    payload_name[0] = '\0';
  }
  const char* ext = strrchr(payload_name.data(), '.');
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

  for (const char* const* ext = image_container_supported_extensions();
       ext != nullptr && *ext != nullptr; ++ext) {
    if (std::find(exts.begin(), exts.end(), *ext) == exts.end()) {
      exts.emplace_back(*ext);
    }
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
