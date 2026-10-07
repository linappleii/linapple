// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/HarddiskLoader.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {

constexpr size_t block_size = 512;
constexpr uint32_t volume_directory_key_block = 2;

// Only the loader's registry is wanted here, not a card.
struct ScopedLoader_t {
  ScopedLoader_t() { harddisk_loader_init(); }
  ~ScopedLoader_t() { harddisk_loader_shutdown(); }
  ScopedLoader_t(const ScopedLoader_t&) = delete;
  auto operator=(const ScopedLoader_t&) -> ScopedLoader_t& = delete;
  ScopedLoader_t(ScopedLoader_t&&) = delete;
  auto operator=(ScopedLoader_t&&) -> ScopedLoader_t& = delete;
};

}  // namespace

TEST_CASE(
    "Harddisk drivers: a ProDOS-order image opens through the loader and its "
    "volume directory key block reads back through the driver") {
  ScopedLoader_t loader;
  const std::string path = TestFixtures::get_fixture_path("minimal.po");

  bool read_only = false;
  HarddiskFormatDriver_t* driver = nullptr;
  void* instance = nullptr;
  REQUIRE(harddisk_loader_open(path.c_str(), &read_only, &driver, &instance) ==
          harddisk_err_none);
  REQUIRE(driver != nullptr);
  REQUIRE(instance != nullptr);
  REQUIRE(driver->read_block != nullptr);
  REQUIRE(driver->close != nullptr);

  std::array<uint8_t, block_size> block{};
  CHECK(driver->read_block(instance, volume_directory_key_block,
                           block.data()) == harddisk_err_none);
  // The volume directory header: storage type and name length $F7, then the
  // name (ProDOS 8 Technical Reference Manual, B.2.2).
  CHECK(block.at(4) == 0xF7);
  CHECK(std::memcmp(&block.at(5), "MINIMAL", 7) == 0);

  driver->close(instance);
}
