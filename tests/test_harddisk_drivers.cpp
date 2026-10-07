// SPDX-License-Identifier: GPL-2.0-only
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskError.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/HarddiskLoader.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

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

namespace {

// A host with every member the card asks for and nothing behind any of them,
// for a case that needs the card's answer and not a machine.
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
// Justification: Log is variadic in the HostInterface_t ABI.
auto silent_log(void*, PeripheralLogLevel_t, const char*, ...) -> void {}
// NOLINTEND(cppcoreguidelines-pro-type-vararg)
auto silent_register_io(int, PeripheralIOHandler, PeripheralIOHandler,
                        PeripheralIOHandler, PeripheralIOHandler) -> void {}
auto silent_register_cx_rom(int, const uint8_t*) -> void {}
auto silent_notify_status(int) -> void {}
auto silent_notify_activity(int, bool) -> void {}
auto silent_floating_bus(uint32_t) -> uint8_t { return 0; }

auto silent_host() -> HostInterface_t {
  HostInterface_t host{};
  host.Log = silent_log;
  host.RegisterIO = silent_register_io;
  host.RegisterCxROM = silent_register_cx_rom;
  host.NotifyStatusChanged = silent_notify_status;
  host.NotifyActivityChanged = silent_notify_activity;
  host.ReadFloatingBus = silent_floating_bus;
  return host;
}

constexpr int card_slot = 7;
constexpr const char* missing_path = "/nonexistent/linapple-missing.hdv";

}  // namespace

TEST_CASE(
    "Harddisk drivers: an image the loader refuses is the insert's answer, "
    "an error to a direct caller and the drive's last error through the "
    "queue") {
  Peripheral_t* descriptor = peripheral_find_internal("linapple.harddisk");
  REQUIRE(descriptor != nullptr);

  HarddiskInsertCmd_t cmd{};
  cmd.drive = harddisk_drive_0;
  std::strncpy(cmd.path, missing_path, sizeof(cmd.path) - 1);

  HostInterface_t host = silent_host();
  void* instance = descriptor->init(card_slot, &host);
  REQUIRE(instance != nullptr);
  CHECK(descriptor->command(instance, harddisk_cmd_insert, &cmd, sizeof(cmd)) ==
        peripheral_error);
  HarddiskStatus_t direct{};
  size_t size = sizeof(direct);
  REQUIRE(descriptor->query(instance, harddisk_query_status, &direct, &size) ==
          peripheral_ok);
  CHECK(direct.drive0_loaded == 0);
  CHECK(direct.drive0_last_error == harddisk_err_not_found);
  descriptor->shutdown(instance);

  TestFixtures::ScopedTestConfig_t::Description_t description;
  description.slots[card_slot - 1] = "Harddisk";
  TestFixtures::ScopedTestConfig_t config(description);
  TestFixtures::ScopedCore_t core(config);
  REQUIRE(peripheral_present(card_slot, "linapple.harddisk"));
  CHECK(peripheral_command(card_slot, harddisk_cmd_insert, &cmd, sizeof(cmd)) ==
        peripheral_ok);
  peripheral_manager_think(0);
  HarddiskStatus_t queued{};
  size = sizeof(queued);
  REQUIRE(peripheral_query(card_slot, harddisk_query_status, &queued, &size) ==
          peripheral_ok);
  CHECK(queued.drive0_loaded == 0);
  CHECK(queued.drive0_last_error == harddisk_err_not_found);
}
