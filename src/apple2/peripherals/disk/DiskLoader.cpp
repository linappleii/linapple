// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-owning-memory)
#include "apple2/peripherals/disk/DiskLoader.h"

#include <strings.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/formats/DiskContainer.h"
#include "apple2/peripherals/disk/formats/DiskFormatRegistration.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

namespace {

// A driver may register itself during static initialisation, so the registry
// has to come into existence on first use rather than wait its turn in an
// initialisation order it cannot see.
auto registry() -> std::vector<const DiskFormatDriver_t*>& {
  static std::vector<const DiskFormatDriver_t*> drivers;
  return drivers;
}

// A driver compiled into the binary outlives any registry a test builds, so it
// is remembered separately and handed back by disk_loader_reset.
auto permanent_registry() -> std::vector<const DiskFormatDriver_t*>& {
  static std::vector<const DiskFormatDriver_t*> drivers;
  return drivers;
}

struct DriverRejection_t {
  std::string name;
  const char* reason;
};

// Registration happens during static initialisation, with no host to tell, so
// a refusal waits here for a caller that has somewhere to put it.
auto rejections() -> std::vector<DriverRejection_t>& {
  static std::vector<DriverRejection_t> refused;
  return refused;
}

auto driver_label(const DiskFormatDriver_t* driver) -> const char* {
  return (driver != nullptr && driver->name != nullptr) ? driver->name
                                                        : "<unnamed>";
}

auto refuse(const DiskFormatDriver_t* driver, const char* reason) -> bool {
  rejections().push_back(DriverRejection_t{driver_label(driver), reason});
  return false;
}

auto driver_is_usable(const DiskFormatDriver_t* driver) -> bool {
  if (driver == nullptr) {
    return refuse(driver, "null driver");
  }
  if (driver->abi_version != disk_format_abi_version) {
    return refuse(driver, "driver ABI version does not match the loader's");
  }
  if (driver->probe == nullptr || driver->open == nullptr ||
      driver->close == nullptr) {
    return refuse(driver, "probe, open or close missing");
  }
  if (driver->read_track_bits == nullptr ||
      driver->is_write_protected == nullptr) {
    return refuse(driver, "read_track_bits or is_write_protected missing");
  }
  const bool has_write_cap =
      (driver->capabilities & disk_driver_cap_write) != 0;
  const bool has_write_fn = driver->write_track_bits != nullptr;
  if (has_write_cap != has_write_fn) {
    return refuse(driver, "write capability disagrees with write_track_bits");
  }
  return true;
}

auto already_registered(const DiskFormatDriver_t* driver) -> bool {
  return std::find(registry().begin(), registry().end(), driver) !=
         registry().end();
}

// The name is what a user picks a format by and what disk_loader_create looks
// a driver up by, so two drivers answering to one name would leave one of
// them unreachable.
auto name_is_taken(const DiskFormatDriver_t* driver) -> bool {
  for (const auto* registered : registry()) {
    if (strcmp(driver_label(registered), driver_label(driver)) == 0) {
      return true;
    }
  }
  return false;
}

auto admit(const DiskFormatDriver_t* driver) -> bool {
  if (!driver_is_usable(driver) || already_registered(driver)) {
    return false;
  }
  if (name_is_taken(driver)) {
    return refuse(driver, "a driver with this name is already registered");
  }
  return true;
}

// A probe that finds no definite claim settles for the first driver that says
// "possible", so the order drivers sit in decides which one opens an ambiguous
// image. Link order is not an answer a user can reason about; alphabetical by
// name is.
auto insert_by_name(std::vector<const DiskFormatDriver_t*>& drivers,
                    const DiskFormatDriver_t* driver) -> void {
  const auto at = std::upper_bound(
      drivers.begin(), drivers.end(), driver,
      [](const DiskFormatDriver_t* lhs, const DiskFormatDriver_t* rhs) {
        return strcmp(driver_label(lhs), driver_label(rhs)) < 0;
      });
  drivers.insert(at, driver);
}

constexpr size_t path_max_len = disk_path_max;

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
                      uint32_t file_size, const char* payload_name)
    -> const DiskFormatDriver_t* {
  char ext_hint[extension_hint_size] = {0};
  const char* dot = strrchr(payload_name, '.');
  if (dot != nullptr) {
    util_safe_strcpy(ext_hint, dot, sizeof(ext_hint));
    for (char* p = ext_hint; *p != '\0'; ++p) {
      *p = static_cast<char>(tolower(static_cast<uint8_t>(*p)));
    }
  }

  const DiskFormatDriver_t* possible_driver = nullptr;
  for (auto* driver : registry()) {
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

auto has_container_extension(const char* path) -> bool {
  const char* base = strrchr(path, '/');
  base = (base != nullptr) ? base + 1 : path;
  const char* dot = strrchr(base, '.');
  if (dot == nullptr) {
    return false;
  }
  for (const char* const* ext = disk_container_supported_extensions();
       ext != nullptr && *ext != nullptr; ++ext) {
    if (strcasecmp(dot + 1, *ext) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

auto disk_loader_register(const DiskFormatDriver_t* driver) -> void {
  if (!admit(driver)) {
    return;
  }
  insert_by_name(registry(), driver);
}

auto disk_loader_register_permanent(const DiskFormatDriver_t* driver) -> void {
  if (!admit(driver)) {
    return;
  }
  insert_by_name(permanent_registry(), driver);
  insert_by_name(registry(), driver);
}

auto disk_loader_reset(void) -> void {
  registry() = permanent_registry();
  rejections().clear();
}

auto disk_loader_drain_rejections(DiskDriverRejectionFn_t sink, void* context)
    -> void {
  if (sink != nullptr) {
    for (const auto& rejection : rejections()) {
      sink(context, rejection.name.c_str(), rejection.reason);
    }
  }
  rejections().clear();
}

auto disk_loader_open(const char* image_path,
                      const DiskFormatDriver_t** out_driver,
                      void** out_instance) -> DiskError_e {
  if (out_driver != nullptr) {
    *out_driver = nullptr;
  }
  if (out_instance != nullptr) {
    *out_instance = nullptr;
  }
  if (image_path == nullptr || out_driver == nullptr ||
      out_instance == nullptr) {
    return disk_err_invalid_argument;
  }

  char load_path[path_max_len] = {0};
  bool is_temporary = false;
  if (!disk_container_prepare_compressed_path(
          image_path, load_path, sizeof(load_path),
          disk_container::floppy_decompression_threshold, &is_temporary)) {
    return disk_err_io;
  }

  TemporaryFileGuard temp_guard(is_temporary ? load_path : nullptr);

  FilePtr_t image_file(fopen(load_path, "rb"), fclose);
  if (image_file == nullptr) {
    return disk_err_file_not_found;
  }

  const int64_t raw_file_size = Path::file_size(image_file.get());
  if (raw_file_size < 0) {
    return disk_err_io;
  }
  // Every driver measures its image in 32 bits; a larger file is a format this
  // loader cannot describe, not a corrupt one.
  if (raw_file_size > static_cast<int64_t>(UINT32_MAX)) {
    return disk_err_unsupported;
  }
  const auto file_size = static_cast<uint32_t>(raw_file_size);

  std::vector<uint8_t> header(probe_header_size, 0);
  const size_t header_read =
      fread(header.data(), 1, header.size(), image_file.get());
  image_file.reset();

  const uint32_t file_offset =
      disk_container_detect_macbinary(header.data(), header_read, file_size);
  if (file_offset > file_size) {
    return disk_err_corrupt;
  }
  const uint8_t* probe_ptr = header.data() + file_offset;
  const size_t probe_size =
      (header_read > file_offset) ? (header_read - file_offset) : 0;

  char payload_name[path_max_len] = {0};
  disk_container_payload_name(image_path, payload_name, sizeof(payload_name));

  *out_driver = find_best_driver(probe_ptr, probe_size, file_size - file_offset,
                                 payload_name);

  if (*out_driver == nullptr) {
    return disk_err_unsupported_format;
  }

  // A decompressed temporary is unlinked the moment this call returns, so
  // anything written to it would be thrown away with it.
  return (*out_driver)
      ->open(load_path, file_offset, is_temporary, out_instance);
}

auto disk_loader_driver_count(void) -> uint32_t {
  return static_cast<uint32_t>(registry().size());
}

auto disk_loader_driver_at(uint32_t index) -> const DiskFormatDriver_t* {
  if (index >= registry().size()) {
    return nullptr;
  }
  return registry()[index];
}

auto disk_loader_create(const char* path, const char* driver_name)
    -> DiskError_e {
  if (path == nullptr || driver_name == nullptr || path[0] == '\0') {
    return disk_err_invalid_argument;
  }

  // A name ending in an archive extension would be created raw and then
  // unwrapped as an archive on its way back in, so the request is malformed
  // before any driver is asked.
  if (has_container_extension(path)) {
    return disk_err_invalid_argument;
  }

  // A driver creates exclusively, so an existing image is safe from
  // truncation either way; answering here keeps the clean-up below from
  // touching a file this call did not make.
  if (access(path, F_OK) == 0) {
    return disk_err_io;
  }

  const DiskFormatDriver_t* driver = nullptr;
  for (const auto* candidate : registry()) {
    if (candidate != nullptr && candidate->name != nullptr &&
        strcmp(candidate->name, driver_name) == 0) {
      driver = candidate;
      break;
    }
  }

  if (driver == nullptr) {
    return disk_err_unsupported_format;
  }
  if (driver->create == nullptr) {
    return disk_err_unsupported;
  }

  const DiskError_e error = driver->create(path);
  if (error != disk_err_none) {
    unlink(path);
  }
  return error;
}

auto disk_loader_get_supported_extensions(char* out_buffer, size_t buffer_size)
    -> void {
  if (out_buffer == nullptr || buffer_size == 0) {
    return;
  }
  out_buffer[0] = '\0';

  std::vector<std::string> exts;
  for (const auto* driver : registry()) {
    if (driver != nullptr && driver->supported_exts != nullptr) {
      for (const char* const* ext = driver->supported_exts; *ext != nullptr;
           ++ext) {
        if (std::find(exts.begin(), exts.end(), *ext) == exts.end()) {
          exts.emplace_back(*ext);
        }
      }
    }
  }

  const char* const* container_exts = disk_container_supported_extensions();
  for (; container_exts != nullptr && *container_exts != nullptr;
       ++container_exts) {
    if (std::find(exts.begin(), exts.end(), *container_exts) == exts.end()) {
      exts.emplace_back(*container_exts);
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

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-owning-memory)
