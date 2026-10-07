// SPDX-License-Identifier: GPL-2.0-only
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "test_fixtures.h"
#include "test_fixtures_core.h"

extern "C" unsigned harddisk_abi_c_frame_size(void);
extern "C" unsigned harddisk_abi_c_frame_drives_offset(void);
extern "C" unsigned harddisk_abi_c_frame_drive_size(void);
extern "C" unsigned harddisk_abi_c_frame_unit_offset(void);
extern "C" unsigned harddisk_abi_c_insert_size(void);
extern "C" unsigned harddisk_abi_c_insert_path_offset(void);
extern "C" unsigned harddisk_abi_c_insert_drive_offset(void);
extern "C" unsigned harddisk_abi_c_status_size(void);
extern "C" unsigned harddisk_abi_c_state_version(void);
extern "C" uint32_t harddisk_abi_c_insert_id(void);
extern "C" uint32_t harddisk_abi_c_eject_id(void);
extern "C" uint32_t harddisk_abi_c_set_protect_id(void);
extern "C" uint32_t harddisk_abi_c_status_query_id(void);
extern "C" uint32_t harddisk_abi_c_extensions_query_id(void);
extern "C" int harddisk_abi_c_error_none(void);

namespace {

using TestConfig_t = TestFixtures::ScopedTestConfig_t;

constexpr const char* harddisk_id = "linapple.harddisk";
constexpr int card_slot = 7;
constexpr uint32_t foreign_command = 0x9999;
constexpr uint32_t unknown_harddisk_id = PERIPHERAL_SUBSYSTEM_HARDDISK | 0x7FFF;

// Through the registry, so one binary covers the built-in card and a plugin.
auto harddisk() -> Peripheral_t* {
  Peripheral_t* descriptor = peripheral_find_internal(harddisk_id);
  REQUIRE(descriptor != nullptr);
  return descriptor;
}

auto harddisk_in_slot_7() -> TestConfig_t::Description_t {
  TestConfig_t::Description_t description;
  description.slots[card_slot - 1] = "Harddisk";
  return description;
}

auto settle() -> void { peripheral_manager_think(0); }

auto status() -> HarddiskStatus_t {
  HarddiskStatus_t out{};
  size_t size = sizeof(out);
  REQUIRE(peripheral_query(card_slot, harddisk_query_status, &out, &size) ==
          peripheral_ok);
  REQUIRE(size == sizeof(out));
  return out;
}

// A host built by hand, for what the real one cannot show: a member missing,
// the line it draws and the status a call returns.
class BenchHost_t {
 public:
  BenchHost_t() {
    host_.Log = bench_log;
    host_.RegisterIO = bench_register_io;
    host_.RegisterCxROM = bench_register_cx_rom;
    host_.RegisterDirectIO = bench_register_direct_io;
    host_.get_mem_ptr = bench_get_mem_ptr;
    host_.GetConfig = bench_get_config;
    host_.SetConfig = bench_set_config;
    host_.NotifyStatusChanged = bench_notify_status_changed;
    host_.NotifyActivityChanged = bench_notify_activity_changed;
    host_.ReadFloatingBus = bench_read_floating_bus;
    s_active = this;
  }
  ~BenchHost_t() {
    for (void* instance : instances_) {
      harddisk()->shutdown(instance);
    }
    if (s_active == this) {
      s_active = nullptr;
    }
  }
  BenchHost_t(const BenchHost_t&) = delete;
  auto operator=(const BenchHost_t&) -> BenchHost_t& = delete;
  BenchHost_t(BenchHost_t&&) = delete;
  auto operator=(BenchHost_t&&) -> BenchHost_t& = delete;

  auto host() -> HostInterface_t* { return &host_; }

  auto create(int slot = card_slot) -> void* {
    void* instance = harddisk()->init(slot, &host_);
    if (instance != nullptr) {
      instances_.push_back(instance);
    }
    return instance;
  }

  auto last_log() const -> const std::string& { return last_log_; }
  auto status_notifications() const -> unsigned {
    return status_notifications_;
  }
  auto config_writes() const -> unsigned { return config_writes_; }

 private:
  HostInterface_t host_{};
  std::vector<void*> instances_;
  std::string last_log_;
  unsigned status_notifications_ = 0;
  unsigned config_writes_ = 0;
  uint8_t scratch_byte_ = 0;

