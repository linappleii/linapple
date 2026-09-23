// SPDX-License-Identifier: GPL-2.0-only
#include <sys/stat.h>

#include <cstdint>

#include "apple2/peripherals/Peripheral_Types.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {
// Declared rather than inherited: with no configuration the slot fallbacks in
// peripheral_register_internal supply a printer, a Super Serial Card and a
// Mockingboard beside the Disk II, none of which these cases touch.
using TestConfig_t = TestFixtures::ScopedTestConfig_t;

constexpr size_t DISK_ABI_CMD_SIZE = 512;
constexpr int SL6 = 6;
constexpr uint8_t BUFFER_INIT_VAL = 0xAA;
constexpr uint32_t BAD_VERSION = 0xdeadbeef;
}  // namespace

TEST_CASE("DiskABI: [DISK-01] Command payloads fit the command queue") {
  CHECK(sizeof(DiskInsertCmd_t) == DISK_ABI_CMD_SIZE);
  CHECK(sizeof(DiskCreateImageCmd_t) == DISK_ABI_CMD_SIZE);
  CHECK(sizeof(DiskFormatNameQuery_t) == 72);
  CHECK(offsetof(DiskFormatNameQuery_t, capabilities) == 4);
  CHECK(offsetof(DiskFormatNameQuery_t, name) == 8);
}

TEST_CASE("DiskABI: [DISK-02] DiskInsertCmd_t field offsets are stable") {
  CHECK(offsetof(DiskInsertCmd_t, path) == 0);
  CHECK(offsetof(DiskInsertCmd_t, drive) == 504);
  CHECK(offsetof(DiskInsertCmd_t, write_protected) == 505);
  CHECK(offsetof(DiskInsertCmd_t, reserved) == 506);
}

TEST_CASE("DiskABI: [DISK-03] Enum values match ABI specification") {
  CHECK(disk_drive_0 == 0);
  CHECK(disk_drive_1 == 1);
  // An id is its subsystem in the high half and its index in the low half.
  // These are the numbers a caller built against the ABI holds, so they are
  // written out rather than rebuilt from the constants they are made of.
  CHECK(disk_cmd_insert == 0x00040001u);
  CHECK(disk_cmd_eject == 0x00040002u);
  CHECK(disk_state_version == 1);
}

TEST_CASE(
    "DiskABI: [DISK-04] DiskStatus_t field offsets are stable (NATURAL)") {
  // Field order: drive0_error(4), drive1_error(4), drive0_loaded(1), ...
  CHECK(offsetof(DiskStatus_t, drive0_last_error) == 0);
  CHECK(offsetof(DiskStatus_t, drive1_last_error) == 4);
  CHECK(offsetof(DiskStatus_t, drive0_loaded) == 8);
  CHECK(offsetof(DiskStatus_t, drive0_spinning) == 9);
  CHECK(offsetof(DiskStatus_t, drive0_writing) == 10);
  CHECK(offsetof(DiskStatus_t, drive0_write_protected) == 11);
}

extern "C" auto disk_get_descriptor() -> Peripheral_t*;

PeripheralIOHandler g_captured_disk_read = nullptr;

// The descriptor hands its read handler to RegisterIO and keeps no other way
// out, so a case that wants to drive a softswitch has to catch it there.
static auto capturing_disk_host() -> HostInterface_t {
  HostInterface_t h{};
  h.RegisterIO = [](int, PeripheralIOHandler read_c0, PeripheralIOHandler,
                    PeripheralIOHandler, PeripheralIOHandler) {
    g_captured_disk_read = read_c0;
  };
  h.RegisterCxROM = [](int, const uint8_t*) {};
  h.GetConfig = [](const char*, const char*, char*, size_t) { return false; };
  h.SetConfig = [](const char*, const char*, const char*) {};
  h.NotifyStatusChanged = [](int) {};
  return h;
}

