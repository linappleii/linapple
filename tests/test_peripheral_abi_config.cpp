// SPDX-License-Identifier: GPL-2.0-only
#include <string>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/peripherals/clock/Clock.h"
#include "apple2/peripherals/disk/Disk.h"
#include "apple2/peripherals/mockingboard/Mockingboard.h"
#include "apple2/peripherals/printer/Printer.h"
#include "apple2/peripherals/super_serial_card/SuperSerial.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "doctest.h"

namespace {

struct ScopedPeripheralManager_t {
  ScopedPeripheralManager_t() { peripheral_manager_init(); }
  ~ScopedPeripheralManager_t() { peripheral_manager_shutdown(); }
};

}  // namespace

TEST_CASE("Peripheral ABI Config: ABI Version Check") {
  CHECK(LINAPPLE_ABI_VERSION == 1);
}

TEST_CASE("Peripheral ABI Config: Built-in Peripheral Schemas") {
  // 1. Parallel Printer
  auto* printer = printer_get_descriptor();
  REQUIRE(printer != nullptr);
  CHECK(printer->abi_version == LINAPPLE_ABI_VERSION);
  REQUIRE(printer->get_config_schema != nullptr);
  const auto* printer_schema = printer->get_config_schema();
  REQUIRE(printer_schema != nullptr);
  CHECK(printer_schema->option_count == 3);
  CHECK(std::string(printer_schema->options[0].name) == "Filename");
  CHECK(printer_schema->options[0].type == peripheral_config_filepath);
  CHECK(std::string(printer_schema->options[0].default_value) == "Printer.txt");
  CHECK(std::string(printer_schema->options[1].name) == "IdleLimit");
  CHECK(printer_schema->options[1].type == peripheral_config_int);
  CHECK(std::string(printer_schema->options[1].default_value) == "10");
  CHECK(std::string(printer_schema->options[2].name) == "Append");
  CHECK(printer_schema->options[2].type == peripheral_config_bool);
  CHECK(std::string(printer_schema->options[2].default_value) == "true");

  // 2. Super Serial Card
  auto* ssc = super_serial_get_descriptor();
  REQUIRE(ssc != nullptr);
  CHECK(ssc->abi_version == LINAPPLE_ABI_VERSION);
  REQUIRE(ssc->get_config_schema != nullptr);
  const auto* ssc_schema = ssc->get_config_schema();
  REQUIRE(ssc_schema != nullptr);
  CHECK(ssc_schema->option_count == 3);
  CHECK(std::string(ssc_schema->options[0].name) == "Port");
  CHECK(ssc_schema->options[0].type == peripheral_config_string);
  CHECK(std::string(ssc_schema->options[1].name) == "Baud");
  CHECK(ssc_schema->options[1].type == peripheral_config_int);
  CHECK(std::string(ssc_schema->options[2].name) == "Loopback");
  CHECK(ssc_schema->options[2].type == peripheral_config_bool);

  // 3. Disk II
  auto* disk = disk_get_descriptor();
  REQUIRE(disk != nullptr);
  CHECK(disk->abi_version == LINAPPLE_ABI_VERSION);
  REQUIRE(disk->get_config_schema != nullptr);
  const auto* disk_schema = disk->get_config_schema();
  REQUIRE(disk_schema != nullptr);
  CHECK(disk_schema->option_count == 3);
  CHECK(std::string(disk_schema->options[0].name) == "Drive1");
  CHECK(disk_schema->options[0].type == peripheral_config_filepath);
  CHECK(std::string(disk_schema->options[1].name) == "Drive2");
  CHECK(disk_schema->options[1].type == peripheral_config_filepath);
  CHECK(std::string(disk_schema->options[2].name) == "FastDisk");
  CHECK(disk_schema->options[2].type == peripheral_config_bool);

  // 4. Mockingboard
  auto* mb = mockingboard_get_descriptor();
  REQUIRE(mb != nullptr);
  CHECK(mb->abi_version == LINAPPLE_ABI_VERSION);
  REQUIRE(mb->get_config_schema != nullptr);
  const auto* mb_schema = mb->get_config_schema();
  REQUIRE(mb_schema != nullptr);
  CHECK(mb_schema->option_count == 2);
  CHECK(std::string(mb_schema->options[0].name) == "Type");
  CHECK(mb_schema->options[0].type == peripheral_config_enum);
  CHECK(std::string(mb_schema->options[0].default_value) == "Mockingboard");
  CHECK(std::string(mb_schema->options[1].name) == "Volume");
  CHECK(mb_schema->options[1].type == peripheral_config_int);
  CHECK(std::string(mb_schema->options[1].default_value) == "50");
  CHECK(mb_schema->options[1].max_int == 100);
}