  static BenchHost_t* s_active;

  // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
  // Justification: Log is variadic in the HostInterface_t ABI.
  static auto bench_log(void* instance, PeripheralLogLevel_t level,
                        const char* fmt, ...) -> void {
    (void)instance;
    (void)level;
    if (s_active == nullptr) {
      return;
    }
    char line[256] = {};
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    s_active->last_log_ = line;
  }
  // NOLINTEND(cppcoreguidelines-pro-type-vararg)

  static auto bench_register_io(int, PeripheralIOHandler, PeripheralIOHandler,
                                PeripheralIOHandler, PeripheralIOHandler)
      -> void {}
  static auto bench_register_cx_rom(int, const uint8_t*) -> void {}
  static auto bench_register_direct_io(void*, uint16_t, PeripheralIOHandler,
                                       PeripheralIOHandler) -> void {}
  static auto bench_get_mem_ptr(uint16_t) -> uint8_t* {
    return s_active != nullptr ? &s_active->scratch_byte_ : nullptr;
  }
  static auto bench_get_config(const char*, const char*, char*, size_t)
      -> bool {
    return false;
  }
  static auto bench_set_config(const char*, const char*, const char*) -> void {
    if (s_active != nullptr) {
      ++s_active->config_writes_;
    }
  }
  static auto bench_notify_status_changed(int) -> void {
    if (s_active != nullptr) {
      ++s_active->status_notifications_;
    }
  }
  static auto bench_notify_activity_changed(int, bool) -> void {}
  static auto bench_read_floating_bus(uint32_t) -> uint8_t { return 0; }
};

BenchHost_t* BenchHost_t::s_active = nullptr;

}  // namespace

TEST_CASE("Harddisk ABI: the C99 view of the headers agrees with the C++ one") {
  CHECK(harddisk_abi_c_frame_size() == sizeof(HarddiskSaveState_t));
  CHECK(harddisk_abi_c_frame_size() == 2160);
  CHECK(harddisk_abi_c_frame_drives_offset() ==
        offsetof(HarddiskSaveState_t, drives));
  CHECK(harddisk_abi_c_frame_drives_offset() == 8);
  CHECK(harddisk_abi_c_frame_drive_size() == sizeof(HarddiskDriveSaveState_t));
  CHECK(harddisk_abi_c_frame_drives_offset() +
            harddisk_abi_c_frame_drive_size() ==
        1080);
  CHECK(harddisk_abi_c_frame_unit_offset() ==
        offsetof(HarddiskSaveState_t, unit_num));
  CHECK(harddisk_abi_c_frame_unit_offset() == 2152);
  CHECK(harddisk_abi_c_state_version() == HARDDISK_STATE_VERSION);
  CHECK(harddisk_abi_c_state_version() == 1);

  CHECK(harddisk_abi_c_insert_size() == sizeof(HarddiskInsertCmd_t));
  CHECK(harddisk_abi_c_insert_size() == PERIPHERAL_CMD_MAX_DATA);
  CHECK(harddisk_abi_c_insert_path_offset() == 0);
  CHECK(harddisk_abi_c_insert_drive_offset() == harddisk_insert_path_max);
  CHECK(harddisk_abi_c_status_size() == sizeof(HarddiskStatus_t));
  CHECK(harddisk_abi_c_status_size() == 1104);

  // High 16 bits the subsystem, low 16 the index.
  CHECK(harddisk_abi_c_insert_id() == 0x00050001u);
  CHECK(harddisk_abi_c_eject_id() == 0x00050002u);
  CHECK(harddisk_abi_c_set_protect_id() == 0x00050004u);
  CHECK(harddisk_abi_c_status_query_id() == 0x00050001u);
  CHECK(harddisk_abi_c_extensions_query_id() == 0x00050002u);
  CHECK(harddisk_abi_c_error_none() == 0);
}

