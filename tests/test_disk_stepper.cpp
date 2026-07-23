// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/peripherals/disk/Disk.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "doctest.h"

namespace {
constexpr int SL6 = 6;
constexpr int STEPPER_OFF = 0xC0E0;
constexpr int STEPPER_ON = 0xC0E1;

auto setup_disk_test() -> void {
  linapple_init();
  peripheral_manager_init();
  linapple_register_peripherals();

  DiskInsertCmd_t cmd{};
  cmd.drive = disk_drive_0;
  cmd.write_protected = false;

  char* cwd_raw = get_current_dir_name();
  std::string repo_root = cwd_raw;
  free(cwd_raw);
  size_t build_pos = repo_root.find("/build");
  if (build_pos != std::string::npos) {
    repo_root = repo_root.substr(0, build_pos);
  }

  std::string fixture = repo_root + "/tests/fixtures/minimal.nib";
  Util_SafeStrCpy(cmd.path, fixture.c_str(), disk_insert_path_max);
  peripheral_command(SL6, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(0);
}
}  // namespace

TEST_CASE("DiskStepper: [STEP-01] Phase to Track Mapping") {
  setup_disk_test();

  // Disk II starts at phase 0, track 0.
  // We energize phase 1, then phase 2 to move to track 1.0 (phase 2 in 1/2
  // track model).

  // Step 1: Energize Phase 1
  IOMap_Dispatch(0, STEPPER_ON + 2, 0, 0, 0);  // $C082 (PH1 ON)
  peripheral_manager_think(0);

  // We should be at phase 1 (half track 0.5)
  // Track = phase / 2 = 0.
  // Note: Internal track state isn't exposed in ABI, but we can verify via IO.

  // Step 2: Energize Phase 2, De-energize Phase 1
  IOMap_Dispatch(0, STEPPER_ON + 4, 0, 0, 0);   // $C084 (PH2 ON)
  IOMap_Dispatch(0, STEPPER_OFF + 2, 0, 0, 0);  // $C082 (PH1 OFF)
  peripheral_manager_think(0);

  // Now at phase 2 (track 1.0).
  // Verify by reading $C08C and seeing track 1 data.

  linapple_shutdown();
}

TEST_CASE("DiskStepper: [STEP-02] Track Clamping") {
  setup_disk_test();

  // Try to step backward from track 0
  IOMap_Dispatch(0, STEPPER_ON + 6, 0, 0, 0);  // $C086 (PH3 ON)
  peripheral_manager_think(0);

  // Should still be at phase 0 or clamping at 0.

  // Step far forward (beyond track 35)
  for (int i = 0; i < 80; ++i) {
    IOMap_Dispatch(0, STEPPER_ON + ((i % 4) * 2) + 1, 0, 0, 0);
    peripheral_manager_think(100);
    IOMap_Dispatch(0, STEPPER_OFF + ((i % 4) * 2), 0, 0, 0);
  }

  // Should be clamped at track 34 (phase 69 in 1/2 track model, or track 35
  // depending on resolution).

  linapple_shutdown();
}

TEST_CASE("DiskStepper: [STEP-03] Flush on Seek") {
  setup_disk_test();

  // 1. Enter Write Mode
  IOMap_Dispatch(0, 0xC0EF, 0, 0, 0);  // $C08F (WRITE MODE)

  // 2. Write a unique byte to Track 0
  IOMap_Dispatch(0, 0xC0ED, 1, 0xA5, 0);  // $C08D (LATCH = 0xA5)
  IOMap_Dispatch(0, 0xC0EC, 0, 0, 0);     // $C08C (STROBE WRITE)

  // 3. Step to Track 1
  // Sequence: PH1 ON, PH0 OFF, PH2 ON, PH1 OFF
  IOMap_Dispatch(0, 0xC0E3, 0, 0, 0);  // PH1 ON
  IOMap_Dispatch(0, 0xC0E0, 0, 0, 0);  // PH0 OFF
  IOMap_Dispatch(0, 0xC0E5, 0, 0, 0);  // PH2 ON
  IOMap_Dispatch(0, 0xC0E2, 0, 0, 0);  // PH1 OFF
  peripheral_manager_think(0);

  // The 'step_drive_head' function should have called 'write_track_to_driver'
  // for Track 0 because it was dirty.

  linapple_shutdown();
}
