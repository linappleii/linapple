// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/peripherals/harddisk/Harddisk.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "doctest.h"
#include "test_fixtures.h"

extern "C" const HarddiskFormatDriver_t g_two_img_driver;

namespace {

struct HarddiskHarness {
  std::array<uint8_t, 65536> ram{};
  std::array<uint8_t, 256> cx_rom{};
  HostInterface_t host{};
  Peripheral_t* desc = nullptr;
  void* instance = nullptr;

  PeripheralIoHandler_t io_read = nullptr;
  PeripheralIoHandler_t io_write = nullptr;
  void* io_context = nullptr;

  static auto s_active_harness() -> HarddiskHarness*& {
    static HarddiskHarness* s_h = nullptr;
    return s_h;
  }

  static auto mock_register_direct_io(void* ctx, uint16_t addr,
                                      PeripheralIoHandler_t r,
                                      PeripheralIoHandler_t w) -> void {
    (void)addr;
    if (s_active_harness() != nullptr) {
      s_active_harness()->io_read = r;
      s_active_harness()->io_write = w;
      s_active_harness()->io_context = ctx;
    }
  }

  static auto mock_register_cx_rom(int slot, uint8_t* rom) -> void {
    (void)slot;
    if (s_active_harness() != nullptr && rom != nullptr) {
      std::copy_n(rom, 256, s_active_harness()->cx_rom.begin());
    }
  }

  static auto mock_get_mem_ptr(uint16_t addr) -> uint8_t* {
    if (s_active_harness() != nullptr) {
      return &s_active_harness()->ram.at(addr);
    }
    return nullptr;
  }

  explicit HarddiskHarness(int slot = 7) {
    s_active_harness() = this;
    host.RegisterDirectIO = mock_register_direct_io;
    host.RegisterCxROM = mock_register_cx_rom;
    host.get_mem_ptr = mock_get_mem_ptr;
    host.NotifyStatusChanged = [](int) {};
    host.GetConfig = [](const char*, const char*, char*, size_t) {
      return false;
    };
    host.SetConfig = [](const char*, const char*, const char*) {};

    desc = harddisk_get_descriptor();
    if (desc != nullptr && desc->init != nullptr) {
      instance = desc->init(slot, &host);
    }
  }

  ~HarddiskHarness() {
    if (desc != nullptr && desc->shutdown != nullptr && instance != nullptr) {
      desc->shutdown(instance);
      instance = nullptr;
    }
    s_active_harness() = nullptr;
  }

  HarddiskHarness(const HarddiskHarness&) = delete;
  auto operator=(const HarddiskHarness&) -> HarddiskHarness& = delete;
  HarddiskHarness(HarddiskHarness&&) = delete;
  auto operator=(HarddiskHarness&&) -> HarddiskHarness& = delete;

  auto read_io(uint16_t addr) -> uint8_t {
    if (io_read != nullptr && instance != nullptr) {
      return io_read(io_context, 0, addr, 0, 0, 0);
    }
    return 0;
  }

  auto write_io(uint16_t addr, uint8_t val) -> void {
    if (io_write != nullptr && instance != nullptr) {
      io_write(io_context, 0, addr, 1, val, 0);
    }
  }
};

}  // namespace

