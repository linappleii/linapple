// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "apple2/peripherals/disk/formats/DiskContainer.h"

extern "C" const char* __asan_default_options() { return "detect_leaks=1"; }

namespace {

// Walking the registry rather than a list of names here is what keeps a
// format added tomorrow under the fuzzer without anyone remembering to add it.
auto probe_every_driver(const uint8_t* data, size_t size) -> void {
  const uint32_t count = disk_loader_driver_count();
  for (uint32_t i = 0; i < count; ++i) {
    const DiskFormatDriver_t* driver = disk_loader_driver_at(i);
    if (driver == nullptr || driver->probe == nullptr) {
      continue;
    }
    driver->probe(data, size, static_cast<uint32_t>(size), "fuzz.dsk");
  }
}

// A probe only reads a header. Laying the first quarter track out as cells is
// what makes the driver walk the whole file the fuzzer wrote.
auto read_first_track(const DiskFormatDriver_t* driver, void* instance)
    -> void {
  if (driver->read_track_bits == nullptr) {
    return;
  }
  static std::vector<uint8_t> bits(max_track_bits / 8);
  uint32_t bit_count = 0;
  uint8_t bit_timing = 0;
  driver->read_track_bits(instance, 0, bits.data(), max_track_bits, &bit_count,
                          &bit_timing);
}

auto open_every_driver(const char* path) -> void {
  const uint32_t count = disk_loader_driver_count();
  for (uint32_t i = 0; i < count; ++i) {
    const DiskFormatDriver_t* driver = disk_loader_driver_at(i);
    if (driver == nullptr || driver->open == nullptr ||
        driver->close == nullptr) {
      continue;
    }
    // Both answers to read_only, because it is the drive's verdict before the
    // medium is read and each one takes the driver down a different path.
    for (int read_only = 0; read_only < 2; ++read_only) {
      void* instance = nullptr;
      if (driver->open(path, 0, read_only != 0, &instance) != disk_err_none ||
          instance == nullptr) {
        continue;
      }
      if (driver->is_write_protected != nullptr) {
        driver->is_write_protected(instance);
      }
      read_first_track(driver, instance);
      driver->close(instance);
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size == 0) {
    return 0;
  }

  disk_container_detect_macbinary(data, size, static_cast<uint32_t>(size));
  probe_every_driver(data, size);

  char tmp_template[] = "/tmp/linapple_fuzz_disk_XXXXXX";
  int fd = mkstemp(tmp_template);
  if (fd >= 0) {
    ssize_t written = write(fd, data, size);
    (void)written;
    close(fd);

    const DiskFormatDriver_t* out_driver = nullptr;
    void* out_instance = nullptr;

    if (disk_loader_open(tmp_template, &out_driver, &out_instance) ==
            disk_err_none &&
        out_driver != nullptr && out_instance != nullptr) {
      read_first_track(out_driver, out_instance);
      if (out_driver->close != nullptr) {
        out_driver->close(out_instance);
      }
    }

    open_every_driver(tmp_template);

    unlink(tmp_template);
  }

  return 0;
}
// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