static HostInterface_t g_test_disk_host = [] {
  HostInterface_t h{};
  h.RegisterIO = [](int, PeripheralIOHandler, PeripheralIOHandler,
                    PeripheralIOHandler, PeripheralIOHandler) {};
  h.RegisterCxROM = [](int, const uint8_t*) {};
  h.GetConfig = [](const char*, const char*, char*, size_t) { return false; };
  h.SetConfig = [](const char*, const char*, const char*) {};
  h.NotifyStatusChanged = [](int) {};
  return h;
}();

TEST_CASE("DiskABI: [ABI-07] SaveState Size Query") {
  TestConfig_t machine(TestConfig_t::disk_ii_only());
  machine.load();
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();
  size_t size = 0;
  peripheral_save_state(SL6, nullptr, &size);
  CHECK(size == sizeof(DiskSavedState_t));
  linapple_shutdown();
}

TEST_CASE("DiskABI: [ABI-07a] DiskSavedState_t layout stability") {
  CHECK(sizeof(DiskStateHeader_t) == 8);
  CHECK(offsetof(DiskStateHeader_t, version) == 0);
  CHECK(offsetof(DiskStateHeader_t, size) == 4);

  CHECK(sizeof(DiskDriveState_t) == 6940);
  CHECK(offsetof(DiskDriveState_t, full_path) == 0);
  CHECK(offsetof(DiskDriveState_t, track) == 256);
  CHECK(offsetof(DiskDriveState_t, phase) == 260);
  CHECK(offsetof(DiskDriveState_t, current_byte_pos) == 264);
  CHECK(offsetof(DiskDriveState_t, user_write_protected) == 268);
  CHECK(offsetof(DiskDriveState_t, reserved_os_read_only) == 269);
  CHECK(offsetof(DiskDriveState_t, is_data_loaded) == 270);
  CHECK(offsetof(DiskDriveState_t, is_dirty) == 271);
  CHECK(offsetof(DiskDriveState_t, spinning_ticks) == 272);
  CHECK(offsetof(DiskDriveState_t, write_light_ticks) == 276);
  CHECK(offsetof(DiskDriveState_t, nibble_count) == 280);
  CHECK(offsetof(DiskDriveState_t, track_buffer) == 284);

  CHECK(sizeof(DiskSavedState_t) == 13897);
  CHECK(offsetof(DiskSavedState_t, header) == 0);
  CHECK(offsetof(DiskSavedState_t, drives) == 8);
  CHECK(offsetof(DiskSavedState_t, stepper_phase_mask) == 13888);
  CHECK(offsetof(DiskSavedState_t, active_drive_index) == 13890);
  CHECK(offsetof(DiskSavedState_t, reserved_tick) == 13892);
  CHECK(offsetof(DiskSavedState_t, reserved_speed) == 13893);
  CHECK(offsetof(DiskSavedState_t, io_latch) == 13894);
  CHECK(offsetof(DiskSavedState_t, is_motor_on) == 13895);
  CHECK(offsetof(DiskSavedState_t, is_write_mode) == 13896);
}

TEST_CASE("DiskABI: [ABI-08] SaveState Undersized Buffer") {
  TestConfig_t machine(TestConfig_t::disk_ii_only());
  machine.load();
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();
  std::array<uint8_t, 4> buffer{};
  size_t size = buffer.size();
  buffer.fill(BUFFER_INIT_VAL);
  peripheral_save_state(SL6, buffer.data(), &size);
  CHECK(buffer[0] == BUFFER_INIT_VAL);
  linapple_shutdown();

  auto* descriptor = disk_get_descriptor();
  REQUIRE(descriptor != nullptr);
  void* instance = descriptor->init(SL6, &g_test_disk_host);
  REQUIRE(instance != nullptr);
  size_t undersized = 4;
  PeripheralStatus_t status =
      descriptor->save_state(instance, buffer.data(), &undersized);
  CHECK(status == peripheral_error);
  descriptor->shutdown(instance);
}

