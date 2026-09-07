// SPDX-License-Identifier: GPL-2.0-only
#include <cstring>
#include <string>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/peripherals/disk/Disk.h"
#include "apple2/peripherals/mockingboard/Mockingboard.h"
#include "apple2/peripherals/printer/Printer.h"
#include "apple2/peripherals/super_serial_card/SuperSerial.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "doctest.h"

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
  const auto* printer_schema =
      peripheral_get_config_schema_by_id("linapple.printer");
  REQUIRE(printer_schema != nullptr);
  CHECK(printer_schema->option_count == 3);

  const auto* disk_schema =
      peripheral_get_config_schema_by_id("linapple.disk_II");
  REQUIRE(disk_schema != nullptr);
  CHECK(disk_schema->option_count == 3);

  CHECK(peripheral_get_config_schema_by_id("linapple.nonexistent") == nullptr);
  CHECK(peripheral_get_config_schema_by_id(nullptr) == nullptr);
}

TEST_CASE("Peripheral ABI Config: Defensive Null & Range Checks") {
  CHECK(peripheral_get_config_schema(-1) == nullptr);
  CHECK(peripheral_get_config_schema(8) == nullptr);
  CHECK(peripheral_get_config_schema(0) == nullptr);

  CHECK(peripheral_configure(-1, "Key", "Val") == peripheral_error);
  CHECK(peripheral_configure(8, "Key", "Val") == peripheral_error);
  CHECK(peripheral_configure(1, nullptr, "Val") == peripheral_error);
  CHECK(peripheral_configure(1, "Key", nullptr) == peripheral_error);
}

namespace {

struct CustomConfigPeripheral_t {
  std::string last_configured_key;
  std::string last_configured_value;
};

static CustomConfigPeripheral_t g_custom_instance;

static const char* const g_enum_allowed_values[] = {"OptionA", "OptionB",
                                                    nullptr};

static const PeripheralConfigOption_t g_custom_options[] = {
    {"SettingA", "Test setting A", "defaultA", peripheral_config_string,
     nullptr, 0, 0},
    {"SettingB", "Test setting B", "42", peripheral_config_int, nullptr, 0,
     100},
    {"SettingEnum", "Test enum setting", "OptionA", peripheral_config_enum,
     g_enum_allowed_values, 0, 0}};

static const PeripheralConfigSchema_t g_custom_schema = {
    g_custom_options, sizeof(g_custom_options) / sizeof(g_custom_options[0])};

static auto custom_init(int slot, HostInterface_t* host) -> void* {
  (void)slot;
  (void)host;
  g_custom_instance.last_configured_key.clear();
  g_custom_instance.last_configured_value.clear();
  return &g_custom_instance;
}

static auto custom_shutdown(void* instance) -> void { (void)instance; }

static auto custom_get_schema() -> const PeripheralConfigSchema_t* {
  return &g_custom_schema;
}

static auto custom_configure(void* instance, const char* key, const char* value)
    -> PeripheralStatus_t {
  if (instance == nullptr || key == nullptr || value == nullptr) {
    return peripheral_error;
  }
  if (std::strcmp(key, "SettingA") == 0 || std::strcmp(key, "SettingB") == 0 ||
      std::strcmp(key, "SettingEnum") == 0) {
    auto* custom = static_cast<CustomConfigPeripheral_t*>(instance);
    custom->last_configured_key = key;
    custom->last_configured_value = value;
    return peripheral_ok;
  }
  return peripheral_incompatible;
}

static Peripheral_t g_custom_peripheral = {LINAPPLE_ABI_VERSION,
                                           "test.custom_config",
                                           "Custom Config Peripheral",
                                           "Tests dynamic configuration ABI",
                                           "Test Author",
                                           "1.0.0",
                                           0xFE,  // Slots 1-7
                                           -1,
                                           custom_init,
                                           nullptr,  // reset
                                           custom_shutdown,
                                           nullptr,  // think
                                           nullptr,  // on_vblank
                                           nullptr,  // save_state
                                           nullptr,  // load_state
                                           nullptr,  // command
                                           nullptr,  // query
                                           custom_get_schema,
                                           custom_configure};

// Peripheral with NO configure callback
static auto dummy_no_cfg_init(int slot, HostInterface_t* host) -> void* {
  (void)slot;
  (void)host;
  return reinterpret_cast<void*>(0x1234);
}

static Peripheral_t g_no_cfg_peripheral = {LINAPPLE_ABI_VERSION,
                                           "test.no_cfg",
                                           "No Config Peripheral",
                                           "Card without configure support",
                                           "Test Author",
                                           "1.0.0",
                                           0xFE,  // Slots 1-7
                                           -1,
                                           dummy_no_cfg_init,
                                           nullptr,   // reset
                                           nullptr,   // shutdown
                                           nullptr,   // think
                                           nullptr,   // on_vblank
                                           nullptr,   // save_state
                                           nullptr,   // load_state
                                           nullptr,   // command
                                           nullptr,   // query
                                           nullptr,   // get_config_schema
                                           nullptr};  // configure

}  // namespace

TEST_CASE("Peripheral ABI Config: Dynamic Configuration Callbacks") {
  peripheral_manager_init();

  int reg_result = peripheral_register(&g_custom_peripheral, 3);
  REQUIRE(reg_result == 0);

  // Check schema on registered slot
  const auto* schema = peripheral_get_config_schema(3);
  REQUIRE(schema != nullptr);
  CHECK(schema->option_count == 3);
  CHECK(std::string(schema->options[0].name) == "SettingA");
  CHECK(std::string(schema->options[0].default_value) == "defaultA");
  CHECK(std::string(schema->options[1].name) == "SettingB");
  CHECK(schema->options[2].allowed_values != nullptr);
  CHECK(std::string(schema->options[2].allowed_values[0]) == "OptionA");
  CHECK(std::string(schema->options[2].allowed_values[1]) == "OptionB");
  CHECK(schema->options[2].allowed_values[2] == nullptr);

  // Dispatch configure calls and verify values applied to instance
  PeripheralStatus_t status1 =
      peripheral_configure(3, "SettingA", "CustomValue123");
  CHECK(status1 == peripheral_ok);
  CHECK(g_custom_instance.last_configured_key == "SettingA");
  CHECK(g_custom_instance.last_configured_value == "CustomValue123");

  PeripheralStatus_t status2 = peripheral_configure(3, "SettingB", "99");
  CHECK(status2 == peripheral_ok);
  CHECK(g_custom_instance.last_configured_key == "SettingB");
  CHECK(g_custom_instance.last_configured_value == "99");

  // Unrecognized key -> must return peripheral_incompatible
  PeripheralStatus_t status_bad_key =
      peripheral_configure(3, "NonExistentKey", "Value");
  CHECK(status_bad_key == peripheral_incompatible);

  peripheral_unregister(3);
}

TEST_CASE("Peripheral ABI Config: Card Without Configure Implementation") {
  peripheral_manager_init();

  int reg_result = peripheral_register(&g_no_cfg_peripheral, 4);
  REQUIRE(reg_result == 0);

  // Card without get_config_schema returns nullptr
  CHECK(peripheral_get_config_schema(4) == nullptr);

  // Card without configure returns peripheral_error
  CHECK(peripheral_configure(4, "AnyKey", "AnyValue") == peripheral_error);

  peripheral_unregister(4);
}

TEST_CASE("Peripheral ABI Config: Built-in Card Key Validation") {
  peripheral_manager_init();

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

  peripheral_unregister(1);
}