TEST_CASE("Harddisk Exhaustive: Register Protocol and Block I/O") {
  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  // 1. Create a mock 1MB HDV file (2048 blocks)
  std::vector<uint8_t> mock_data(2048 * 512, 0);
  for (int i = 0; i < 512; ++i) {
    mock_data[10 * 512 + i] = static_cast<uint8_t>(i & 0xFF);
  }

  auto fixture = TestFixtures::create_ephemeral_blank("exhaustive_test.hdv",
                                                      mock_data.size());
  {
    std::ofstream out(fixture.c_str(), std::ios::binary);
    out.write(reinterpret_cast<const char*>(mock_data.data()),
              mock_data.size());
  }

  // 2. Insert mock disk
  HarddiskInsertCmd_t insert{};
  insert.drive = harddisk_drive_0;
  strncpy(insert.path, fixture.c_str(), sizeof(insert.path) - 1);

  PeripheralStatus_t pstatus = harness.desc->command(
      harness.instance, harddisk_cmd_insert, &insert, sizeof(insert));
  CHECK(pstatus == peripheral_ok);

  HarddiskStatus_t status{};
  size_t status_size = sizeof(status);
  pstatus = harness.desc->query(harness.instance, harddisk_query_status,
                                &status, &status_size);
  CHECK(pstatus == peripheral_ok);
  CHECK(status.drive0_loaded == 1);
  CHECK(status.drive0_last_error == 0);
  CHECK(strcmp(status.drive0_full_path, fixture.c_str()) == 0);

  // 3. Read block 10 via SmartPort register protocol
  harness.write_io(0xC0F3, 0x00);  // Drive 0
  harness.write_io(0xC0F2, 0x01);  // Read
  harness.write_io(0xC0F4, 0x00);  // Mem $2000
  harness.write_io(0xC0F5, 0x20);
  harness.write_io(0xC0F6, 0x0A);  // Block 10
  harness.write_io(0xC0F7, 0x00);

  uint8_t io_res = harness.read_io(0xC0F0);  // Exec
  CHECK(io_res == 0);                        // ok

  // Verify buffer bytes
  uint8_t b0 = harness.read_io(0xC0F8);
  CHECK(b0 == 0x00);
  uint8_t b1 = harness.read_io(0xC0F8);
  CHECK(b1 == 0x01);

  // 4. Write Protocol: write to memory $3000 and then block 20
  std::fill_n(&harness.ram.at(0x3000), 512, 0xAA);

  harness.write_io(0xC0F2, 0x02);  // Write
  harness.write_io(0xC0F4, 0x00);  // Mem $3000
  harness.write_io(0xC0F5, 0x30);
  harness.write_io(0xC0F6, 0x14);  // Block 20
  harness.write_io(0xC0F7, 0x00);

  io_res = harness.read_io(0xC0F0);  // Exec
  CHECK(io_res == 0);

  // 5. Write protection toggle
  HarddiskSetProtectCmd_t prot{};
  prot.drive = harddisk_drive_0;
  prot.write_protected = 1;
  pstatus = harness.desc->command(harness.instance, harddisk_cmd_set_protect,
                                  &prot, sizeof(prot));
  CHECK(pstatus == peripheral_ok);

  // Try writing again - should fail with io_error
  io_res = harness.read_io(0xC0F0);
  CHECK(io_res != 0);

  // 6. Eject
  HarddiskEjectCmd_t eject{};
  eject.drive = harddisk_drive_0;
  pstatus = harness.desc->command(harness.instance, harddisk_cmd_eject, &eject,
                                  sizeof(eject));
  CHECK(pstatus == peripheral_ok);
}

TEST_CASE("Harddisk Exhaustive: Edge Cases and Safety") {
  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  // 1. Invalid Drive Index in Command
  HarddiskEjectCmd_t eject{};
  eject.drive = 99;
  PeripheralStatus_t pstatus = harness.desc->command(
      harness.instance, harddisk_cmd_eject, &eject, sizeof(eject));
  CHECK(pstatus == peripheral_error);

  // 2. Read from unloaded drive
  harness.write_io(0xC0F3, 0x00);  // Drive 0 (unloaded)
  harness.write_io(0xC0F2, 0x01);  // Read
  uint8_t io_res = harness.read_io(0xC0F0);
  CHECK(io_res != 0);  // error

  // 3. Memory Bounds Safety (near 64K boundary)
  auto fixture = TestFixtures::create_ephemeral_blank("safety.hdv", 512);

  HarddiskInsertCmd_t insert{};
  insert.drive = harddisk_drive_0;
  strncpy(insert.path, fixture.c_str(), sizeof(insert.path) - 1);
  CHECK(harness.desc->command(harness.instance, harddisk_cmd_insert, &insert,
                              sizeof(insert)) == peripheral_ok);

  harness.write_io(0xC0F4,
                   0xF0);  // $FFF0 (reading 512 bytes would overflow 64K)
  harness.write_io(0xC0F5, 0xFF);
  harness.write_io(0xC0F2, 0x02);  // Write
  io_res = harness.read_io(0xC0F0);
  CHECK(io_res != 0);
}

