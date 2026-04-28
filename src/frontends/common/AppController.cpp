#include "frontends/common/AppController.h"
#include "frontends/common/AppArgs.h"
#include "frontends/common/AppEnvironment.h"
#include "core/LinAppleCore.h"
#include "core/Common_Globals.h"
#include "core/Log.h"
#include "core/ProgramLoader.h"
#include "core/Peripheral_Internal.h"
#include "core/Util_Text.h"
#include "apple2/SaveState.h"
#include "apple2/Video.h"
#include "apple2/DiskCommands.h"
#include "apple2/CPU.h"
#include "core/Peripheral.h"
#include "frontends/sdl3/Frontend.h"
#include <cstdio>

static bool s_initialized = false;

auto AppController_Initialize(AppConfig* config) -> int {
  if (!config) {
    return -1;
  }

  // Idempotency: ensure we start from a clean state if called multiple times
  if (s_initialized) {
    AppController_Shutdown();
  }

  // 1. Resolve paths and init Registry/Logger
  AppEnv_ResolvePaths(config);

  // 2. Init Core
  Linapple_Init();
  s_initialized = true;

  // 3. Set Hardware Type and PAL
  g_Apple2Type = config->apple2Type;
  if (config->bPAL) {
    g_videotype = VT_COLOR_TVEMU;
  } else {
    g_videotype = VT_COLOR_STANDARD;
  }

  // 4. Init Snapshots
  if (config->szSnapshotPath[0] != '\0') {
    Snapshot_SetFilename(&config->szSnapshotPath[0]);
  }
  Snapshot_Startup();

  // 6. Register Peripherals
  Peripheral_Manager_Init();
  Linapple_RegisterPeripherals();
  Frontend_UpdateKeyboardMapping();

  if (config->szDebuggerScript[0] != '\0') {
    Util_SafeStrCpy(&g_state.sDebuggerScript[0], &config->szDebuggerScript[0], PATH_MAX_LEN);
  }

  g_state.mode = MODE_RUNNING;
  g_state.restart = false;
  g_state.fullscreen = config->bFullscreen;

  return 0;
}

auto AppController_HandleDiagnosticCommands(const AppConfig* config) -> bool {
  if (!config) {
    return false;
  }

  if (config->intent == INTENT_HELP) {
    AppArgs_PrintHelp();
    return true;
  }

  if (config->intent == INTENT_DIAGNOSTIC) {
    if (config->bListHardware) {
      Linapple_ListHardware();
      return true;
    }
    if (config->szHardwareInfoName[0] != '\0') {
      Peripheral_t* p = Peripheral_Find_Internal(&config->szHardwareInfoName[0]);
      if (p) {
        printf("Hardware Info: %s\n", p->name);
        printf("ABI Version: %d\n", p->abi_version);
        printf("Compatible Slots: ");
        bool first = true;
        for (int i = 0; i < NUM_SLOTS; ++i) {
          if (p->compatible_slots & (1u << static_cast<uint32_t>(i))) {
          if (!first) printf(", ");
          printf("%d", i);
          first = false;
          }
        }
        printf("\n");
        const char* path = Peripheral_GetPluginPath(&config->szHardwareInfoName[0]);
        if (path) {
          printf("Plugin Path: %s\n", path);
        }
      } else {
        fprintf(stderr, "Error: Unknown hardware '%s'\n", &config->szHardwareInfoName[0]);
      }
      return true;
    }
    if (config->szTestCpuFile[0] != '\0') {
      Linapple_CpuTest(&config->szTestCpuFile[0], config->uTestCpuTrap);
      return true;
    }
  }

  return false;
}

void AppController_LoadInitialMedia(const AppConfig* config) {
  if (!config) return;

  // 1. Load Disks or Programs via probing
  for (int i = 0; i < 2; ++i) {
    const char* path = (i == 0) ? &config->szDiskPath[0][0] : &config->szDiskPath[1][0];
    if (*path != '\0') {
      int res = Linapple_LoadProgram(path);
      if (res == PROGRAM_LOAD_NOT_A_PROGRAM) {
        // It's a disk image (or at least not a program)
        DiskInsertCmd_t cmd = {};
        cmd.drive = static_cast<uint8_t>(i);
        Util_SafeStrCpy(&cmd.path[0], path, DISK_INSERT_PATH_MAX);
        Peripheral_Command(DISK_DEFAULT_SLOT, DISK_CMD_INSERT, &cmd, sizeof(cmd));
      }
    }
  }

  // 2. Load explicit program path
  if (config->szProgramPath[0] != '\0') {
    if (Linapple_LoadProgram(&config->szProgramPath[0]) != 0) {
      fprintf(stderr, "Error: Could not load program '%s'\n", &config->szProgramPath[0]);
    }
  }

  // 3. Handle Boot
  if (config->bBoot) {
    // Reset the system to boot from disk
    CpuReset();
    Peripheral_Manager_Reset();
    // Redraw to clear splash
    VideoRedrawScreen();
  }
}

void AppController_Shutdown() {
  if (!s_initialized) return;

  Snapshot_Shutdown();
  Linapple_Shutdown();
  Logger::Destroy();

  s_initialized = false;
}

auto AppController_ShouldRestart() -> bool {
  return g_state.restart;
}

void AppController_SetRestart(bool restart) {
  g_state.restart = restart;
}
