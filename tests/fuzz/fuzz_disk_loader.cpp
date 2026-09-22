// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/media/image_container/ImageContainer.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"

extern "C" const char* __asan_default_options() { return "detect_leaks=1"; }

namespace {

constexpr size_t path_len = 512;

// The loader picks its container and its driver by suffix, and the archive
// bytes cannot choose one (gzip's first byte is fixed), so the first input
// byte does. Without it no well-formed archive would ever reach extraction.
auto suffix_for(uint8_t selector) -> const char* {
  switch (selector & 3) {
    case 0:
      return ".dsk";
    case 1:
      return ".gz";
    case 2:
      return ".zip";
    default:
      return ".woz";
  }
}

auto temp_dir() -> std::string {
  const char* env = getenv("TMPDIR");
  return (env != nullptr && env[0] != '\0') ? env : "/tmp";
}

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

// A probe only reads a header. Laying tracks out as cells is what makes the
// driver walk the file the fuzzer wrote. The first four quarter tracks cover
// every phase of the head; from there one read per whole track reaches the
// end of the image at a fraction of the cost of all 160.
auto read_tracks(const DiskFormatDriver_t* driver, void* instance) -> void {
  if (driver->read_track_bits == nullptr) {
    return;
  }
  static std::vector<uint8_t> bits(max_track_bits / 8);
  for (uint32_t quarter_track = 0; quarter_track < 160;
       quarter_track += (quarter_track < 4) ? 1 : 4) {
    uint32_t bit_count = 0;
    uint8_t bit_timing = 0;
    driver->read_track_bits(instance, quarter_track, bits.data(),
                            max_track_bits, &bit_count, &bit_timing);
  }
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
      read_tracks(driver, instance);
      driver->close(instance);
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 1) {
    return 0;
  }
  const char* suffix = suffix_for(data[0]);
  const uint8_t* payload = data + 1;
  const size_t payload_size = size - 1;

  image_container_detect_macbinary(payload, payload_size,
                                   static_cast<uint32_t>(payload_size));
  probe_every_driver(payload, payload_size);

  const std::string path_template =
      temp_dir() + "/linapple_fuzz_disk_XXXXXX" + suffix;
  std::array<char, path_len> path{};
  if (path_template.size() >= path.size()) {
    return 0;
  }
  memcpy(path.data(), path_template.c_str(), path_template.size() + 1);

  const int fd = mkstemps(path.data(), static_cast<int>(strlen(suffix)));
  if (fd < 0) {
    return 0;
  }
  const ssize_t written = write(fd, payload, payload_size);
  close(fd);
  if (written != static_cast<ssize_t>(payload_size)) {
    unlink(path.data());
    return 0;
  }

  const DiskFormatDriver_t* out_driver = nullptr;
  void* out_instance = nullptr;
  if (disk_loader_open(path.data(), &out_driver, &out_instance) ==
          disk_err_none &&
      out_driver != nullptr && out_instance != nullptr) {
    read_tracks(out_driver, out_instance);
    if (out_driver->close != nullptr) {
      out_driver->close(out_instance);
    }
  }

  open_every_driver(path.data());

  unlink(path.data());
  return 0;
}
// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)
