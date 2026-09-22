// SPDX-License-Identifier: GPL-2.0-only
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "doctest.h"

namespace {

auto probe_no(const uint8_t*, size_t, uint32_t, const char*) -> DiskProbe_e {
  return disk_probe_no;
}

auto probe_possible(const uint8_t*, size_t, uint32_t, const char*)
    -> DiskProbe_e {
  return disk_probe_possible;
}

auto fake_open(const char*, uint32_t, bool, void** out_instance)
    -> DiskError_e {
  *out_instance = const_cast<char*>("fake");
  return disk_err_none;
}

auto fake_close(void*) -> void {}

auto fake_is_write_protected(void*) -> bool { return true; }

auto fake_read_track_bits(void*, uint32_t, uint8_t*, uint32_t,
                          uint32_t* out_bit_count, uint8_t* out_bit_timing)
    -> DiskError_e {
  *out_bit_count = 0;
  *out_bit_timing = disk_default_bit_timing;
  return disk_err_none;
}

auto make_fake(const char* name,
               DiskProbe_e (*probe)(const uint8_t*, size_t, uint32_t,
                                    const char*)) -> DiskFormatDriver_t {
  DiskFormatDriver_t driver{};
  driver.abi_version = disk_format_abi_version;
  driver.name = name;
  driver.probe = probe;
  driver.open = fake_open;
  driver.close = fake_close;
  driver.is_write_protected = fake_is_write_protected;
  driver.read_track_bits = fake_read_track_bits;
  return driver;
}

struct ScopedRandomFile_t {
  char path[64] = "/tmp/linapple_registry_XXXXXX";

  ScopedRandomFile_t() {
    const int fd = mkstemp(path);
    if (fd >= 0) {
      // An odd length no real driver recognises, so only the fakes answer.
      const std::vector<uint8_t> junk(777, 0x5A);
      const ssize_t written = write(fd, junk.data(), junk.size());
      static_cast<void>(written);
      close(fd);
    }
  }
  ~ScopedRandomFile_t() { unlink(path); }

  ScopedRandomFile_t(const ScopedRandomFile_t&) = delete;
  auto operator=(const ScopedRandomFile_t&) -> ScopedRandomFile_t& = delete;
};

auto count_rejections(std::vector<std::string>* names) -> void {
  disk_loader_drain_rejections(
      [](void* context, const char* driver_name, const char*) {
        static_cast<std::vector<std::string>*>(context)->emplace_back(
            driver_name);
      },
      names);
}

}  // namespace

TEST_CASE("DiskRegistry: every driver source contributes one driver") {
  disk_loader_reset();
  CHECK(disk_loader_driver_count() == DISK_FORMAT_DRIVER_COUNT);
}

TEST_CASE("DiskRegistry: the same driver registers once") {
  disk_loader_reset();
  const uint32_t baseline = disk_loader_driver_count();

  DiskFormatDriver_t fake = make_fake("Fake Once", probe_no);
  disk_loader_register(&fake);
  disk_loader_register(&fake);
  disk_loader_register(&fake);

  CHECK(disk_loader_driver_count() == baseline + 1);
  disk_loader_reset();
  CHECK(disk_loader_driver_count() == baseline);
}

TEST_CASE("DiskRegistry: a foreign ABI is refused and reported") {
  disk_loader_reset();
  const uint32_t baseline = disk_loader_driver_count();

  DiskFormatDriver_t fake = make_fake("Fake Future", probe_no);
  fake.abi_version = disk_format_abi_version + 1;
  disk_loader_register(&fake);

  CHECK(disk_loader_driver_count() == baseline);

  std::vector<std::string> refused;
  count_rejections(&refused);
  REQUIRE(refused.size() == 1);
  CHECK(refused[0] == "Fake Future");

  disk_loader_reset();
}

TEST_CASE("DiskRegistry: an ambiguous image always resolves the same way") {
  const ScopedRandomFile_t image;

  DiskFormatDriver_t first = make_fake("AAA Fake", probe_possible);
  DiskFormatDriver_t second = make_fake("AAB Fake", probe_possible);

  for (int attempt = 0; attempt < 4; ++attempt) {
    disk_loader_reset();
    // Registered second first, so insertion order cannot be what decides it.
    disk_loader_register(&second);
    disk_loader_register(&first);

    const DiskFormatDriver_t* chosen = nullptr;
    void* instance = nullptr;
    CHECK(disk_loader_open(image.path, &chosen, &instance) == disk_err_none);
    CHECK(chosen == &first);
  }

  disk_loader_reset();
}

TEST_CASE("DiskRegistry: a driver missing read or protect is refused by name") {
  disk_loader_reset();
  const uint32_t baseline = disk_loader_driver_count();

  DiskFormatDriver_t unreadable = make_fake("Fake Unreadable", probe_no);
  unreadable.read_track_bits = nullptr;
  disk_loader_register(&unreadable);

  DiskFormatDriver_t unprotected = make_fake("Fake Unprotected", probe_no);
  unprotected.is_write_protected = nullptr;
  disk_loader_register(&unprotected);

  CHECK(disk_loader_driver_count() == baseline);

  std::vector<std::string> refused;
  count_rejections(&refused);
  REQUIRE(refused.size() == 2);
  CHECK(refused[0] == "Fake Unreadable");
  CHECK(refused[1] == "Fake Unprotected");

  disk_loader_reset();
}

TEST_CASE("DiskRegistry: a second driver with a registered name is refused") {
  disk_loader_reset();
  const uint32_t baseline = disk_loader_driver_count();

  DiskFormatDriver_t impostor = make_fake("DOS Order", probe_no);
  disk_loader_register(&impostor);

  CHECK(disk_loader_driver_count() == baseline);

  std::vector<std::string> refused;
  count_rejections(&refused);
  REQUIRE(refused.size() == 1);
  CHECK(refused[0] == "DOS Order");

  disk_loader_reset();
}

TEST_CASE("DiskRegistry: the extension list reports the length it needs") {
  disk_loader_reset();
  const char* const expected = "do;dsk;iie;nb2;nib;po;woz;gz;zip";
  const size_t needed = strlen(expected);
  REQUIRE(needed == 32);

  CHECK(disk_loader_get_supported_extensions(nullptr, 0) == needed);

  char full[64] = {};
  CHECK(disk_loader_get_supported_extensions(full, sizeof(full)) == needed);
  CHECK(std::string(full) == expected);

  // Truncation keeps the terminator and still reports the whole length, so a
  // caller can size a second buffer from the first answer.
  char tiny[5] = {};
  CHECK(disk_loader_get_supported_extensions(tiny, sizeof(tiny)) == needed);
  CHECK(std::string(tiny) == "do;d");

  char untouched[8] = "keep";
  CHECK(disk_loader_get_supported_extensions(untouched, 0) == needed);
  CHECK(std::string(untouched) == "keep");
}
