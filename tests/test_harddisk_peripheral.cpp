// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <ios>

#include "Peripheral_Types.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "apple2/Memory.h"
#include "apple2/peripherals/harddisk/Harddisk.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/HarddiskLoader.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "doctest.h"
#include "test_fixtures.h"

extern "C" auto harddisk_get_descriptor() -> Peripheral_t*;

TEST_CASE("Harddisk Peripheral: Lifecycle and Registration") {
  linapple_init();
  peripheral_manager_init();

  // Register Harddisk in Slot 7
  int result = peripheral_register(harddisk_get_descriptor(), 7);
  CHECK(result == 0);

  // Verify I/O mapping for $C0F0
  // We expect some value from the harddisk IO handler.
  // By default, without an image, it should return DEVICE_OK (0x00) for most
  // reads.
  uint8_t val = io_map_dispatch(0, 0xC0F2, 0, 0, 0);  // Read Command register
  CHECK(val == 0);

  linapple_shutdown();
}

TEST_CASE(
    "Harddisk Peripheral: E2E Block Read/Write Round-Trip and Persistence") {
  linapple_init();
  peripheral_manager_init();

  int reg_result = peripheral_register(harddisk_get_descriptor(), 7);
  CHECK(reg_result == 0);

  // 1. Create an ephemeral 32MB .hdv file
  constexpr size_t hdv_size = static_cast<size_t>(32) * 1024 * 1024;
  auto fixture =
      TestFixtures::create_ephemeral_blank("prodos_32mb.hdv", hdv_size);

  // 2. Insert it into drive 0 via harddisk_cmd_insert (write_protected = 0)
  HarddiskInsertCmd_t insert{};
  insert.drive = 0;
  insert.write_protected = 0;
  insert.create_if_necessary = 0;
  strncpy(insert.path, fixture.c_str(), sizeof(insert.path) - 1);

  PeripheralStatus_t pstatus =
      peripheral_command(7, harddisk_cmd_insert, &insert, sizeof(insert));
  CHECK(pstatus == peripheral_ok);

  peripheral_manager_think(0);

  HarddiskStatus_t status{};
  size_t size = sizeof(status);
  pstatus = peripheral_query(7, harddisk_cmd_get_status, &status, &size);
  CHECK(pstatus == peripheral_ok);
  CHECK(status.drive0_loaded == 1);
  CHECK(status.drive0_last_error == 0);

  // 3. Write a test block with known distinct payload data via direct I/O
  constexpr uint32_t target_block = 2;
  constexpr size_t k_block_size = 512;
  std::array<uint8_t, k_block_size> payload{};
  for (size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<uint8_t>((i * 7 + 0x5A) & 0xFF);
  }

  // Populate memory block at $0000 to match payload for DMA emulation
  std::copy(payload.begin(), payload.end(), mem);

  // Set unit ($C0F3) = 0
  io_map_dispatch(0, 0xC0F3, 1, 0x00, 0);
  // Set memblock ($C0F4, $C0F5) = 0
  io_map_dispatch(0, 0xC0F4, 1, 0x00, 0);
  io_map_dispatch(0, 0xC0F5, 1, 0x00, 0);
  // Set diskblock ($C0F6, $C0F7) = target block
  io_map_dispatch(0, 0xC0F6, 1, static_cast<uint8_t>(target_block & 0xFF), 0);
  io_map_dispatch(0, 0xC0F7, 1,
                  static_cast<uint8_t>((target_block >> 8) & 0xFF), 0);
  // Write 512 bytes sequentially to data buffer ($C0F8)
  for (size_t i = 0; i < payload.size(); ++i) {
    io_map_dispatch(0, 0xC0F8, 1, payload[i], 0);
  }
  // Write command ($C0F2) = 0x02 (write command)
  io_map_dispatch(0, 0xC0F2, 1, 0x02, 0);
  // Read $C0F0 (execute command), check return is 0 (status::ok) and error
  // register $C0F1 is 0
  uint8_t write_res = io_map_dispatch(0, 0xC0F0, 0, 0, 0);
  CHECK(write_res == 0);
  uint8_t write_err = io_map_dispatch(0, 0xC0F1, 0, 0, 0);
  CHECK(write_err == 0);

  // Clear memory buffer before read back to guarantee direct I/O read isolation
  std::fill_n(mem, k_block_size, 0);

  // 4. Read the block back via direct I/O
  // Set unit ($C0F3) = 0
  io_map_dispatch(0, 0xC0F3, 1, 0x00, 0);
  // Set memblock ($C0F4, $C0F5) = 0
  io_map_dispatch(0, 0xC0F4, 1, 0x00, 0);
  io_map_dispatch(0, 0xC0F5, 1, 0x00, 0);
  // Set diskblock ($C0F6, $C0F7) = target block
  io_map_dispatch(0, 0xC0F6, 1, static_cast<uint8_t>(target_block & 0xFF), 0);
  io_map_dispatch(0, 0xC0F7, 1,
                  static_cast<uint8_t>((target_block >> 8) & 0xFF), 0);
  // Write command ($C0F2) = 0x01 (read command)
  io_map_dispatch(0, 0xC0F2, 1, 0x01, 0);
  // Read $C0F0 (execute command), check return is 0 and error register $C0F1 is
  // 0
  uint8_t read_res = io_map_dispatch(0, 0xC0F0, 0, 0, 0);
  CHECK(read_res == 0);
  uint8_t read_err = io_map_dispatch(0, 0xC0F1, 0, 0, 0);
  CHECK(read_err == 0);
  // Read 512 bytes sequentially from buffer register ($C0F8) and verify all 512
  // bytes match
  std::array<uint8_t, k_block_size> read_buf{};
  for (size_t i = 0; i < read_buf.size(); ++i) {
    read_buf[i] = io_map_dispatch(0, 0xC0F8, 0, 0, 0);
  }
  CHECK(memcmp(read_buf.data(), payload.data(), k_block_size) == 0);

  // 5. Directly inspect the underlying ephemeral file on the filesystem using
  // std::ifstream
  {
    std::ifstream disk_file(fixture.path(), std::ios::binary);
    REQUIRE(disk_file.is_open());
    disk_file.seekg(static_cast<std::streamoff>(target_block * k_block_size));
    REQUIRE(disk_file.good());
    std::array<uint8_t, k_block_size> file_bytes{};
    disk_file.read(reinterpret_cast<char*>(file_bytes.data()),
                   static_cast<std::streamsize>(file_bytes.size()));
    CHECK(disk_file.gcount() == static_cast<std::streamsize>(k_block_size));
    CHECK(memcmp(file_bytes.data(), payload.data(), k_block_size) == 0);
  }

  // 6. Eject the disk and verify cleanly
  HarddiskEjectCmd_t eject{};
  eject.drive = 0;
  pstatus = peripheral_command(7, harddisk_cmd_eject, &eject, sizeof(eject));
  CHECK(pstatus == peripheral_ok);
  peripheral_manager_think(0);

  size = sizeof(status);
  pstatus = peripheral_query(7, harddisk_cmd_get_status, &status, &size);
  CHECK(pstatus == peripheral_ok);
  CHECK(status.drive0_loaded == 0);
  CHECK(status.drive0_last_error == 0);

  linapple_shutdown();
}