TEST_CASE("Harddisk Exhaustive: MacBinary Detection") {
  std::array<uint8_t, 128 + 512> macbin_data{};
  macbin_data[0] = 0;
  macbin_data[1] = 10;
  memcpy(&macbin_data[2], "test.hdv  ", 10);
  macbin_data[122] = 0;
  macbin_data[123] = 0;
  macbin_data[128] = 0x55;

  auto fixture = TestFixtures::create_ephemeral_blank("test_macbin.hdv",
                                                      macbin_data.size());
  {
    std::ofstream out(fixture.c_str(), std::ios::binary);
    out.write(reinterpret_cast<const char*>(macbin_data.data()),
              macbin_data.size());
  }

  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  HarddiskInsertCmd_t insert{};
  insert.drive = harddisk_drive_0;
  strncpy(insert.path, fixture.c_str(), sizeof(insert.path) - 1);
  CHECK(harness.desc->command(harness.instance, harddisk_cmd_insert, &insert,
                              sizeof(insert)) == peripheral_ok);

  // Read block 0
  harness.write_io(0xC0F3, 0x00);
  harness.write_io(0xC0F2, 0x01);
  harness.write_io(0xC0F6, 0x00);
  harness.write_io(0xC0F7, 0x00);
  harness.read_io(0xC0F0);

  uint8_t b0 = harness.read_io(0xC0F8);
  CHECK(b0 == 0x55);  // Should have skipped 128-byte MacBinary header
}

TEST_CASE("Harddisk Exhaustive: Native 2MG Container Support") {
  std::vector<uint8_t> two_mg_data(64 + 2 * 512, 0);
  memcpy(&two_mg_data[0], "2IMG", 4);
  memcpy(&two_mg_data[4], "2mgx", 4);
  uint16_t header_len = 64;
  memcpy(&two_mg_data[8], &header_len, 2);
  uint16_t version = 1;
  memcpy(&two_mg_data[10], &version, 2);
  uint32_t image_format = 1;
  memcpy(&two_mg_data[12], &image_format, 4);
  uint32_t flags = 0;
  memcpy(&two_mg_data[16], &flags, 4);
  uint32_t blocks = 2;
  memcpy(&two_mg_data[20], &blocks, 4);
  uint32_t data_offset = 64;
  memcpy(&two_mg_data[24], &data_offset, 4);
  uint32_t data_len = 1024;
  memcpy(&two_mg_data[28], &data_len, 4);

  two_mg_data[64] = 0xAA;
  two_mg_data[64 + 512] = 0xBB;

  auto fixture = TestFixtures::create_ephemeral_blank("total_replay.2mg",
                                                      two_mg_data.size());
  {
    std::ofstream out(fixture.c_str(), std::ios::binary);
    out.write(reinterpret_cast<const char*>(two_mg_data.data()),
              two_mg_data.size());
  }

  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  HarddiskInsertCmd_t insert{};
  insert.drive = harddisk_drive_0;
  strncpy(insert.path, fixture.c_str(), sizeof(insert.path) - 1);
  CHECK(harness.desc->command(harness.instance, harddisk_cmd_insert, &insert,
                              sizeof(insert)) == peripheral_ok);

  HarddiskStatus_t status{};
  size_t status_size = sizeof(status);
  CHECK(harness.desc->query(harness.instance, harddisk_query_status, &status,
                            &status_size) == peripheral_ok);
  CHECK(status.drive0_loaded == 1);
  CHECK(status.drive0_last_error == 0);

  // Read Block 0
  harness.write_io(0xC0F3, 0x00);
  harness.write_io(0xC0F2, 0x01);
  harness.write_io(0xC0F6, 0x00);
  harness.write_io(0xC0F7, 0x00);
  harness.read_io(0xC0F0);

  uint8_t byte0 = harness.read_io(0xC0F8);
  CHECK(byte0 == 0xAA);

  // Read Block 1
  harness.write_io(0xC0F6, 0x01);
  harness.write_io(0xC0F7, 0x00);
  harness.read_io(0xC0F0);

  uint8_t byte1 = harness.read_io(0xC0F8);
  CHECK(byte1 == 0xBB);
}