TEST_CASE(
    "Harddisk ABI: the registry finds the card by id, named Harddisk, "
    "preferring slot 7") {
  Peripheral_t* descriptor = harddisk();
  CHECK(descriptor->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(std::string(descriptor->id) == harddisk_id);
  CHECK(std::string(descriptor->name) == "Harddisk");
  CHECK(descriptor->default_slot == card_slot);
  CHECK(peripheral_find_internal("Harddisk") == descriptor);
  REQUIRE(descriptor->init != nullptr);
  REQUIRE(descriptor->reset != nullptr);
  REQUIRE(descriptor->shutdown != nullptr);
  REQUIRE(descriptor->save_state != nullptr);
  REQUIRE(descriptor->load_state != nullptr);
  REQUIRE(descriptor->command != nullptr);
  REQUIRE(descriptor->query != nullptr);
}

TEST_CASE("Harddisk ABI: a null host or a null instance is refused") {
  Peripheral_t* descriptor = harddisk();
  CHECK(descriptor->init(card_slot, nullptr) == nullptr);

  descriptor->reset(nullptr);
  descriptor->shutdown(nullptr);

  HarddiskEjectCmd_t eject{};
  CHECK(descriptor->command(nullptr, harddisk_cmd_eject, &eject,
                            sizeof(eject)) == peripheral_error);

  HarddiskStatus_t out{};
  size_t size = sizeof(out);
  CHECK(descriptor->query(nullptr, harddisk_query_status, &out, &size) ==
        peripheral_error);

  std::vector<uint8_t> frame(sizeof(HarddiskSaveState_t));
  size = frame.size();
  CHECK(descriptor->save_state(nullptr, frame.data(), &size) ==
        peripheral_error);
  CHECK(descriptor->load_state(nullptr, frame.data(), frame.size()) ==
        peripheral_error);
  CHECK(descriptor->save_state(nullptr, nullptr, nullptr) == peripheral_error);
}

TEST_CASE(
    "Harddisk ABI: a command or query from another subsystem, or an id of its "
    "own it does not know, is answered incompatible") {
  BenchHost_t bench;
  void* instance = bench.create();
  REQUIRE(instance != nullptr);
  Peripheral_t* descriptor = harddisk();

  CHECK(descriptor->command(instance, foreign_command, nullptr, 0) ==
        peripheral_incompatible);
  CHECK(descriptor->command(instance, unknown_harddisk_id, nullptr, 0) ==
        peripheral_incompatible);

  uint8_t out[64] = {};
  size_t size = sizeof(out);
  CHECK(descriptor->query(instance, foreign_command, out, &size) ==
        peripheral_incompatible);
  size = sizeof(out);
  CHECK(descriptor->query(instance, unknown_harddisk_id, out, &size) ==
        peripheral_incompatible);
}

TEST_CASE(
    "Harddisk ABI: insert, protect and eject by command set and clear the "
    "status flags") {
  TestConfig_t config(harddisk_in_slot_7());
  TestFixtures::ScopedCore_t core(config);
  REQUIRE(peripheral_present(card_slot, harddisk_id));
  const auto image = TestFixtures::create_ephemeral("minimal-block.hdv");

  CHECK(status().drive0_loaded == 0);

  HarddiskInsertCmd_t insert{};
  insert.drive = harddisk_drive_0;
  std::strncpy(insert.path, image.c_str(), sizeof(insert.path) - 1);
  REQUIRE(peripheral_command(card_slot, harddisk_cmd_insert, &insert,
                             sizeof(insert)) == peripheral_ok);
  settle();
  HarddiskStatus_t loaded = status();
  CHECK(loaded.drive0_loaded == 1);
  CHECK(loaded.drive0_write_protected == 0);
  CHECK(loaded.drive1_loaded == 0);
  CHECK(std::string(loaded.drive0_full_path) == image.path());

  HarddiskSetProtectCmd_t protect{};
  protect.drive = harddisk_drive_0;
  protect.write_protected = 1;
  REQUIRE(peripheral_command(card_slot, harddisk_cmd_set_protect, &protect,
                             sizeof(protect)) == peripheral_ok);
  settle();
  CHECK(status().drive0_write_protected == 1);

  HarddiskEjectCmd_t eject{};
  eject.drive = harddisk_drive_0;
  REQUIRE(peripheral_command(card_slot, harddisk_cmd_eject, &eject,
                             sizeof(eject)) == peripheral_ok);
  settle();
  CHECK(status().drive0_loaded == 0);
}
