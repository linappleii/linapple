#include "frontends/common/AppController.h"

#include <cstdio>

#include "apple2/CPU.h"
#include "apple2/Video.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/Common_Globals.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/ProgramLoader.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "frontends/common/AppArgs.h"
#include "frontends/common/AppEnvironment.h"
#include "frontends/common/SaveStateManager.h"
#include "frontends/sdl3/Frontend.h"

static bool s_initialized = false;

static void InitializeDirectory(const char* reg_key, char* target_buffer,
                                size_t buffer_size) {
  std::string path =
      Configuration::Instance().GetString("Preferences", reg_key);
  if (path.empty()) {
    path = Path::GetUserDataDir();
  }

  if (!path.empty()) {
    Util_SafeStrCpy(target_buffer, path.c_str(), buffer_size);
    Path::EnsureDirExists(path);
  }
}

auto AppController_Initialize(AppConfig* config) -> int {
  if (config == nullptr) {
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
    g_state.bVideoScannerNTSC = false;
    g_state.dwClksPerFrame = 20280;
  } else {
    g_videotype = VT_COLOR_STANDARD;
    g_state.bVideoScannerNTSC = true;
    g_state.dwClksPerFrame = 17030;
  }

  // 4. Init Snapshots
  if (config->szSnapshotPath.at(0) != '\0') {
    save_state_set_filename(config->szSnapshotPath.data());
  }
  save_state_startup();

  // 5. Initialize directories
  InitializeDirectory(REGVALUE_PREF_START_DIR, &g_state.sCurrentDir[0],
                      sizeof(g_state.sCurrentDir));
  InitializeDirectory(REGVALUE_PREF_HDD_START_DIR, &g_state.sHDDDir[0],
                      sizeof(g_state.sHDDDir));
  InitializeDirectory(REGVALUE_PREF_SAVESTATE_DIR, &g_state.sSaveStateDir[0],
                      sizeof(g_state.sSaveStateDir));

  Frontend_UpdateKeyboardMapping();

  if (config->szDebuggerScript.at(0) != '\0') {
    Util_SafeStrCpy(&g_state.sDebuggerScript[0],
                    config->szDebuggerScript.data(), path_max_len);
  }

  g_state.mode = MODE_RUNNING;
  g_state.restart = false;
  g_state.fullscreen = config->bFullscreen;

  return 0;
}

auto AppController_HandleDiagnosticCommands(const AppConfig* config) -> bool {
  if (config == nullptr) {
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
    if (config->szHardwareInfoName.at(0) != '\0') {
      Peripheral_t* p =
          Peripheral_Find_Internal(config->szHardwareInfoName.data());
      if (p != nullptr) {
        printf("Hardware Info: %s\n", p->name);
        printf("ABI Version: %d\n", p->abi_version);
        printf("Compatible Slots: ");
        bool first = true;
        for (int i = 0; i < NUM_SLOTS; ++i) {
          if ((p->compatible_slots & (1u << static_cast<uint32_t>(i))) != 0) {
            if (!first) printf(", ");
            printf("%d", i);
            first = false;
          }
        }
        printf("\n");
        const char* path =
            Peripheral_GetPluginPath(config->szHardwareInfoName.data());
        if (path != nullptr) {
          printf("Plugin Path: %s\n", path);
        }
      } else {
        fprintf(stderr, "Error: Unknown hardware '%s'\n",
                config->szHardwareInfoName.data());
      }
      return true;
    }
    if (config->szTestCpuFile.at(0) != '\0') {
      Linapple_CpuTest(config->szTestCpuFile.data(), config->uTestCpuTrap);
      return true;
    }
  }

  return false;
}

void AppController_LoadInitialMedia(const AppConfig* config) {
  if (config == nullptr) return;

  // 1. Load Disks or Programs via probing
  for (int i = 0; i < 2; ++i) {
    const char* path = (i == 0) ? config->szDiskPath.at(0).data()
                                : config->szDiskPath.at(1).data();
    if (path != nullptr && *path != '\0') {
      int res = Linapple_LoadProgram(path);
      if (res == PROGRAM_LOAD_NOT_A_PROGRAM) {
        // It's a disk image (or at least not a program)
        DiskInsertCmd_t cmd = {};
        cmd.drive = static_cast<uint8_t>(i);
        Util_SafeStrCpy(&cmd.path[0], path, disk_insert_path_max);
        Peripheral_Command(disk_default_slot, disk_cmd_insert, &cmd,
                           sizeof(cmd));
      }
    }
  }

  // 2. Load explicit program path
  if (config->szProgramPath.at(0) != '\0') {
    if (Linapple_LoadProgram(config->szProgramPath.data()) != 0) {
      fprintf(stderr, "Error: Could not load program '%s'\n",
              config->szProgramPath.data());
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

  save_state_shutdown();
  Linapple_Shutdown();
  Logger::Destroy();

  s_initialized = false;
}

auto AppController_ShouldRestart() -> bool { return g_state.restart; }

void AppController_SetRestart(bool restart) { g_state.restart = restart; }