TEST_CASE("DiskABI: [ABI-09] LoadState Version Mismatch") {
  TestConfig_t machine(TestConfig_t::disk_ii_only());
  machine.load();
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();
  size_t size = 0;
  peripheral_save_state(SL6, nullptr, &size);
  std::vector<uint8_t> buffer(size);
  peripheral_save_state(SL6, buffer.data(), &size);

  auto* state = reinterpret_cast<DiskSavedState_t*>(buffer.data());
  state->header.version = BAD_VERSION;

  peripheral_load_state(SL6, buffer.data(), size);
  linapple_shutdown();

  auto* descriptor = disk_get_descriptor();
  REQUIRE(descriptor != nullptr);
  void* instance = descriptor->init(SL6, &g_test_disk_host);
  REQUIRE(instance != nullptr);
  PeripheralStatus_t status =
      descriptor->load_state(instance, buffer.data(), size);
  CHECK(status == peripheral_error);
  descriptor->shutdown(instance);
}

TEST_CASE("DiskABI: [ABI-10] Get Supported Extensions Query") {
  TestConfig_t machine(TestConfig_t::disk_ii_only());
  machine.load();
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();

  const char* const expected = "do;dsk;iie;nb2;nib;po;woz;gz;zip";
  const size_t needed = strlen(expected) + 1;

  char exts[256] = {};
  size_t size = sizeof(exts);
  PeripheralStatus_t status =
      peripheral_query(SL6, disk_query_supported_extensions, exts, &size);
  CHECK(status == peripheral_ok);
  CHECK(size == needed);
  CHECK(std::string(exts) == expected);

  // A buffer too short for the list is told the size it needs, not handed a
  // truncated list it would take for the whole one.
  char tiny[5] = {};
  size = sizeof(tiny);
  status = peripheral_query(SL6, disk_query_supported_extensions, tiny, &size);
  CHECK(status == peripheral_error);
  CHECK(size == needed);
  CHECK(tiny[0] == '\0');

  linapple_shutdown();
}

TEST_CASE("DiskABI: [DISK-11] Insert Command NUL Terminator Check") {
  auto* descriptor = disk_get_descriptor();
  REQUIRE(descriptor != nullptr);
  void* instance = descriptor->init(SL6, &g_test_disk_host);
  REQUIRE(instance != nullptr);

  DiskInsertCmd_t cmd;
  memset(&cmd, 'A', sizeof(cmd));  // No NUL terminator anywhere in struct
  cmd.drive = 0;
  cmd.write_protected = 0;

  PeripheralStatus_t status =
      descriptor->command(instance, disk_cmd_insert, &cmd, sizeof(cmd));
  CHECK(status == peripheral_error);

  descriptor->shutdown(instance);
}

namespace {
int g_set_config_calls = 0;
}  // namespace

TEST_CASE("DiskABI: [ABI-15] Mechanical events never write the config") {
  g_set_config_calls = 0;

  HostInterface_t host{};
  host.RegisterIO = [](int, PeripheralIOHandler, PeripheralIOHandler,
                       PeripheralIOHandler, PeripheralIOHandler) {};
  host.RegisterCxROM = [](int, const uint8_t*) {};
  host.GetConfig = [](const char*, const char*, char*, size_t) {
    return false;
  };
  host.SetConfig = [](const char*, const char*, const char*) {
    ++g_set_config_calls;
  };
  host.NotifyStatusChanged = [](int) {};

  auto* descriptor = disk_get_descriptor();
  REQUIRE(descriptor != nullptr);
  void* instance = descriptor->init(SL6, &host);
  REQUIRE(instance != nullptr);

  const std::string fixture = TestFixtures::get_fixture_path("minimal.woz");
  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  strncpy(cmd.path, fixture.c_str(), sizeof(cmd.path) - 1);
  CHECK(descriptor->command(instance, disk_cmd_insert, &cmd, sizeof(cmd)) ==
        peripheral_ok);

  CHECK(descriptor->command(instance, disk_cmd_swap_drives, nullptr, 0) ==
        peripheral_ok);

  DiskEjectCmd_t eject{};
  eject.drive = disk_drive_1;
  CHECK(descriptor->command(instance, disk_cmd_eject, &eject,
                            sizeof(eject)) == peripheral_ok);

  descriptor->shutdown(instance);

  // Which image sits in which drive is the user's configuration: only the
  // frontend that acted on the user's behalf may write it, and quitting must
  // not blank it.
  CHECK(g_set_config_calls == 0);
}