TEST_CASE("Peripheral ABI Config: Schema Query by ID") {
  const ScopedPeripheralManager_t scoped_pm;

  peripheral_register_builtin(printer_get_descriptor());
  const auto* printer_schema =
      peripheral_get_config_schema_by_id("linapple.printer");
  REQUIRE(printer_schema != nullptr);
  CHECK(printer_schema->option_count == 3);

  peripheral_register_builtin(disk_get_descriptor());
  const auto* disk_schema =
      peripheral_get_config_schema_by_id("linapple.disk_II");
  REQUIRE(disk_schema != nullptr);
  CHECK(disk_schema->option_count == 3);

  CHECK(peripheral_get_config_schema_by_id("linapple.nonexistent") == nullptr);
  CHECK(peripheral_get_config_schema_by_id(nullptr) == nullptr);
}

TEST_CASE("Peripheral ABI Config: Defensive Null & Range Checks") {
  const ScopedPeripheralManager_t scoped_pm;

  CHECK(peripheral_get_config_schema(-1) == nullptr);
  CHECK(peripheral_get_config_schema(8) == nullptr);
  CHECK(peripheral_get_config_schema(0) == nullptr);

  CHECK(peripheral_configure(-1, "Key", "Val") == peripheral_error);
  CHECK(peripheral_configure(8, "Key", "Val") == peripheral_error);
  CHECK(peripheral_configure(1, nullptr, "Val") == peripheral_error);
  CHECK(peripheral_configure(1, "Key", nullptr) == peripheral_error);
}

TEST_CASE("Peripheral ABI Config: Dynamic Configuration Callbacks") {
  const ScopedPeripheralManager_t scoped_pm;

  auto* ssc = super_serial_get_descriptor();
  REQUIRE(ssc != nullptr);
  int reg_result = peripheral_register(ssc, 2);
  REQUIRE(reg_result == 0);

  // Check schema on registered slot
  const auto* schema = peripheral_get_config_schema(2);
  REQUIRE(schema != nullptr);
  CHECK(schema->option_count == 3);
  CHECK(std::string(schema->options[0].name) == "Port");
  CHECK(std::string(schema->options[0].default_value) == "/dev/null");
  CHECK(std::string(schema->options[1].name) == "Baud");
  CHECK(std::string(schema->options[1].default_value) == "9600");
  CHECK(std::string(schema->options[2].name) == "Loopback");
  CHECK(std::string(schema->options[2].default_value) == "false");

  // Dispatch configure calls across public C-ABI
  PeripheralStatus_t status1 = peripheral_configure(2, "Port", "/dev/ttyUSB0");
  CHECK(status1 == peripheral_ok);

  PeripheralStatus_t status2 = peripheral_configure(2, "Baud", "19200");
  CHECK(status2 == peripheral_ok);

  PeripheralStatus_t status3 = peripheral_configure(2, "Loopback", "true");
  CHECK(status3 == peripheral_ok);

  // Unrecognized key -> must return peripheral_incompatible
  PeripheralStatus_t status_bad_key =
      peripheral_configure(2, "NonExistentKey", "Value");
  CHECK(status_bad_key == peripheral_incompatible);
}

TEST_CASE("Peripheral ABI Config: Card Without Configure Implementation") {
  const ScopedPeripheralManager_t scoped_pm;

  auto* clock = clock_get_descriptor();
  REQUIRE(clock != nullptr);
  int reg_result = peripheral_register(clock, 4);
  REQUIRE(reg_result == 0);

  // Card without get_config_schema returns nullptr
  CHECK(peripheral_get_config_schema(4) == nullptr);

  // Card without configure returns peripheral_error
  CHECK(peripheral_configure(4, "AnyKey", "AnyValue") == peripheral_error);
}

TEST_CASE("Peripheral ABI Config: Built-in Card Key Validation") {
  const ScopedPeripheralManager_t scoped_pm;

  auto* printer = printer_get_descriptor();
  REQUIRE(printer != nullptr);
  int reg = peripheral_register(printer, 1);
  REQUIRE(reg == 0);

  // Recognized keys succeed
  CHECK(peripheral_configure(1, "Filename", "Out.txt") == peripheral_ok);
  CHECK(peripheral_configure(1, "IdleLimit", "15") == peripheral_ok);
  CHECK(peripheral_configure(1, "Append", "false") == peripheral_ok);

  // Unrecognized key returns peripheral_incompatible
  CHECK(peripheral_configure(1, "InvalidKey", "Val") ==
        peripheral_incompatible);
}
