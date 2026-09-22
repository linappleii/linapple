// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/peripherals/harddisk/Harddisk.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "doctest.h"
#include "test_fixtures.h"

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

TEST_CASE("HD-01: Descriptor Identity and Interface Verification") {
  auto* desc = harddisk_get_descriptor();
  REQUIRE(desc != nullptr);
  CHECK(desc->abi_version == LINAPPLE_ABI_VERSION);
  CHECK(strcmp(desc->id, "linapple.harddisk") == 0);
  CHECK(desc->default_slot == 7);
  CHECK(desc->compatible_slots == PERIPHERAL_MASK_EXPANSION);
  CHECK(desc->init != nullptr);
  CHECK(desc->reset != nullptr);
  CHECK(desc->shutdown != nullptr);
  CHECK(desc->save_state != nullptr);
  CHECK(desc->load_state != nullptr);
  CHECK(desc->command != nullptr);
  CHECK(desc->query != nullptr);
}

TEST_CASE("HD-02: Null Host and Missing Callback Defensive Guards") {
  auto* desc = harddisk_get_descriptor();
  REQUIRE(desc != nullptr);

  // Null host
  CHECK(desc->init(7, nullptr) == nullptr);

  // Missing RegisterDirectIO
  HostInterface_t h{};
  h.RegisterCxROM = [](int, uint8_t*) {};
  h.get_mem_ptr = [](uint16_t) -> uint8_t* { return nullptr; };
  CHECK(desc->init(7, &h) == nullptr);

  // Missing RegisterCxROM
  h.RegisterDirectIO = [](void*, uint16_t, PeripheralIoHandler_t,
                          PeripheralIoHandler_t) {};
  h.RegisterCxROM = nullptr;
  CHECK(desc->init(7, &h) == nullptr);

  // Missing get_mem_ptr
  h.RegisterCxROM = [](int, uint8_t*) {};
  h.get_mem_ptr = nullptr;
  CHECK(desc->init(7, &h) == nullptr);
}

TEST_CASE("HD-03: Slot ROM Registration and Content Verification") {
  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  // Verify SmartPort firmware signature in registered ROM: $20, $00, $03, $3C
  CHECK(harness.cx_rom[0] == 0xA9);
  CHECK(harness.cx_rom[1] == 0x20);
  CHECK(harness.cx_rom[2] == 0xA9);
  CHECK(harness.cx_rom[3] == 0x00);
}

TEST_CASE("HD-04: MMIO Register Read/Write Latches ($C0F0..$C0F8)") {
  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  // Unit Number ($C0F3)
  harness.write_io(0xC0F3, 0x80);
  CHECK(harness.read_io(0xC0F3) == 0x80);

  // Command Register ($C0F2)
  harness.write_io(0xC0F2, 0x01);
  CHECK(harness.read_io(0xC0F2) == 0x01);

  // Memory Address ($C0F4, $C0F5)
  harness.write_io(0xC0F4, 0x34);
  harness.write_io(0xC0F5, 0x12);
  CHECK(harness.read_io(0xC0F4) == 0x34);
  CHECK(harness.read_io(0xC0F5) == 0x12);

  // Disk Block ($C0F6, $C0F7)
  harness.write_io(0xC0F6, 0x78);
  harness.write_io(0xC0F7, 0x56);
  CHECK(harness.read_io(0xC0F6) == 0x78);
  CHECK(harness.read_io(0xC0F7) == 0x56);
}