TEST_CASE("Harddisk Peripheral: Commands and Queries") {
  linapple_init();
  peripheral_manager_init();
  peripheral_register(harddisk_get_descriptor(), 7);

  // 1. Check initial status
  HarddiskStatus_t status;
  size_t size = sizeof(status);
  PeripheralStatus_t pstatus =
      peripheral_query(7, harddisk_cmd_get_status, &status, &size);
  CHECK(pstatus == peripheral_ok);
  CHECK(status.drive0_loaded == 0);
  CHECK(status.drive1_loaded == 0);

  // 2. Test INSERT command (with a non-existent file to check error reporting)
  HarddiskInsertCmd_t insert;
  memset(&insert, 0, sizeof(insert));
  insert.drive = 0;
  strncpy(insert.path, "non_existent_image.hdv", sizeof(insert.path) - 1);

  pstatus = peripheral_command(7, harddisk_cmd_insert, &insert, sizeof(insert));
  CHECK(pstatus == peripheral_ok);

  // Commands are processed during Think
  peripheral_manager_think(0);

  // 3. Verify error in status
  size = sizeof(status);
  pstatus = peripheral_query(7, harddisk_cmd_get_status, &status, &size);
  CHECK(pstatus == peripheral_ok);
  CHECK(status.drive0_loaded == 0);
  CHECK(status.drive0_last_error != 0);  // harddisk_err_not_found

  // 4. Test EJECT command
  HarddiskEjectCmd_t eject;
  eject.drive = 0;
  pstatus = peripheral_command(7, harddisk_cmd_eject, &eject, sizeof(eject));
  CHECK(pstatus == peripheral_ok);
  peripheral_manager_think(0);

  size = sizeof(status);
  pstatus = peripheral_query(7, harddisk_cmd_get_status, &status, &size);
  CHECK(status.drive0_last_error == 0);  // Reset after eject/cleanup

  linapple_shutdown();
}