TEST_CASE("DiskABI: [ABI-16] A created image is one a drive can take") {
  TestConfig_t machine(TestConfig_t::disk_ii_only());
  machine.load();
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();

  TestFixtures::ScopedTempDir_t work_dir("linapple_disk_create_test_");
  const std::string image_path = work_dir.path() + "/blank.dsk";

  DiskCreateImageCmd_t create{};
  strncpy(create.path, image_path.c_str(), sizeof(create.path) - 1);
  strncpy(create.format_name, "DOS Order", sizeof(create.format_name) - 1);
  peripheral_command(SL6, disk_cmd_create_image, &create, sizeof(create));
  peripheral_manager_think(0);

  constexpr int64_t dos_33_image_size = 143360;
  struct stat created {};
  REQUIRE(stat(image_path.c_str(), &created) == 0);
  CHECK(created.st_size == dos_33_image_size);

  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  strncpy(cmd.path, image_path.c_str(), sizeof(cmd.path) - 1);
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  DiskStatus_t status{};
  size_t size = sizeof(status);
  REQUIRE(peripheral_query(SL6, disk_query_status, &status, &size) ==
          peripheral_ok);
  CHECK(status.drive0_loaded == 1);
  CHECK(status.drive0_last_error == disk_err_none);

  linapple_shutdown();
}

TEST_CASE("DiskABI: [ABI-17] Creating an image refuses to overwrite") {
  TestConfig_t machine(TestConfig_t::disk_ii_only());
  machine.load();
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();

  auto* descriptor = disk_get_descriptor();
  REQUIRE(descriptor != nullptr);
  void* instance = descriptor->init(SL6, &g_test_disk_host);
  REQUIRE(instance != nullptr);

  TestFixtures::ScopedTempDir_t work_dir("linapple_disk_create_test_");
  const std::string image_path = work_dir.path() + "/occupied.dsk";
  const std::string contents = "not a disk image";
  {
    FILE* existing = fopen(image_path.c_str(), "wb");
    REQUIRE(existing != nullptr);
    fwrite(contents.data(), 1, contents.size(), existing);
    fclose(existing);
  }

  DiskCreateImageCmd_t create{};
  strncpy(create.path, image_path.c_str(), sizeof(create.path) - 1);
  strncpy(create.format_name, "DOS Order", sizeof(create.format_name) - 1);
  CHECK(descriptor->command(instance, disk_cmd_create_image, &create,
                            sizeof(create)) == peripheral_error);

  struct stat untouched {};
  REQUIRE(stat(image_path.c_str(), &untouched) == 0);
  CHECK(untouched.st_size == static_cast<int64_t>(contents.size()));

  // A format nobody registered is not a format the card can make
  DiskCreateImageCmd_t unknown{};
  strncpy(unknown.path, (work_dir.path() + "/unknown.dsk").c_str(),
          sizeof(unknown.path) - 1);
  strncpy(unknown.format_name, "Tape", sizeof(unknown.format_name) - 1);
  CHECK(descriptor->command(instance, disk_cmd_create_image, &unknown,
                            sizeof(unknown)) == peripheral_error);
  struct stat not_created {};
  CHECK(stat(unknown.path, &not_created) != 0);

  descriptor->shutdown(instance);
  linapple_shutdown();
}