TEST_CASE("HD-05: Image Insert and Eject ABI Commands") {
  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  auto fixture =
      TestFixtures::create_ephemeral_blank("test_hd_05.hdv", 1024 * 1024);

  HarddiskInsertCmd_t insert{};
  insert.drive = harddisk_drive_0;
  insert.write_protected = 0;
  insert.create_if_necessary = 0;
  strncpy(insert.path, fixture.c_str(), sizeof(insert.path) - 1);

  PeripheralStatus_t st = harness.desc->command(
      harness.instance, harddisk_cmd_insert, &insert, sizeof(insert));
  CHECK(st == peripheral_ok);

  HarddiskStatus_t status{};
  size_t size = sizeof(status);
  st = harness.desc->query(harness.instance, harddisk_query_status, &status,
                           &size);
  CHECK(st == peripheral_ok);
  CHECK(status.drive0_loaded == 1);
  CHECK(status.drive0_write_protected == 0);
  CHECK(strcmp(status.drive0_full_path, fixture.c_str()) == 0);

  // Eject
  HarddiskEjectCmd_t eject{};
  eject.drive = harddisk_drive_0;
  st = harness.desc->command(harness.instance, harddisk_cmd_eject, &eject,
                             sizeof(eject));
  CHECK(st == peripheral_ok);

  size = sizeof(status);
  st = harness.desc->query(harness.instance, harddisk_query_status, &status,
                           &size);
  CHECK(st == peripheral_ok);
  CHECK(status.drive0_loaded == 0);
}

TEST_CASE("HD-06: Write Protection Toggle and Enforcement") {
  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  auto fixture =
      TestFixtures::create_ephemeral_blank("test_hd_06.hdv", 1024 * 1024);

  HarddiskInsertCmd_t insert{};
  insert.drive = harddisk_drive_0;
  insert.write_protected = 0;
  strncpy(insert.path, fixture.c_str(), sizeof(insert.path) - 1);
  CHECK(harness.desc->command(harness.instance, harddisk_cmd_insert, &insert,
                              sizeof(insert)) == peripheral_ok);

  // Toggle protection on
  HarddiskSetProtectCmd_t protect{};
  protect.drive = harddisk_drive_0;
  protect.write_protected = 1;
  CHECK(harness.desc->command(harness.instance, harddisk_cmd_set_protect,
                              &protect, sizeof(protect)) == peripheral_ok);

  HarddiskStatus_t status{};
  size_t size = sizeof(status);
  CHECK(harness.desc->query(harness.instance, harddisk_query_status, &status,
                            &size) == peripheral_ok);
  CHECK(status.drive0_write_protected == 1);

  // Attempt block write (command 2) to protected drive
  harness.write_io(0xC0F3, 0x00);  // Drive 0
  harness.write_io(0xC0F2, 0x02);  // Write command
  harness.write_io(0xC0F4, 0x00);  // Memory $2000
  harness.write_io(0xC0F5, 0x20);
  harness.write_io(0xC0F6, 0x05);  // Block 5
  harness.write_io(0xC0F7, 0x00);

  uint8_t res = harness.read_io(0xC0F0);   // Execute
  CHECK(res == 0x08);                      // io_error
  CHECK(harness.read_io(0xC0F1) == 0x01);  // error_code
}

TEST_CASE("HD-07: DMA Block Read and Write Round-Trip (Commands 1 & 2)") {
  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  auto fixture =
      TestFixtures::create_ephemeral_blank("test_hd_07.hdv", 1024 * 1024);

  HarddiskInsertCmd_t insert{};
  insert.drive = harddisk_drive_0;
  insert.write_protected = 0;
  strncpy(insert.path, fixture.c_str(), sizeof(insert.path) - 1);
  CHECK(harness.desc->command(harness.instance, harddisk_cmd_insert, &insert,
                              sizeof(insert)) == peripheral_ok);

  // Populate mock RAM at $3000 with test payload
  std::array<uint8_t, 512> write_payload{};
  for (size_t i = 0; i < 512; ++i) {
    write_payload[i] = static_cast<uint8_t>((i * 13 + 0x42) & 0xFF);
  }
  std::copy(write_payload.begin(), write_payload.end(),
            &harness.ram.at(0x3000));

  // Write block 12 via SmartPort write command (0x02)
  harness.write_io(0xC0F3, 0x00);  // Drive 0
  harness.write_io(0xC0F2, 0x02);  // Write
  harness.write_io(0xC0F4, 0x00);  // Mem $3000
  harness.write_io(0xC0F5, 0x30);
  harness.write_io(0xC0F6, 0x0C);  // Block 12
  harness.write_io(0xC0F7, 0x00);

  uint8_t exec_status = harness.read_io(0xC0F0);
  CHECK(exec_status == 0x00);  // ok
  CHECK(harness.read_io(0xC0F1) == 0x00);

  // Clear memory buffer at $4000
  std::fill_n(&harness.ram.at(0x4000), 512, 0xAA);

  // Read block 12 into $C0F8 buffer via SmartPort read command (0x01)
  harness.write_io(0xC0F2, 0x01);  // Read
  harness.write_io(0xC0F6, 0x0C);  // Block 12
  harness.write_io(0xC0F7, 0x00);

  exec_status = harness.read_io(0xC0F0);
  CHECK(exec_status == 0x00);  // ok
  CHECK(harness.read_io(0xC0F1) == 0x00);

  // Read 512 bytes from buffer register ($C0F8)
  for (size_t i = 0; i < 512; ++i) {
    uint8_t b = harness.read_io(0xC0F8);
    CHECK(b == write_payload[i]);
  }
}

