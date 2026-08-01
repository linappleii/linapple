#include "frontends/common/AppController.h"

#include <cstdio>

#include "apple2/CPU.h"
#include "apple2/Video.h"
#include "apple2/peripherals/disk/DiskCommands.h"
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
      Configuration_t::instance().get_string("Preferences", reg_key);
  if (path.empty()) {
    path = Path::get_user_data_dir();
  }

  if (!path.empty()) {
    Util_SafeStrCpy(target_buffer, path.c_str(), buffer_size);
    Path::EnsureDirExists(path);
  }
}

auto app_controller_initialize(AppConfig_t* config) -> int {
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
  linapple_init();
  s_initialized = true;

  // 3. Set Hardware Type and PAL
  g_apple2_type = config->apple2Type;
  if (config->bPAL) {
    g_videotype = VT_COLOR_TVEMU;
    g_state.video_scanner_ntsc = false;
    g_state.clks_per_frame = 20280;
  } else {
    g_videotype = VT_COLOR_STANDARD;
    g_state.video_scanner_ntsc = true;
    g_state.clks_per_frame = 17030;
  }

  // 4. Init Snapshots
  if (config->szSnapshotPath.at(0) != '\0') {
    save_state_set_filename(config->szSnapshotPath.data());
  }
  save_state_startup();

  // 5. Initialize directories
  InitializeDirectory(REGVALUE_PREF_START_DIR, &g_state.current_dir[0],
                      sizeof(g_state.current_dir));
  InitializeDirectory(REGVALUE_PREF_HDD_START_DIR, &g_state.hdd_dir[0],
                      sizeof(g_state.hdd_dir));
  InitializeDirectory(REGVALUE_PREF_SAVESTATE_DIR, &g_state.save_state_dir[0],
                      sizeof(g_state.save_state_dir));

  Frontend_UpdateKeyboardMapping();

  if (config->szDebuggerScript.at(0) != '\0') {
    Util_SafeStrCpy(&g_state.debugger_script[0],
                    config->szDebuggerScript.data(), path_max_len);
  }

  g_state.mode = MODE_RUNNING;
  g_state.restart = false;
  g_state.fullscreen = config->bFullscreen;

  bool disable_dbg_config = false;
  if (config_load_bool("Configuration", REGVALUE_DISABLE_DEBUGGER,
                     &disable_dbg_config)) {
    g_state.disable_debugger = config->disable_debugger || disable_dbg_config;
  } else {
    g_state.disable_debugger = config->disable_debugger;
  }

  return 0;
}

auto app_controller_handle_diagnostic_commands(const AppConfig_t* config) -> bool {
  if (config == nullptr) {
    return false;
  }

  if (config->intent == INTENT_HELP) {
    AppArgs_PrintHelp();
    return true;
  }

  if (config->intent == INTENT_DIAGNOSTIC) {
    if (config->bListHardware) {
      linapple_list_hardware();
      return true;
    }
    if (config->szHardwareInfoName.at(0) != '\0') {
      Peripheral_t* p =
          peripheral_find_internal(config->szHardwareInfoName.data());
      if (p != nullptr) {
        printf("Hardware info: %s\n", p->name);
        printf("ABI Version: %d\n", p->AbiVersion_t);
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
            peripheral_get_plugin_path(config->szHardwareInfoName.data());
        if (path != nullptr) {
          printf("Plugin Path: %s\n", path);
        }
      } else {
        fprintf(stderr, "error: Unknown hardware '%s'\n",
                config->szHardwareInfoName.data());
      }
      return true;
    }
    if (config->szTestCpuFile.at(0) != '\0') {
      linapple_cpu_test(config->szTestCpuFile.data(), config->uTestCpuTrap);
      return true;
    }
  }

  return false;
}

void AppController_LoadInitialMedia(const AppConfig_t* config) {
  if (config == nullptr) return;

  // 1. Load Disks or Programs via probing
  for (int i = 0; i < 2; ++i) {
    const char* path = (i == 0) ? config->szDiskPath.at(0).data()
                                : config->szDiskPath.at(1).data();
    if (path != nullptr && *path != '\0') {
      int res = linapple_load_program(path);
      if (res == program_load_not_a_program) {
        // It's a disk image (or at least not a program)
        DiskInsertCmd_t cmd = {};
        cmd.drive = static_cast<uint8_t>(i);
        Util_SafeStrCpy(&cmd.path[0], path, disk_insert_path_max);
        peripheral_command(disk_default_slot, disk_cmd_insert, &cmd,
                           sizeof(cmd));
      }
    }
  }

  // 2. Load explicit program path
  if (config->szProgramPath.at(0) != '\0') {
    if (linapple_load_program(config->szProgramPath.data()) != 0) {
      fprintf(stderr, "error: Could not load program '%s'\n",
              config->szProgramPath.data());
    }
  }

  // 3. Handle Boot
  if (config->bBoot) {
    // Reset the system to boot from disk
    cpu_reset();
    peripheral_manager_reset();
    // Redraw to clear splash
    video_redraw_screen();
  }
}

void AppController_Shutdown() {
  if (!s_initialized) return;

  save_state_shutdown();
  linapple_shutdown();
  Logger::destroy();

  s_initialized = false;
}

auto app_controller_should_restart() -> bool { return g_state.restart; }

void AppController_SetRestart(bool restart) { g_state.restart = restart; }
