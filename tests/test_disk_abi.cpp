#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
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
  CHECK(offsetof(DiskInsertCmd_t, drive) == 0);
  CHECK(offsetof(DiskInsertCmd_t, path) == 1);
  CHECK(offsetof(DiskInsertCmd_t, write_protected) == 505);
  CHECK(offsetof(DiskInsertCmd_t, create_if_necessary) == 506);
}

TEST_CASE("DiskABI: [DISK-03] Enum values match ABI specification") {
  CHECK(DISK_DRIVE_0 == 0);
  CHECK(DISK_DRIVE_1 == 1);
  CHECK(DISK_CMD_INSERT == 0x01);
  CHECK(DISK_CMD_EJECT == 0x02);
}

TEST_CASE("DiskABI: [DISK-04] DiskStatus_t field offsets are stable (PACKED)") {
  // Field order: error(4), loaded(1), spinning(1), writing(1), wp(1)
  CHECK(offsetof(DiskStatus_t, drive0_last_error) == 0);
  CHECK(offsetof(DiskStatus_t, drive0_loaded) == 4);
  CHECK(offsetof(DiskStatus_t, drive0_spinning) == 5);
  CHECK(offsetof(DiskStatus_t, drive0_writing) == 6);
  CHECK(offsetof(DiskStatus_t, drive0_write_protected) == 7);
}

TEST_CASE("DiskABI: [ABI-07] SaveState Size Query") {
  Linapple_Init();
  Linapple_RegisterPeripherals();
  size_t size = 0;
  Peripheral_SaveState(SL6, nullptr, &size);
  CHECK(size > 0);
  Linapple_Shutdown();
}

TEST_CASE("DiskABI: [ABI-08] SaveState Undersized Buffer") {
  Linapple_Init();
  Linapple_RegisterPeripherals();
  std::array<uint8_t, 4> buffer{};
  size_t size = buffer.size();
  buffer.fill(BUFFER_INIT_VAL);
  Peripheral_SaveState(SL6, buffer.data(), &size);
  CHECK(buffer[0] == BUFFER_INIT_VAL);
  Linapple_Shutdown();
}

TEST_CASE("DiskABI: [ABI-09] LoadState Version Mismatch") {
  Linapple_Init();
  Linapple_RegisterPeripherals();
  size_t size = 0;
  Peripheral_SaveState(SL6, nullptr, &size);
  std::vector<uint8_t> buffer(size);
  Peripheral_SaveState(SL6, buffer.data(), &size);

  // Corrupt version (first 4 bytes of DiskSavedState_t is Header_t {version,
  // size})
  auto* version = reinterpret_cast<uint32_t*>(buffer.data());
  *version = BAD_VERSION;

  Peripheral_LoadState(SL6, buffer.data(), size);
  Linapple_Shutdown();
}
