/*
 * HarddiskLoader.cpp - Centralised harddisk image loading and format detection
 */

#include "apple2/peripherals/harddisk/HarddiskLoader.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <array>
#include <vector>

#include "core/Util_Path.h"
#include "core/Util_Text.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static std::vector<HarddiskFormatDriver_t*> g_harddisk_drivers;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

extern "C" HarddiskFormatDriver_t g_raw_hd_driver;

void HarddiskLoader_Init(void) {
  g_harddisk_drivers.clear();
  HarddiskLoader_Register(&g_raw_hd_driver);
}

void HarddiskLoader_Shutdown(void) { g_harddisk_drivers.clear(); }

void HarddiskLoader_Register(HarddiskFormatDriver_t* driver) {
  if (driver) {
    g_harddisk_drivers.push_back(driver);
  }
}

auto HarddiskLoader_Open(const char* filename, bool* out_os_readonly,
                         HarddiskFormatDriver_t** out_driver,
                         void** out_instance) -> HarddiskError_e {
  if (!filename || !out_driver || !out_instance) {
    return HARDDISK_ERR_IO;
  }

  FILE* f = fopen(filename, "rb"); // NOLINT(cppcoreguidelines-owning-memory)
  if (!f) {
    return HARDDISK_ERR_NOT_FOUND;
  }

  fseek(f, 0, SEEK_END);
  auto file_size = static_cast<uint32_t>(ftell(f));
  fseek(f, 0, SEEK_SET);

  constexpr size_t PROBE_HEADER_SIZE = 4096;
  std::array<uint8_t, PROBE_HEADER_SIZE> header{};
  size_t header_size = fread(header.data(), 1, header.size(), f);
  fclose(f); // NOLINT(cppcoreguidelines-owning-memory)

  const char* ext = strrchr(filename, '.');
  constexpr size_t EXT_HINT_SIZE = 16;
  std::array<char, EXT_HINT_SIZE> ext_hint{};
  ext_hint.fill(0);

  if (ext) {
    Util_SafeStrCpy(ext_hint.data(), ext, ext_hint.size());
    for (char& c : ext_hint) {
      if (c == '\0') {
        break;
      }
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }

  HarddiskFormatDriver_t* best_driver = nullptr;
  HarddiskProbe_e best_probe = HARDDISK_PROBE_NO;

  for (auto* driver : g_harddisk_drivers) {
    HarddiskProbe_e result =
        driver->probe(header.data(), header_size, file_size, ext_hint.data());
    if (result > best_probe) {
      best_probe = result;
      best_driver = driver;
    }
    if (best_probe == HARDDISK_PROBE_DEFINITE) {
      break;
    }
  }

  if (best_driver && best_probe != HARDDISK_PROBE_NO) {
    HarddiskError_e err =
        best_driver->open(filename, out_os_readonly, out_instance);
    if (err == HARDDISK_ERR_NONE) {
      *out_driver = best_driver;
      return HARDDISK_ERR_NONE;
    }
    return err;
  }

  return HARDDISK_ERR_INVALID_FORMAT;
}