TEST_CASE(
    "Harddisk Exhaustive: Reject corrupted 2MG data offset beyond file "
    "bounds") {
  std::vector<uint8_t> corrupted_2mg(128, 0);
  corrupted_2mg[0] = '2';
  corrupted_2mg[1] = 'I';
  corrupted_2mg[2] = 'M';
  corrupted_2mg[3] = 'G';
  corrupted_2mg[12] = 1;
  uint32_t bad_offset = 0x100000;
  memcpy(&corrupted_2mg[24], &bad_offset, sizeof(bad_offset));

  auto fixture = TestFixtures::create_ephemeral_blank("corrupt_offset.2mg",
                                                      corrupted_2mg.size());
  {
    std::ofstream out(fixture.c_str(), std::ios::binary);
    out.write(reinterpret_cast<const char*>(corrupted_2mg.data()),
              corrupted_2mg.size());
  }

  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  HarddiskInsertCmd_t insert{};
  insert.drive = harddisk_drive_0;
  strncpy(insert.path, fixture.c_str(), sizeof(insert.path) - 1);
  harness.desc->command(harness.instance, harddisk_cmd_insert, &insert,
                        sizeof(insert));

  HarddiskStatus_t status{};
  size_t status_size = sizeof(status);
  harness.desc->query(harness.instance, harddisk_query_status, &status,
                      &status_size);
  CHECK(status.drive0_loaded == 0);
}

TEST_CASE(
    "Harddisk Exhaustive: Safe Handling of Unbounded SmartPort Buffer I/O "
    "($C0F8)") {
  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  // Unbounded reads from $C0F8 without reading a block
  for (int i = 0; i < 600; ++i) {
    uint8_t val = harness.read_io(0xC0F8);
    if (i >= 512) {
      CHECK(val == 0x00);
    }
  }

  // Unbounded writes to $C0F8
  for (int i = 0; i < 600; ++i) {
    harness.write_io(0xC0F8, static_cast<uint8_t>(i & 0xFF));
  }

  // Unbounded reads after a valid block read
  std::vector<uint8_t> mock_data(512 * 2, 0);
  for (int i = 0; i < 512; ++i) {
    mock_data[i] = static_cast<uint8_t>(i & 0xFF);
  }
  auto fixture = TestFixtures::create_ephemeral_blank("unbounded_test.hdv",
                                                      mock_data.size());
  {
    std::ofstream out(fixture.c_str(), std::ios::binary);
    out.write(reinterpret_cast<const char*>(mock_data.data()),
              mock_data.size());
  }

  HarddiskInsertCmd_t insert{};
  insert.drive = harddisk_drive_0;
  strncpy(insert.path, fixture.c_str(), sizeof(insert.path) - 1);
  CHECK(harness.desc->command(harness.instance, harddisk_cmd_insert, &insert,
                              sizeof(insert)) == peripheral_ok);

  // Read Block 0
  harness.write_io(0xC0F3, 0x00);
  harness.write_io(0xC0F2, 0x01);
  harness.write_io(0xC0F6, 0x00);
  harness.write_io(0xC0F7, 0x00);
  harness.read_io(0xC0F0);

  // Read 512 bytes
  for (int i = 0; i < 512; ++i) {
    uint8_t val = harness.read_io(0xC0F8);
    CHECK(val == static_cast<uint8_t>(i & 0xFF));
  }

  // Read beyond 512 bytes
  for (int i = 512; i < 1024; ++i) {
    uint8_t val = harness.read_io(0xC0F8);
    CHECK(val == 0x00);
  }
}

TEST_CASE("Harddisk Exhaustive: 2MG Offset validation and integer wrap") {
  std::vector<uint8_t> bad_2mg(128, 0);
  memcpy(bad_2mg.data(), "2IMG", 4);
  uint32_t big_offset = 0xFFFFFF00;
  memcpy(&bad_2mg[24], &big_offset, sizeof(big_offset));

  auto fixture =
      TestFixtures::create_ephemeral_blank("bad_offset.2mg", bad_2mg.size());
  {
    std::ofstream out(fixture.c_str(), std::ios::binary);
    out.write(reinterpret_cast<const char*>(bad_2mg.data()), bad_2mg.size());
  }

  bool is_readonly = false;
  void* instance = nullptr;
  HarddiskError_e err =
      g_two_img_driver.open(fixture.c_str(), 0, &is_readonly, &instance);
  CHECK(err == harddisk_err_invalid_format);
  CHECK(instance == nullptr);
}
