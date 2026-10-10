// SPDX-License-Identifier: GPL-2.0-only
// Justification: a libFuzzer entry point has a fixed C signature, and the
// harness fills C arrays by pointer for the C99 driver entry points.
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

#include "apple2/media/image_container/ImageContainer.h"
#include "apple2/peripherals/harddisk/HarddiskError.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/HarddiskLoader.h"

extern "C" const char* __asan_default_options() { return "detect_leaks=1"; }

namespace {

constexpr size_t path_len = 512;
constexpr size_t block_size = 512;

// The loader picks its container and its driver by suffix, and the archive
// bytes cannot choose one (gzip's first byte is fixed), so a selector byte
// does. It is the input's last byte, so a fixture handed over as a seed
// reaches the probes exactly as the loader would see it.
auto suffix_for(uint8_t selector) -> const char* {
  switch (selector & 7) {
    case 0:
      return ".hdv";
    case 1:
      return ".po";
    case 2:
      return ".dsk";
    case 3:
      return ".2mg";
    case 4:
      return ".woz";
    case 5:
      return ".gz";
    case 6:
      return ".zip";
    default:
      return ".img";
  }
}

auto temp_dir() -> std::string {
  const char* env = getenv("TMPDIR");
  return (env != nullptr && env[0] != '\0') ? env : "/tmp";
}

// Walking the registry rather than a list of names here is what keeps a
// format added tomorrow under the fuzzer without anyone remembering to add it.
auto probe_every_driver(const uint8_t* data, size_t size) -> void {
  const uint32_t count = harddisk_loader_driver_count();
  for (uint32_t i = 0; i < count; ++i) {
    const HarddiskFormatDriver* driver = harddisk_loader_driver_at(i);
    if (driver == nullptr || driver->probe == nullptr) {
      continue;
    }
    driver->probe(data, size, size, "fuzz.hdv");
  }
}

// A probe only reads a header; the blocks are what make the driver walk the
// file the fuzzer wrote: the first, the last, one past the end, and one
// written back when the medium allows it.
auto exercise(const HarddiskFormatDriver* driver, void* instance) -> void {
  static std::array<uint8_t, block_size> buffer{};
  const uint32_t total = driver->get_total_blocks(instance);
  driver->read_block(instance, 0, buffer.data());
  if (total > 0) {
    driver->read_block(instance, total - 1, buffer.data());
  }
  driver->read_block(instance, total, buffer.data());
  if (!driver->is_write_protected(instance) && driver->write_block != nullptr &&
      total > 0) {
    driver->write_block(instance, total - 1, buffer.data());
  }
}

auto open_every_driver(const char* path) -> void {
  const uint32_t count = harddisk_loader_driver_count();
  for (uint32_t i = 0; i < count; ++i) {
    const HarddiskFormatDriver* driver = harddisk_loader_driver_at(i);
    if (driver == nullptr || driver->open == nullptr ||
        driver->close == nullptr) {
      continue;
    }
    // Both answers to read_only, because it is the drive's verdict before the
    // medium is read and each one takes the driver down a different path.
    for (int read_only = 0; read_only < 2; ++read_only) {
      void* instance = nullptr;
      if (driver->open(path, 0, read_only != 0, &instance) !=
              harddisk_err_none ||
          instance == nullptr) {
        continue;
      }
      exercise(driver, instance);
      driver->close(instance);
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 1) {
    return 0;
  }
  const char* suffix = suffix_for(data[size - 1]);
  const uint8_t* payload = data;
  const size_t payload_size = size - 1;

  image_container_detect_macbinary(payload, payload_size,
                                   static_cast<uint32_t>(payload_size));
  probe_every_driver(payload, payload_size);

  const std::string path_template =
      temp_dir() + "/linapple_fuzz_harddisk_XXXXXX" + suffix;
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

  const HarddiskFormatDriver* out_driver = nullptr;
  void* out_instance = nullptr;
  if (harddisk_loader_open(path.data(), &out_driver, &out_instance) ==
          harddisk_err_none &&
      out_driver != nullptr && out_instance != nullptr) {
    exercise(out_driver, out_instance);
    out_driver->close(out_instance);
  }
  harddisk_loader_drain_rejections(nullptr, nullptr);

  open_every_driver(path.data());
  harddisk_loader_drain_rejections(nullptr, nullptr);

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
