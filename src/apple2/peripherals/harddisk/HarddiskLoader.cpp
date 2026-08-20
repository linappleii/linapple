// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/harddisk/HarddiskLoader.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/peripherals/disk/formats/DiskContainer.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,
//             modernize-make-unique, cppcoreguidelines-pro-type-const-cast,
//             bugprone-easily-swappable-parameters)
// Justification: Driver registration uses a global registry pattern for
// technical consistency with the floppy loading subsystem. const-cast is
// required to register the immutable global driver descriptor.
// easily-swappable-parameters is mandated by the loader ABI signatures.
// make-unique is suppressed to maintain C++11 compatibility.

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
  if (driver_ptr != nullptr) {
    g_harddisk_drivers.push_back(driver_ptr);
  }
}

auto harddisk_loader_open(const char* path, bool* out_os_readonly,
                          HarddiskFormatDriver_t** out_driver,
                          void** out_instance_handle) -> HarddiskError_e {
  if (path == nullptr || out_driver == nullptr ||
      out_instance_handle == nullptr) {
    return harddisk_err_io;
  }

  FilePtr_t file{fopen(path, "rb"), fclose};
  if (file == nullptr) {
    return harddisk_err_not_found;
  }

  fseek(file.get(), 0, SEEK_END);
  const uint32_t file_size = static_cast<uint32_t>(ftell(file.get()));
  fseek(file.get(), 0, SEEK_SET);

  constexpr size_t probe_header_size = 4096;
  std::array<uint8_t, probe_header_size> header{};
  const size_t header_read = fread(header.data(), 1, header.size(), file.get());
  file.reset();

  const uint32_t file_offset =
      disk_container_detect_macbinary(header.data(), header_read, file_size);

  const char* ext = strrchr(path, '.');
  constexpr size_t ext_hint_size = 16;
  std::array<char, ext_hint_size> ext_hint{};
  ext_hint.fill(0);

  if (ext != nullptr) {
    Util_SafeStrCpy(ext_hint.data(), ext, ext_hint.size());
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

  const HarddiskError_e err = best_driver->open(
      path, file_offset, out_os_readonly, out_instance_handle);

  if (err != harddisk_err_none) {
    return err;
  }

  *out_driver = best_driver;
  return harddisk_err_none;
}

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,
//           modernize-make-unique, cppcoreguidelines-pro-type-const-cast,
//           bugprone-easily-swappable-parameters)