TEST_CASE("Harddisk Peripheral: Direct I/O Logic") {
  linapple_init();
  peripheral_manager_init();
  peripheral_register(harddisk_get_descriptor(), 7);

  // Write Unit Number to $C0F3
  io_map_dispatch(0, 0xC0F3, 1, 0x80, 0);  // Drive 2

  // Read it back
  uint8_t val = io_map_dispatch(0, 0xC0F3, 0, 0, 0);
  CHECK(val == 0x80);

  // Write Command to $C0F2
  io_map_dispatch(0, 0xC0F2, 1, 0x01, 0);  // Read command
  val = io_map_dispatch(0, 0xC0F2, 0, 0, 0);
  CHECK(val == 0x01);

  linapple_shutdown();
}

TEST_CASE("Harddisk Peripheral: Get Supported Extensions Query") {
  linapple_init();
  peripheral_manager_init();
  peripheral_register(harddisk_get_descriptor(), 7);

  char exts[256] = {};
  size_t size = sizeof(exts);
  PeripheralStatus_t pstatus =
      peripheral_query(7, harddisk_cmd_get_supported_extensions, exts, &size);
  CHECK(pstatus == peripheral_ok);
  CHECK(strstr(exts, "hdv") != nullptr);
  CHECK(strstr(exts, "po") != nullptr);
  CHECK(strstr(exts, "2mg") != nullptr);
  CHECK(strstr(exts, "2img") != nullptr);
  CHECK(strstr(exts, "2meg") != nullptr);
  CHECK(strstr(exts, "img") != nullptr);
  CHECK(strstr(exts, "bin") != nullptr);
  CHECK(strstr(exts, "zip") != nullptr);
  CHECK(strstr(exts, "gz") != nullptr);

  linapple_shutdown();
}

TEST_CASE(
    "Harddisk Peripheral: Enforce write capability and callback invariants on "
    "registration") {
  harddisk_loader_init();

  // Harddisk driver: write capability with null write_block must be rejected
  HarddiskFormatDriver_t bad_hd1{};
  bad_hd1.capabilities = harddisk_driver_cap_write;
  bad_hd1.write_block = nullptr;
  harddisk_loader_register(&bad_hd1);

  // Harddisk driver: read-only capability with non-null write_block must be
  // rejected
  HarddiskFormatDriver_t bad_hd2{};
  bad_hd2.capabilities = 0;
  bad_hd2.write_block = [](void*, uint32_t, const uint8_t*) -> HarddiskError_e {
    return harddisk_err_none;
  };
  harddisk_loader_register(&bad_hd2);

  // Harddisk driver: missing probe/open/close must be rejected
  HarddiskFormatDriver_t bad_hd3{};
  bad_hd3.capabilities = 0;
  bad_hd3.probe = nullptr;
  bad_hd3.open = nullptr;
  bad_hd3.close = nullptr;
  harddisk_loader_register(&bad_hd3);

  // Null pointer registrations are safely ignored
  harddisk_loader_register(nullptr);
}

static HostInterface_t g_test_hd_host = [] {
  HostInterface_t h{};
  h.RegisterIO = [](int, PeripheralIOHandler, PeripheralIOHandler,
                    PeripheralIOHandler, PeripheralIOHandler) {};
  h.RegisterCxROM = [](int, uint8_t*) {};
  h.RegisterDirectIO = [](void*, uint16_t, PeripheralIOHandler,
                          PeripheralIOHandler) {};
  h.GetConfig = [](const char*, const char*, char*, size_t) { return false; };
  h.SetConfig = [](const char*, const char*, const char*) {};
  h.NotifyStatusChanged = [](int) {};
  return h;
}();

TEST_CASE(
    "Harddisk Peripheral: [HARDDISK-11] Insert Command NUL Terminator Check") {
  auto* descriptor = harddisk_get_descriptor();
  REQUIRE(descriptor != nullptr);
  void* instance = descriptor->init(7, &g_test_hd_host);
  REQUIRE(instance != nullptr);

  HarddiskInsertCmd_t cmd;
  memset(&cmd, 'B', sizeof(cmd));  // No NUL terminator
  cmd.drive = 0;
  cmd.write_protected = 0;
  cmd.create_if_necessary = 0;

  PeripheralStatus_t status =
      descriptor->command(instance, harddisk_cmd_insert, &cmd, sizeof(cmd));
  CHECK(status == peripheral_error);

  descriptor->shutdown(instance);
}
