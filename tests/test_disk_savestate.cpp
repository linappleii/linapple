// SPDX-License-Identifier: GPL-2.0-only
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "test_fixtures.h"

namespace {
constexpr int slot_6 = 6;
constexpr size_t dsk_140k_size = 143360;
}  // namespace

TEST_CASE("DiskSaveState: [SS-01] Round-trip fidelity") {
  linapple_init();
  peripheral_manager_init();
  peripheral_register_internal();

  // Insert a disk
  auto fixture = TestFixtures::create_ephemeral("minimal.dsk");
  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  util_safe_strcpy(cmd.path, fixture.c_str(), disk_insert_path_max);
  peripheral_command(slot_6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  DiskStatus_t status{};
  size_t s_size = sizeof(status);
  peripheral_query(slot_6, disk_cmd_get_status, &status, &s_size);
  REQUIRE(status.drive0_loaded == 1);

  // Save State
  size_t state_size = 0;
  peripheral_save_state(slot_6, nullptr, &state_size);
  REQUIRE(state_size == sizeof(DiskSavedState_t));

  std::vector<uint8_t> buffer(state_size);
  peripheral_save_state(slot_6, buffer.data(), &state_size);

  // Reset peripheral state and eject disk
  peripheral_manager_reset();
  DiskEjectCmd_t eject_cmd{};
  eject_cmd.drive = disk_drive_0;
  peripheral_command(slot_6, disk_cmd_eject, &eject_cmd, sizeof(eject_cmd));
  peripheral_manager_think(0);
  peripheral_query(slot_6, disk_cmd_get_status, &status, &s_size);
  CHECK(status.drive0_loaded == 0);

  // Restore State
  peripheral_load_state(slot_6, buffer.data(), state_size);

  peripheral_query(slot_6, disk_cmd_get_status, &status, &s_size);
  CHECK(status.drive0_loaded == 1);
  CHECK(status.drive0_last_error == disk_err_none);
  CHECK(status.drive0_full_path == fixture.path());

  linapple_shutdown();
}

TEST_CASE("DiskSaveState: [SS-02] Missing image on restore") {
  linapple_init();
  peripheral_manager_init();
  peripheral_register_internal();

  TestFixtures::ScopedTempFile_t temp_img(".dsk");
  {
    std::ofstream ofs(temp_img.path(), std::ios::binary);
    REQUIRE(ofs.is_open());
    std::vector<uint8_t> zero(dsk_140k_size, 0);
    ofs.write(reinterpret_cast<const char*>(zero.data()),
              static_cast<std::streamsize>(zero.size()));
  }

  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  util_safe_strcpy(cmd.path, temp_img.c_str(), disk_insert_path_max);
  peripheral_command(slot_6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  size_t state_size = 0;
  peripheral_save_state(slot_6, nullptr, &state_size);
  REQUIRE(state_size == sizeof(DiskSavedState_t));
  std::vector<uint8_t> buffer(state_size);
  peripheral_save_state(slot_6, buffer.data(), &state_size);

  // Make image unreachable
  temp_img.unlink_file();

  // Restore state
  peripheral_load_state(slot_6, buffer.data(), state_size);

  DiskStatus_t status{};
  size_t s_size = sizeof(status);
  peripheral_query(slot_6, disk_cmd_get_status, &status, &s_size);

  // Should handle gracefully: not loaded, but reported error
  CHECK(status.drive0_loaded == 0);
  CHECK(status.drive0_last_error == disk_err_file_not_found);

  linapple_shutdown();
}

TEST_CASE(
    "DiskSaveState: [SNAP-1] Out-of-bounds snapshot indices are safely "
    "clamped") {
  linapple_init();
  peripheral_manager_init();
  peripheral_register_internal();

  auto fixture = TestFixtures::create_ephemeral("minimal.dsk");
  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  util_safe_strcpy(cmd.path, fixture.c_str(), disk_insert_path_max);
  peripheral_command(slot_6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  size_t state_size = 0;
  peripheral_save_state(slot_6, nullptr, &state_size);
  REQUIRE(state_size == sizeof(DiskSavedState_t));

  std::vector<uint8_t> buffer(state_size);
  peripheral_save_state(slot_6, buffer.data(), &state_size);

  // Corrupt the snapshot indices in the buffer to huge values
  auto* state = reinterpret_cast<DiskSavedState_t*>(buffer.data());
  constexpr int32_t huge_val = 0x7FFFFFFF;
  state->drives[0].track = huge_val;
  state->drives[0].phase = huge_val;
  state->drives[0].current_byte_pos = huge_val;
  state->drives[0].nibble_count = huge_val;

  // Loading corrupted state must not crash or trigger OOB writes
  peripheral_load_state(slot_6, buffer.data(), state_size);

  DiskStatus_t status{};
  size_t s_size = sizeof(status);
  peripheral_query(slot_6, disk_cmd_get_status, &status, &s_size);
  CHECK(status.drive0_loaded == 1);
  CHECK(status.drive0_last_error == disk_err_none);

  // Save state again to verify clamped values
  std::vector<uint8_t> saved_buffer(state_size);
  peripheral_save_state(slot_6, saved_buffer.data(), &state_size);

  const auto* saved_state =
      reinterpret_cast<const DiskSavedState_t*>(saved_buffer.data());
  CHECK(saved_state->drives[0].track == 0);
  CHECK(saved_state->drives[0].phase == 0);
  CHECK(saved_state->drives[0].current_byte_pos == 0);
  CHECK(saved_state->drives[0].nibble_count ==
        static_cast<int32_t>(nibbles_per_track));

  linapple_shutdown();
}

TEST_CASE(
    "DiskSaveState: [SNAP-2] Negative snapshot indices are safely clamped to "
    "0") {
  linapple_init();
  peripheral_manager_init();
  peripheral_register_internal();

  auto fixture = TestFixtures::create_ephemeral("minimal.dsk");
  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  util_safe_strcpy(cmd.path, fixture.c_str(), disk_insert_path_max);
  peripheral_command(slot_6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);

  size_t state_size = 0;
  peripheral_save_state(slot_6, nullptr, &state_size);
  REQUIRE(state_size == sizeof(DiskSavedState_t));

  std::vector<uint8_t> buffer(state_size);
  peripheral_save_state(slot_6, buffer.data(), &state_size);

  // Corrupt the snapshot indices to negative values
  auto* state = reinterpret_cast<DiskSavedState_t*>(buffer.data());
  constexpr int32_t neg_val = -1;
  state->drives[0].track = neg_val;
  state->drives[0].phase = neg_val;
  state->drives[0].current_byte_pos = neg_val;
  state->drives[0].nibble_count = neg_val;

  // Loading corrupted state must clamp negative values
  peripheral_load_state(slot_6, buffer.data(), state_size);

  DiskStatus_t status{};
  size_t s_size = sizeof(status);
  peripheral_query(slot_6, disk_cmd_get_status, &status, &s_size);
  CHECK(status.drive0_loaded == 1);
  CHECK(status.drive0_last_error == disk_err_none);

  // Save state again to verify clamped values
  std::vector<uint8_t> saved_buffer(state_size);
  peripheral_save_state(slot_6, saved_buffer.data(), &state_size);

  const auto* saved_state =
      reinterpret_cast<const DiskSavedState_t*>(saved_buffer.data());
  CHECK(saved_state->drives[0].track == 0);
  CHECK(saved_state->drives[0].phase == 0);
  CHECK(saved_state->drives[0].current_byte_pos == 0);
  CHECK(saved_state->drives[0].nibble_count ==
        static_cast<int32_t>(nibbles_per_track));

  linapple_shutdown();
}
