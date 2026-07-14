#pragma once

#include <array>
#include <cstring>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

enum AppIntent { INTENT_RUN, INTENT_DIAGNOSTIC, INTENT_HELP, INTENT_ERROR };

enum { ARGV_EXTRA_MAX = 64 };

struct AppConfig {
  AppIntent intent = INTENT_RUN;
  std::array<std::array<char, path_max_len>, disk_drive_count> szDiskPath = {};
  std::array<char, path_max_len> szProgramPath = {};
  std::array<char, path_max_len> szConfigPath = {};
  std::array<char, path_max_len> szSnapshotPath = {};
  std::array<char, path_max_len> szAudioDumpPath = {};

  eApple2Type apple2Type = A2TYPE_APPLE2EENHANCED;
  bool bPAL = false;
  bool bFullscreen = false;
  bool bBoot = false;
  bool bBenchmark = false;
  bool bLog = false;
  bool bVerbose = false;

  bool bListHardware = false;
  std::array<char, path_max_len> szHardwareInfoName = {};

  // Test/Diagnostic fields
  std::array<char, path_max_len> szTestCpuFile = {};
  uint16_t uTestCpuTrap = TRAP_NMOS_DEFAULT;
  std::array<char, path_max_len> szDebuggerScript = {};
  bool bDisableDebugger = false;

  // Extra args for frontend pass-through
  int argc_extra = 0;
  std::array<const char*, ARGV_EXTRA_MAX> argv_extra = {};
};

/**
 * Initialize AppConfig with default values.
 * Note: Member initializers handle most defaults, this ensures parity for
 * existing calls.
 */
inline void AppConfig_Default(AppConfig* pConfig) {
  if (pConfig) {
    *pConfig = AppConfig{};
  }
}
