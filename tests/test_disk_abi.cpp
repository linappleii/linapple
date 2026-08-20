#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Util_Path.h"
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

TEST_CASE("DiskABI: [ABI-07] SaveState Size Query") {
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();
  size_t size = 0;
  peripheral_save_state(SL6, nullptr, &size);
  CHECK(size > 0);
  linapple_shutdown();
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
}

TEST_CASE("DiskABI: [ABI-09] LoadState Version Mismatch") {
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();
  size_t size = 0;
  peripheral_save_state(SL6, nullptr, &size);
  std::vector<uint8_t> buffer(size);
  peripheral_save_state(SL6, buffer.data(), &size);

  // Corrupt version (first 4 bytes of DiskSavedState_t is Header_t {version,
  // size})
  auto* version = reinterpret_cast<uint32_t*>(buffer.data());
  *version = BAD_VERSION;

  peripheral_load_state(SL6, buffer.data(), size);
  linapple_shutdown();
}
