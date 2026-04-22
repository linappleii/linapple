#pragma once

#include <cstring>
#include "core/Common.h"
#include "apple2/DiskCommands.h"
#include "apple2/CPU.h"

// These are required for this specific C-compatible architectural boundary.
// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays)
// NOLINTBEGIN(modernize-avoid-c-arrays)
// NOLINTBEGIN(cppcoreguidelines-use-enum-class)
// NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)

enum AppIntent {
  INTENT_RUN,
  INTENT_DIAGNOSTIC,
  INTENT_HELP,
  INTENT_ERROR
};

enum {
  ARGV_EXTRA_MAX = 64
};

struct AppConfig {
  AppIntent intent = INTENT_RUN;
  char szDiskPath[DISK_DRIVE_COUNT][PATH_MAX_LEN] = {};
  char szProgramPath[PATH_MAX_LEN] = {};
  char szConfigPath[PATH_MAX_LEN] = {};
  char szSnapshotPath[PATH_MAX_LEN] = {};
  char szAudioDumpPath[PATH_MAX_LEN] = {};

  eApple2Type apple2Type = A2TYPE_APPLE2EENHANCED;
  bool bPAL = false;
  bool bFullscreen = false;
  bool bBoot = false;
  bool bBenchmark = false;
  bool bLog = false;
  bool bVerbose = false;

  bool bListHardware = false;
  char szHardwareInfoName[PATH_MAX_LEN] = {};

  // Test/Diagnostic fields
  char szTestCpuFile[PATH_MAX_LEN] = {};
  uint16_t uTestCpuTrap = TRAP_NMOS_DEFAULT;
  char szDebuggerScript[PATH_MAX_LEN] = {};

  // Extra args for frontend pass-through
  int argc_extra = 0;
  const char* argv_extra[ARGV_EXTRA_MAX] = {};
};

/**
 * Initialize AppConfig with default values.
 * Note: Member initializers handle most defaults, this ensures parity for existing calls.
 */
inline void AppConfig_Default(AppConfig* pConfig) {
  if (pConfig) {
    *pConfig = AppConfig{};
  }
}

// NOLINTEND(cppcoreguidelines-pro-type-member-init)
// NOLINTEND(cppcoreguidelines-use-enum-class)
// NOLINTEND(modernize-avoid-c-arrays)
// NOLINTEND(cppcoreguidelines-avoid-c-arrays)
