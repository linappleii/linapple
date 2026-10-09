// SPDX-License-Identifier: GPL-2.0-only
// Justification: the probe window is walked by pointer, as the C99 driver
// entry points take it, and the drivers' extension lists are C arrays.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include "apple2/peripherals/harddisk/HarddiskLoader.h"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/media/image_container/ImageContainer.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskError.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/formats/HarddiskFormatRegistration.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

namespace {

// A driver may register itself during static initialisation, so the registry
// has to come into existence on first use rather than wait its turn in an
// initialisation order it cannot see.
auto registry() -> std::vector<const HarddiskFormatDriver_t*>& {
  static std::vector<const HarddiskFormatDriver_t*> drivers;
  return drivers;
}

// A driver compiled into the binary outlives any registry a test builds, so it
// is remembered separately and handed back by harddisk_loader_reset.
auto permanent_registry() -> std::vector<const HarddiskFormatDriver_t*>& {
  static std::vector<const HarddiskFormatDriver_t*> drivers;
  return drivers;
}

struct DriverNote_t {
  std::string subject;
  std::string text;
};

// Registration happens during static initialisation, with no host to tell, so
// a refusal waits here for a caller that has somewhere to put it.
auto notes() -> std::vector<DriverNote_t>& {
  static std::vector<DriverNote_t> pending;
  return pending;
}

auto driver_label(const HarddiskFormatDriver_t* driver) -> const char* {
  return (driver != nullptr && driver->name != nullptr) ? driver->name
                                                        : "<unnamed>";
}

auto refuse(const HarddiskFormatDriver_t* driver, const char* reason) -> bool {
  notes().push_back(DriverNote_t{driver_label(driver), reason});
  return false;
}

auto driver_is_usable(const HarddiskFormatDriver_t* driver) -> bool {
  if (driver == nullptr) {
    return refuse(driver, "null driver");
  }
  if (driver->abi_version != harddisk_format_abi_version) {
    return refuse(driver, "driver ABI version does not match the loader's");
  }
  if (driver->probe == nullptr || driver->open == nullptr ||
      driver->close == nullptr) {
    return refuse(driver, "probe, open or close missing");
  }
  if (driver->read_block == nullptr || driver->is_write_protected == nullptr ||
      driver->get_total_blocks == nullptr) {
    return refuse(driver,
                  "read_block, is_write_protected or get_total_blocks missing");
  }
  const bool has_write_cap =
      (driver->capabilities & harddisk_driver_cap_write) != 0;
  const bool has_write_fn = driver->write_block != nullptr;
  if (has_write_cap != has_write_fn) {
    return refuse(driver, "write capability disagrees with write_block");
  }
  return true;
}

auto already_registered(const HarddiskFormatDriver_t* driver) -> bool {
  return std::find(registry().begin(), registry().end(), driver) !=
         registry().end();
}

// The name is what settles an ambiguous image and what a note names, so two
// drivers answering to one name would leave one of them unreachable.
auto name_is_taken(const HarddiskFormatDriver_t* driver) -> bool {
  for (const auto* registered : registry()) {
    if (strcmp(driver_label(registered), driver_label(driver)) == 0) {
      return true;
    }
  }
  return false;
}

auto admit(const HarddiskFormatDriver_t* driver) -> bool {
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
auto insert_by_name(std::vector<const HarddiskFormatDriver_t*>& drivers,
                    const HarddiskFormatDriver_t* driver) -> void {
  const auto at = std::upper_bound(
      drivers.begin(), drivers.end(), driver,
      [](const HarddiskFormatDriver_t* lhs, const HarddiskFormatDriver_t* rhs) {
        return strcmp(driver_label(lhs), driver_label(rhs)) < 0;
      });
  drivers.insert(at, driver);
}

constexpr size_t path_max_len = harddisk_status_path_max;

// 80 KiB holds track 17, where the DOS 3.3 catalog ends at 73,728 bytes, even
// behind a 128-byte MacBinary wrapper. A ProDOS directory that chains past
// the window is not seen; the probe then answers possible, never wrong.
constexpr size_t probe_header_size = static_cast<size_t>(80) * 1024;
constexpr size_t extension_hint_size = 16;

// A ProDOS volume tops out at 65,535 blocks, just under 32 MiB, because its
// block count is a 16-bit field (ProDOS 8 Technical Reference), so an all-zero
// blank of the largest volume passes on size alone and only an archive that
// claims more has to satisfy the library's ratio as well.
constexpr size_t harddisk_decompression_threshold =
    static_cast<size_t>(32) * 1024 * 1024;

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

// The temporary an archive was unwrapped into is unlinked the moment the open
// returns, whatever happened; the driver's handle keeps the inode alive, and
// nothing written to it would outlive the session anyway.
struct TemporaryFile_t {
  char path[path_max_len] = {};
  explicit TemporaryFile_t(const char* p) {
    if (p != nullptr) {
      util_safe_strcpy(path, p, path_max_len);
    } else {
      path[0] = '\0';
    }
  }
  ~TemporaryFile_t() {
    if (path[0] != '\0') {
      unlink(path);
    }
  }
  TemporaryFile_t(const TemporaryFile_t&) = delete;
  auto operator=(const TemporaryFile_t&) -> TemporaryFile_t& = delete;
  TemporaryFile_t(TemporaryFile_t&&) = delete;
  auto operator=(TemporaryFile_t&&) -> TemporaryFile_t& = delete;
};

auto extension_hint(const char* payload_name, char* ext_hint, size_t size)
    -> void {
  ext_hint[0] = '\0';
  const char* dot = strrchr(payload_name, '.');
  if (dot == nullptr) {
    return;
  }
  util_safe_strcpy(ext_hint, dot, size);
  for (char* p = ext_hint; *p != '\0'; ++p) {
    *p = static_cast<char>(tolower(static_cast<uint8_t>(*p)));
  }
}

auto find_best_driver(const uint8_t* header_ptr, size_t header_size,
                      uint64_t file_size, const char* ext_hint)
    -> const HarddiskFormatDriver_t* {
  const HarddiskFormatDriver_t* possible_driver = nullptr;
  for (const auto* driver : registry()) {
    const HarddiskProbe_e result =
        driver->probe(header_ptr, header_size, file_size, ext_hint);
    if (result == harddisk_probe_definite) {
      return driver;
    }
    if (result == harddisk_probe_possible && possible_driver == nullptr) {
      possible_driver = driver;
    }
  }
  return possible_driver;
}

auto driver_lists_extension(const HarddiskFormatDriver_t* driver,
                            const char* ext_hint) -> bool {
  if (driver->supported_exts == nullptr || ext_hint[0] != '.') {
    return false;
  }
  for (const char* const* ext = driver->supported_exts; *ext != nullptr;
       ++ext) {
    if (strcmp(*ext, ext_hint + 1) == 0) {
      return true;
    }
  }
  return false;
}

// A file whose contents read coherently in one order under another order's
// name is served as its contents say, and the user is told the name that
// would say the same, since a later tool going by the name alone would read
// it wrongly.
auto note_name_overridden(const HarddiskFormatDriver_t* chosen,
                          const char* payload_name, const char* ext_hint)
    -> void {
  if (ext_hint[0] == '\0' || driver_lists_extension(chosen, ext_hint)) {
    return;
  }
  bool another_lists_it = false;
  for (const auto* driver : registry()) {
    if (driver != chosen && driver_lists_extension(driver, ext_hint)) {
      another_lists_it = true;
      break;
    }
  }
  if (!another_lists_it || chosen->supported_exts == nullptr ||
      chosen->supported_exts[0] == nullptr) {
    return;
  }
  const char* base = strrchr(payload_name, '/');
  base = (base != nullptr) ? base + 1 : payload_name;
  std::string stem(base);
  stem.resize(stem.size() - strlen(ext_hint));
  const std::string text = std::string("serves '") + base + "' as " +
                           driver_label(chosen) +
                           ", the order its contents read in; '" + stem + "." +
                           chosen->supported_exts[0] + "' would name it truly";
  harddisk_loader_note(driver_label(chosen), text.c_str());
}

}  // namespace

auto harddisk_loader_register(const HarddiskFormatDriver_t* driver) -> void {
  if (!admit(driver)) {
    return;
  }
  insert_by_name(registry(), driver);
}

auto harddisk_loader_register_permanent(const HarddiskFormatDriver_t* driver)
    -> void {
  if (!admit(driver)) {
    return;
  }
  insert_by_name(permanent_registry(), driver);
  insert_by_name(registry(), driver);
}

auto harddisk_loader_reset(void) -> void {
  registry() = permanent_registry();
  notes().clear();
}

auto harddisk_loader_drain_rejections(HarddiskDriverRejectionFn_t sink,
                                      void* context) -> void {
  if (sink != nullptr) {
    for (const auto& note : notes()) {
      sink(context, note.subject.c_str(), note.text.c_str());
    }
  }
  notes().clear();
}

auto harddisk_loader_note(const char* subject, const char* text) -> void {
  if (subject == nullptr || text == nullptr) {
    return;
  }
  notes().push_back(DriverNote_t{subject, text});
}

auto harddisk_loader_open(const char* image_path,
                          const HarddiskFormatDriver_t** out_driver,
                          void** out_instance) -> HarddiskError_e {
  if (out_driver != nullptr) {
    *out_driver = nullptr;
  }
  if (out_instance != nullptr) {
    *out_instance = nullptr;
  }
  if (image_path == nullptr || out_driver == nullptr ||
      out_instance == nullptr) {
    return harddisk_err_io;
  }

  char load_path[path_max_len] = {0};
  bool is_temporary = false;
  const ImageContainerError_e prepared =
      image_container_prepare_compressed_path(
          image_path, load_path, sizeof(load_path),
          harddisk_decompression_threshold, &is_temporary);
  if (prepared != image_container_ok) {
    return container_error_to_harddisk_error(prepared);
  }

  TemporaryFile_t temporary(is_temporary ? load_path : nullptr);

  FilePtr image_file{fopen(load_path, "rb"), fclose};
  if (image_file == nullptr) {
    return harddisk_err_not_found;
  }

  const int64_t raw_file_size = Path::file_size(image_file.get());
  if (raw_file_size < 0) {
    return harddisk_err_io;
  }
  const auto file_size = static_cast<uint64_t>(raw_file_size);

  std::vector<uint8_t> header(probe_header_size, 0);
  const size_t header_read =
      fread(header.data(), 1, header.size(), image_file.get());
  image_file.reset();

  // The wrapper detector measures in 32 bits; the size is clamped, since no
  // wrapped image comes near the limit.
  const uint32_t wrapper_size =
      static_cast<uint32_t>(std::min<uint64_t>(file_size, UINT32_MAX));
  const uint32_t file_offset = image_container_detect_macbinary(
      header.data(), header_read, wrapper_size);
  if (file_offset > file_size) {
    return harddisk_err_invalid_format;
  }
  const uint8_t* probe_ptr = header.data() + file_offset;
  const size_t probe_size =
      (header_read > file_offset) ? (header_read - file_offset) : 0;

  // The name only supplies the extension hint; a name the library cannot give
  // (one that does not fit) leaves the hint empty and the probes deciding by
  // content alone.
  char payload_name[path_max_len] = {0};
  if (image_container_payload_name(image_path, payload_name,
                                   sizeof(payload_name)) !=
      image_container_ok) {
    payload_name[0] = '\0';
  }
  char ext_hint[extension_hint_size] = {0};
  extension_hint(payload_name, ext_hint, sizeof(ext_hint));

  *out_driver = find_best_driver(probe_ptr, probe_size, file_size - file_offset,
                                 ext_hint);

  if (*out_driver == nullptr) {
    return harddisk_err_invalid_format;
  }

  // A decompressed temporary is unlinked the moment this call returns, so
  // anything written to it would be thrown away with it.
  const HarddiskError_e opened =
      (*out_driver)->open(load_path, file_offset, is_temporary, out_instance);
  if (opened == harddisk_err_none) {
    note_name_overridden(*out_driver, payload_name, ext_hint);
  }
  return opened;
}

auto harddisk_loader_driver_count(void) -> uint32_t {
  return static_cast<uint32_t>(registry().size());
}

auto harddisk_loader_driver_at(uint32_t index)
    -> const HarddiskFormatDriver_t* {
  if (index >= registry().size()) {
    return nullptr;
  }
  return registry()[index];
}

auto harddisk_loader_get_supported_extensions(char* out_buffer,
                                              size_t buffer_size) -> size_t {
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

  const char* const* container_exts = image_container_supported_extensions();
  for (; container_exts != nullptr && *container_exts != nullptr;
       ++container_exts) {
    if (std::find(exts.begin(), exts.end(), *container_exts) == exts.end()) {
      exts.emplace_back(*container_exts);
    }
  }

  std::string result;
  for (size_t i = 0; i < exts.size(); ++i) {
    if (i > 0) {
      result += ';';
    }
    result += exts[i];
  }

  if (out_buffer != nullptr && buffer_size > 0) {
    util_safe_strcpy(out_buffer, result.c_str(), buffer_size);
  }
  return result.size();
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay)