TEST_CASE("DiskABI: [ABI-18] The card lists the formats it can make") {
  TestConfig_t machine(TestConfig_t::disk_ii_only());
  machine.load();
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();

  uint32_t count = 0;
  size_t size = sizeof(count);
  REQUIRE(peripheral_query(SL6, disk_query_format_count, &count, &size) ==
          peripheral_ok);
  CHECK(size == sizeof(uint32_t));
  // Alphabetical, because the probe's fallback picks the first driver that
  // calls an image possible and that must not depend on link order.
  const char* const expected_order[] = {"DOS Order",         "IIE",
                                        "NB2 (6384-nibble)",
                                        "NIB (6656-nibble)", "ProDOS Order",
                                        "WOZ 1",             "WOZ 2"};
  // Only the sector and nibble formats can make a blank; the WOZ and IIE
  // drivers read what they are given.
  const bool expected_creatable[] = {true, false, true, true,
                                     true, false, false};
  REQUIRE(count == sizeof(expected_order) / sizeof(expected_order[0]));
  for (uint32_t i = 0; i < count; ++i) {
    DiskFormatNameQuery_t name_query{};
    name_query.index = i;
    size = sizeof(name_query);
    REQUIRE(peripheral_query(SL6, disk_query_format_name, &name_query,
                             &size) == peripheral_ok);
    CHECK(std::string(name_query.name) == expected_order[i]);
    CHECK(((name_query.capabilities & disk_driver_cap_create) != 0) ==
          expected_creatable[i]);
  }

  DiskFormatNameQuery_t past_the_end{};
  past_the_end.index = count;
  size = sizeof(past_the_end);
  CHECK(peripheral_query(SL6, disk_query_format_name, &past_the_end, &size) ==
        peripheral_error);

  linapple_shutdown();
}

TEST_CASE("DiskABI: [REG-15] DiskLoader registration validation") {
  disk_loader_reset();
  const uint32_t baseline = disk_loader_driver_count();

  disk_loader_register(nullptr);

  DiskFormatDriver_t missing_entry_points{};
  disk_loader_register(&missing_entry_points);

  DiskFormatDriver_t usable{};
  usable.probe = [](const uint8_t*, size_t, uint32_t, const char*) {
    return disk_probe_no;
  };
  usable.open = [](const char*, uint32_t, bool, void**) {
    return disk_err_none;
  };
  usable.close = [](void*) {};
  usable.is_write_protected = [](void*) { return true; };
  usable.read_track_bits = [](void*, uint32_t, uint8_t*, uint32_t,
                              uint32_t* out_bit_count,
                              uint8_t* out_bit_timing) {
    *out_bit_count = 0;
    *out_bit_timing = static_cast<uint8_t>(disk_default_bit_timing);
    return disk_err_none;
  };

  DiskFormatDriver_t write_cap_mismatch = usable;
  write_cap_mismatch.capabilities = disk_driver_cap_write;
  write_cap_mismatch.write_track_bits = nullptr;
  disk_loader_register(&write_cap_mismatch);

  DiskFormatDriver_t foreign_abi = usable;
  foreign_abi.abi_version = disk_format_abi_version + 1;
  disk_loader_register(&foreign_abi);

  CHECK(disk_loader_driver_count() == baseline);

  int refused = 0;
  disk_loader_drain_rejections(
      [](void* context, const char*, const char*) {
        ++*static_cast<int*>(context);
      },
      &refused);
  CHECK(refused == 4);

  disk_loader_register(&usable);
  disk_loader_register(&usable);
  CHECK(disk_loader_driver_count() == baseline + 1);

  disk_loader_reset();
  CHECK(disk_loader_driver_count() == baseline);
}