TEST_CASE("HD-11: Query Sizing Probe (Pass 1 and Pass 2)") {
  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  // Pass 1: data == nullptr should return peripheral_ok and set *size =
  // sizeof(HarddiskStatus_t)
  size_t size = 0;
  PeripheralStatus_t st = harness.desc->query(
      harness.instance, harddisk_query_status, nullptr, &size);
  CHECK(st == peripheral_ok);
  CHECK(size == sizeof(HarddiskStatus_t));

  // Undersized buffer should return peripheral_error
  size = sizeof(HarddiskStatus_t) - 1;
  HarddiskStatus_t status{};
  st = harness.desc->query(harness.instance, harddisk_query_status, &status,
                           &size);
  CHECK(st == peripheral_error);
  CHECK(size == sizeof(HarddiskStatus_t));

  // Pass 2: valid buffer
  size = sizeof(HarddiskStatus_t);
  st = harness.desc->query(harness.instance, harddisk_query_status, &status,
                           &size);
  CHECK(st == peripheral_ok);
}

TEST_CASE("HD-12: Query Supported Extensions") {
  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  // Pass 1: data == nullptr
  size_t size = 0;
  PeripheralStatus_t st = harness.desc->query(
      harness.instance, harddisk_query_supported_extensions, nullptr, &size);
  CHECK(st == peripheral_ok);
  CHECK(size == 256);

  // Pass 2: valid buffer
  char exts[256] = {};
  size = sizeof(exts);
  st = harness.desc->query(harness.instance,
                           harddisk_query_supported_extensions, exts, &size);
  CHECK(st == peripheral_ok);
  CHECK(strstr(exts, "hdv") != nullptr);
  CHECK(strstr(exts, "2mg") != nullptr);

  // The archive extensions come last, in the container library's own order,
  // so both cards offer the same list to a file browser.
  const char* const archive_tail = "gz;zip";
  const size_t len = strlen(exts);
  REQUIRE(len > strlen(archive_tail));
  CHECK(strcmp(exts + len - strlen(archive_tail), archive_tail) == 0);
}

TEST_CASE("HD-13: Unknown Command and Query Rejection") {
  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  // Unknown command
  CHECK(harness.desc->command(harness.instance, 0xFFFF, nullptr, 0) ==
        peripheral_incompatible);

  // Unknown query
  size_t size = 64;
  uint8_t buf[64] = {};
  CHECK(harness.desc->query(harness.instance, 0xFFFF, buf, &size) ==
        peripheral_incompatible);
}

TEST_CASE("HD-14: Deterministic Save State Sizing Probe") {
  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  // Pass 1 sizing probe
  size_t size = 0;
  PeripheralStatus_t st =
      harness.desc->save_state(harness.instance, nullptr, &size);
  CHECK(st == peripheral_ok);
  CHECK(size == sizeof(HarddiskSaveState_t));
  CHECK(size == 2160);

  // Undersized buffer
  size = sizeof(HarddiskSaveState_t) - 1;
  uint8_t buffer[2160] = {};
  st = harness.desc->save_state(harness.instance, buffer, &size);
  CHECK(st == peripheral_error);
  CHECK(size == sizeof(HarddiskSaveState_t));
}

