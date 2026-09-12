// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>

#include "Peripheral_Types.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/disk/DiskLoader.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "doctest.h"

namespace {
constexpr size_t DISK_ABI_CMD_SIZE = 512;
constexpr int SL6 = 6;
constexpr uint8_t BUFFER_INIT_VAL = 0xAA;
constexpr uint32_t BAD_VERSION = 0xdeadbeef;
}  // namespace

TEST_CASE("DiskABI: [DISK-01] DiskInsertCmd_t is exactly 512 bytes") {
  CHECK(sizeof(DiskInsertCmd_t) == DISK_ABI_CMD_SIZE);
}

TEST_CASE("DiskABI: [DISK-02] DiskInsertCmd_t field offsets are stable") {
  CHECK(offsetof(DiskInsertCmd_t, path) == 0);
  CHECK(offsetof(DiskInsertCmd_t, drive) == 504);
  CHECK(offsetof(DiskInsertCmd_t, write_protected) == 505);
  CHECK(offsetof(DiskInsertCmd_t, create_if_necessary) == 506);
}

TEST_CASE("DiskABI: [DISK-03] Enum values match ABI specification") {
  CHECK(disk_drive_0 == 0);
  CHECK(disk_drive_1 == 1);
  CHECK(disk_cmd_insert == 0x01);
  CHECK(disk_cmd_eject == 0x02);
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

static HostInterface_t g_test_disk_host = [] {
  HostInterface_t h{};
  h.RegisterIO = [](int, PeripheralIOHandler, PeripheralIOHandler,
                    PeripheralIOHandler, PeripheralIOHandler) {};
  h.RegisterCxROM = [](int, uint8_t*) {};
  h.GetConfig = [](const char*, const char*, char*, size_t) { return false; };
  h.SetConfig = [](const char*, const char*, const char*) {};
  h.NotifyStatusChanged = [](int) {};
  return h;
}();

TEST_CASE("DiskABI: [ABI-07] SaveState Size Query") {
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
  CHECK(offsetof(DiskDriveState_t, is_os_read_only) == 269);
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
  CHECK(offsetof(DiskSavedState_t, was_accessed_this_tick) == 13892);
  CHECK(offsetof(DiskSavedState_t, is_speed_enhanced) == 13893);
  CHECK(offsetof(DiskSavedState_t, io_latch) == 13894);
  CHECK(offsetof(DiskSavedState_t, is_motor_on) == 13895);
  CHECK(offsetof(DiskSavedState_t, is_write_mode) == 13896);
}

TEST_CASE("DiskABI: [ABI-08] SaveState Undersized Buffer") {
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
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();

  char exts[256] = {};
  size_t size = sizeof(exts);
  PeripheralStatus_t status =
      peripheral_query(SL6, disk_cmd_get_supported_extensions, exts, &size);
  CHECK(status == peripheral_ok);
  CHECK(strstr(exts, "do") != nullptr);
  CHECK(strstr(exts, "dsk") != nullptr);
  CHECK(strstr(exts, "po") != nullptr);
  CHECK(strstr(exts, "nib") != nullptr);
  CHECK(strstr(exts, "nb2") != nullptr);
  CHECK(strstr(exts, "woz") != nullptr);
  CHECK(strstr(exts, "iie") != nullptr);
  CHECK(strstr(exts, "zip") != nullptr);
  CHECK(strstr(exts, "gz") != nullptr);

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
  cmd.create_if_necessary = 0;

  PeripheralStatus_t status =
      descriptor->command(instance, disk_cmd_insert, &cmd, sizeof(cmd));
  CHECK(status == peripheral_error);

  descriptor->shutdown(instance);
}

TEST_CASE("DiskABI: [REG-15] DiskLoader registration validation") {
  disk_loader_init();

  // Null driver
  disk_loader_register(nullptr);

  // Missing probe/open/close
  DiskFormatDriver_t bad_drv1{};
  bad_drv1.capabilities = 0;
  bad_drv1.probe = nullptr;
  bad_drv1.open = nullptr;
  bad_drv1.close = nullptr;
  disk_loader_register(&bad_drv1);

  // Write cap mismatch (cap set, write_track null)
  DiskFormatDriver_t bad_drv2{};
  bad_drv2.probe = [](const uint8_t*, size_t, uint32_t, const char*) {
    return disk_probe_no;
  };
  bad_drv2.open = [](const char*, uint32_t, uint8_t, bool*, void**) {
    return disk_err_none;
  };
  bad_drv2.close = [](void*) {};
  bad_drv2.capabilities = disk_driver_cap_write;
  bad_drv2.write_track = nullptr;
  disk_loader_register(&bad_drv2);

  // Flux cap mismatch (cap set, read_flux_bit null)
  DiskFormatDriver_t bad_drv3{};
  bad_drv3.probe = bad_drv2.probe;
  bad_drv3.open = bad_drv2.open;
  bad_drv3.close = bad_drv2.close;
  bad_drv3.capabilities = disk_driver_cap_flux;
  bad_drv3.read_flux_bit = nullptr;
  disk_loader_register(&bad_drv3);

  disk_loader_shutdown();
}