TEST_CASE("DiskABI: [ABI-12] Query Sizing Probe and Status Query") {
  auto* descriptor = disk_get_descriptor();
  REQUIRE(descriptor != nullptr);
  void* instance = descriptor->init(SL6, &g_test_disk_host);
  REQUIRE(instance != nullptr);

  // Sizing probe for disk_query_status
  size_t size = 0;
  PeripheralStatus_t status =
      descriptor->query(instance, disk_query_status, nullptr, &size);
  CHECK(status == peripheral_ok);
  CHECK(size == sizeof(DiskStatus_t));

  // Undersized buffer returns peripheral_error
  DiskStatus_t disk_stat{};
  size = sizeof(DiskStatus_t) - 1;
  status = descriptor->query(instance, disk_query_status, &disk_stat, &size);
  CHECK(status == peripheral_error);
  CHECK(size == sizeof(DiskStatus_t));

  // Full query with disk_query_status
  size = sizeof(DiskStatus_t);
  status = descriptor->query(instance, disk_query_status, &disk_stat, &size);
  CHECK(status == peripheral_ok);
  CHECK(size == sizeof(DiskStatus_t));

  // Sizing probe for disk_query_supported_extensions answers the list's own
  // length with its NUL, not a fixed ceiling.
  size = 0;
  status = descriptor->query(instance, disk_query_supported_extensions, nullptr,
                             &size);
  CHECK(status == peripheral_ok);
  CHECK(size == strlen("do;dsk;iie;nb2;nib;po;woz;gz;zip") + 1);

  // Query with disk_query_supported_extensions
  char exts[256] = {};
  size = sizeof(exts);
  status =
      descriptor->query(instance, disk_query_supported_extensions, exts, &size);
  CHECK(status == peripheral_ok);
  CHECK(strstr(exts, "dsk") != nullptr);

  // Unknown query returns peripheral_incompatible
  status = descriptor->query(instance, 0xFFFF, nullptr, &size);
  CHECK(status == peripheral_incompatible);

  // Unknown command returns peripheral_incompatible
  status = descriptor->command(instance, 0xFFFF, nullptr, 0);
  CHECK(status == peripheral_incompatible);

  descriptor->shutdown(instance);
}

TEST_CASE("DiskABI: [ABI-13] Host Interface Null Callbacks Defensive Guards") {
  auto* descriptor = disk_get_descriptor();
  REQUIRE(descriptor != nullptr);

  // Null host
  CHECK(descriptor->init(SL6, nullptr) == nullptr);

  HostInterface_t h{};
  // Missing RegisterIO
  CHECK(descriptor->init(SL6, &h) == nullptr);

  h.RegisterIO = [](int, PeripheralIOHandler, PeripheralIOHandler,
                    PeripheralIOHandler, PeripheralIOHandler) {};
#if ENABLE_ROM_DISK2
  // Missing RegisterCxROM
  CHECK(descriptor->init(SL6, &h) == nullptr);

  h.RegisterCxROM = [](int, const uint8_t*) {};
#endif
  // Valid host with minimal callbacks
  void* inst = descriptor->init(SL6, &h);
  CHECK(inst != nullptr);
  descriptor->shutdown(inst);
}

TEST_CASE("DiskABI: [ABI-14] A host with no floating bus reads back 0xFF") {
  auto* descriptor = disk_get_descriptor();
  REQUIRE(descriptor != nullptr);

  g_captured_disk_read = nullptr;
  HostInterface_t host = capturing_disk_host();
  host.ReadFloatingBus = nullptr;

  void* instance = descriptor->init(SL6, &host);
  REQUIRE(instance != nullptr);
  REQUIRE(g_captured_disk_read != nullptr);

  // A card whose host cannot say what the bus holds is not on a bus, and an
  // undriven bus pulls high. Only the odd offsets leave the bus undriven; the
  // even ones answer with the card's own data register, which powers up clear.
  CHECK(g_captured_disk_read(instance, 0, 0xE1, 0, 0, 0) == 0xFF);
  CHECK(g_captured_disk_read(instance, 0, 0xE9, 0, 0, 0) == 0xFF);
  CHECK(g_captured_disk_read(instance, 0, 0xEB, 0, 0, 0) == 0xFF);
  CHECK(g_captured_disk_read(instance, 0, 0xEF, 0, 0, 0) == 0xFF);
  CHECK(g_captured_disk_read(nullptr, 0, 0xE0, 0, 0, 0) == 0xFF);

  CHECK(g_captured_disk_read(instance, 0, 0xE0, 0, 0, 0) == 0x00);
  CHECK(g_captured_disk_read(instance, 0, 0xE8, 0, 0, 0) == 0x00);
  CHECK(g_captured_disk_read(instance, 0, 0xEA, 0, 0, 0) == 0x00);

  descriptor->shutdown(instance);
}
