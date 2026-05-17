#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <unistd.h>

#include <fstream>

#include "apple2/peripherals/disk/Disk.h"
#include "apple2/Video.h"
#include "core/Common_Globals.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "frontends/common/AppArgs.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AppEnvironment.h"

TEST_CASE("AppController: Initialize and Shutdown") {
  AppConfig config = {};
  AppConfig_Default(&config);

  AppEnv_ResolvePaths(&config);
  // Test initialization
  int result = AppController_Initialize(&config);
  CHECK(result == 0);
  CHECK(g_state.mode == MODE_RUNNING);

  // Check if default directories are initialized
  CHECK(strlen(g_state.sCurrentDir.data()) > 0);
  CHECK(strlen(g_state.sHDDDir.data()) > 0);
  CHECK(strlen(g_state.sSaveStateDir.data()) > 0);
  // Test shutdown
  AppController_Shutdown();
}

TEST_CASE("AppController: Video Mode Reset") {
  AppConfig config = {};
  AppConfig_Default(&config);

  // 1. Init with PAL
  config.bPAL = true;
  AppController_Initialize(&config);
  CHECK(g_videotype == VT_COLOR_TVEMU);

  // 2. Re-init without PAL (should reset to standard)
  config.bPAL = false;
  AppController_Initialize(&config);
  CHECK(g_videotype == VT_COLOR_STANDARD);

  AppController_Shutdown();
}

TEST_CASE("AppController: Media Loading") {
  AppConfig config = {};
  AppConfig_Default(&config);
  const char* disk_path = access("../res/Master.dsk", R_OK) == 0
                              ? "../res/Master.dsk"
                              : "res/Master.dsk";
  Util_SafeStrCpy(config.szDiskPath[0].data(), disk_path, PATH_MAX_LEN);

  AppEnv_ResolvePaths(&config);
  AppController_Initialize(&config);
  AppController_LoadInitialMedia(&config);

  // Explicitly think a bit to process commands from LoadInitialMedia
  for (int i = 0; i < 500; ++i) {
    Peripheral_Manager_Think(100);
  }

  // Check if disk was loaded
  DiskStatus_t status = {};
  size_t status_size = sizeof(status);
  PeripheralStatus res = Peripheral_Query(
      disk_default_slot, disk_cmd_get_status, &status, &status_size);

  CHECK(res == PERIPHERAL_OK);
  CHECK(status.drive0_loaded == 1);

  AppController_Shutdown();
}

TEST_CASE("AppController: Diagnostic Commands") {
  AppConfig config = {};
  AppConfig_Default(&config);
  config.intent = INTENT_HELP;

  // We don't want to actually print help to stdout during tests usually,
  // but here we just verify it returns true as expected.
  CHECK(AppController_HandleDiagnosticCommands(&config) == true);

  config.intent = INTENT_RUN;
  CHECK(AppController_HandleDiagnosticCommands(&config) == false);
}
