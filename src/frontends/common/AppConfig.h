#pragma once

#include <array>
#include <cstring>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

enum AppIntent_t { INTENT_RUN, INTENT_DIAGNOSTIC, INTENT_HELP, INTENT_ERROR };

enum { ARGV_EXTRA_MAX = 64 };

struct AppConfig_t {
  AppIntent_t intent = INTENT_RUN;
  std::array<std::array<char, path_max_len>, disk_drive_count> disk_path = {};
  std::array<std::array<char, path_max_len>, 2> harddisk_path = {};
  std::array<char, path_max_len> program_path = {};
  std::array<char, path_max_len> config_path = {};
  std::array<char, path_max_len> snapshot_path = {};
  std::array<char, path_max_len> audio_dump_path = {};

  eApple2Type apple2_type = A2TYPE_APPLE2EENHANCED;
  bool apple2_type_explicit = false;
  bool is_pal = false;
  bool is_fullscreen = false;
  bool is_boot = false;
  bool is_benchmark = false;
  bool is_log = false;
  bool is_verbose = false;

  bool is_list_hardware = false;
  std::array<char, path_max_len> hardware_info_name = {};

  // Test/Diagnostic fields
  std::array<char, path_max_len> test_cpu_file = {};
  uint16_t test_cpu_trap = TRAP_NMOS_DEFAULT;
  std::array<char, path_max_len> debugger_script = {};
  bool disable_debugger = false;

  // Extra args for frontend pass-through
  int argc_extra = 0;
  std::array<const char*, ARGV_EXTRA_MAX> argv_extra = {};
};

/**
 * Initialize AppConfig_t with default values.
 * Note: Member initializers handle most defaults, this ensures parity for
 * existing calls.
 */
inline void AppConfig_Default(AppConfig_t* config) {
  if (config) {
    *config = AppConfig_t{};
  }
}