TEST_CASE("HD-15: Save/Load State Round-Trip Fidelity") {
  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  auto fixture =
      TestFixtures::create_ephemeral_blank("test_hd_15.hdv", 1024 * 1024);

  HarddiskInsertCmd_t insert{};
  insert.drive = harddisk_drive_0;
  insert.write_protected = 0;
  strncpy(insert.path, fixture.c_str(), sizeof(insert.path) - 1);
  CHECK(harness.desc->command(harness.instance, harddisk_cmd_insert, &insert,
                              sizeof(insert)) == peripheral_ok);

  // Set non-trivial registers
  harness.write_io(0xC0F3, 0x80);  // Unit
  harness.write_io(0xC0F2, 0x03);  // Command
  harness.write_io(0xC0F4, 0x55);  // Mem Lo
  harness.write_io(0xC0F5, 0xAA);  // Mem Hi
  harness.write_io(0xC0F6, 0x11);  // Block Lo
  harness.write_io(0xC0F7, 0x22);  // Block Hi

  // Save state
  size_t size = sizeof(HarddiskSaveState_t);
  std::vector<uint8_t> ss_buffer(size, 0);
  CHECK(harness.desc->save_state(harness.instance, ss_buffer.data(), &size) ==
        peripheral_ok);
  CHECK(size == 2160);

  // Reset and verify cleared state
  harness.desc->reset(harness.instance);
  harness.write_io(0xC0F3, 0x00);
  harness.write_io(0xC0F2, 0x00);
  harness.write_io(0xC0F4, 0x00);
  harness.write_io(0xC0F5, 0x00);

  // Restore state
  CHECK(harness.desc->load_state(harness.instance, ss_buffer.data(), size) ==
        peripheral_ok);

  // Verify latches are fully restored
  CHECK(harness.read_io(0xC0F3) == 0x80);
  CHECK(harness.read_io(0xC0F2) == 0x03);
  CHECK(harness.read_io(0xC0F4) == 0x55);
  CHECK(harness.read_io(0xC0F5) == 0xAA);
  CHECK(harness.read_io(0xC0F6) == 0x11);
  CHECK(harness.read_io(0xC0F7) == 0x22);

  // Verify drive 0 remains loaded
  HarddiskStatus_t status{};
  size_t status_size = sizeof(status);
  CHECK(harness.desc->query(harness.instance, harddisk_query_status, &status,
                            &status_size) == peripheral_ok);
  CHECK(status.drive0_loaded == 1);
  CHECK(strcmp(status.drive0_full_path, fixture.c_str()) == 0);
}

TEST_CASE("HD-16: Corrupt Save State Rejection") {
  HarddiskHarness harness(7);
  REQUIRE(harness.instance != nullptr);

  size_t size = sizeof(HarddiskSaveState_t);
  std::vector<uint8_t> ss_buffer(size, 0);
  CHECK(harness.desc->save_state(harness.instance, ss_buffer.data(), &size) ==
        peripheral_ok);

  // Null buffer
  CHECK(harness.desc->load_state(harness.instance, nullptr, size) ==
        peripheral_error);

  // Undersized buffer
  CHECK(harness.desc->load_state(harness.instance, ss_buffer.data(),
                                 size - 1) == peripheral_error);

  // Oversized buffer
  CHECK(harness.desc->load_state(harness.instance, ss_buffer.data(),
                                 size + 1) == peripheral_error);

  // Corrupted version
  auto* ss = reinterpret_cast<HarddiskSaveState_t*>(ss_buffer.data());
  ss->version = 999;
  CHECK(harness.desc->load_state(harness.instance, ss_buffer.data(), size) ==
        peripheral_error);
  ss->version = HARDDISK_STATE_VERSION;

  // Corrupted struct_size
  ss->struct_size = 1234;
  CHECK(harness.desc->load_state(harness.instance, ss_buffer.data(), size) ==
        peripheral_error);
}
