// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/AppArgs.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "frontends/common/AppConfig.h"

namespace {

enum class ArgType_t {
  no_arg,
  req_arg,
};

enum OptId_t : int {
  k_opt_unknown = 0,
  k_opt_d1,
  k_opt_d2,
  k_opt_hd1,
  k_opt_hd2,
  k_opt_autoboot,
  k_opt_boot,
  k_opt_config,
  k_opt_fullscreen,
  k_opt_help,
  k_opt_log,
  k_opt_benchmark,
  k_opt_pal,
  k_opt_program,
  k_opt_rom,
  k_opt_snapshot,
  k_opt_script,
  k_opt_test_cpu,
  k_opt_test_trap,
  k_opt_test_6502,
  k_opt_test_65c02,
  k_opt_verbose,
  k_opt_audio_dump,
  k_opt_list_hardware,
  k_opt_hardware_info,
  k_opt_no_debugger,
  k_opt_basic_sync,
  k_opt_basic_line_mode,
  k_opt_caps_mode,
  k_opt_tui_render,
};

struct OptionDef_t {
  const char* long_name;
  char short_name;
  ArgType_t arg_type;
  OptId_t id;
};

constexpr OptionDef_t k_options[] = {
    {"d1", '1', ArgType_t::req_arg, k_opt_d1},
    {"d2", '2', ArgType_t::req_arg, k_opt_d2},
    {"hd1", '\0', ArgType_t::req_arg, k_opt_hd1},
    {"hd2", '\0', ArgType_t::req_arg, k_opt_hd2},
    {"autoboot", 'a', ArgType_t::no_arg, k_opt_autoboot},
    {"boot", 'b', ArgType_t::no_arg, k_opt_boot},
    {"config", 'c', ArgType_t::req_arg, k_opt_config},
    {"fullscreen", 'f', ArgType_t::no_arg, k_opt_fullscreen},
    {"help", 'h', ArgType_t::no_arg, k_opt_help},
    {"log", 'l', ArgType_t::no_arg, k_opt_log},
    {"benchmark", 'm', ArgType_t::no_arg, k_opt_benchmark},
    {"pal", 'p', ArgType_t::no_arg, k_opt_pal},
    {"program", 'P', ArgType_t::req_arg, k_opt_program},
    {"rom", 'R', ArgType_t::req_arg, k_opt_rom},
    {"snapshot", 's', ArgType_t::req_arg, k_opt_snapshot},
    {"script", 'x', ArgType_t::req_arg, k_opt_script},
    {"test-cpu", 'T', ArgType_t::req_arg, k_opt_test_cpu},
    {"test-trap", 'X', ArgType_t::req_arg, k_opt_test_trap},
    {"test-6502", '6', ArgType_t::no_arg, k_opt_test_6502},
    {"test-65c02", 'C', ArgType_t::no_arg, k_opt_test_65c02},
    {"verbose", 'v', ArgType_t::no_arg, k_opt_verbose},
    {"audio-dump", 'A', ArgType_t::req_arg, k_opt_audio_dump},
    {"list-hardware", '\0', ArgType_t::no_arg, k_opt_list_hardware},
    {"hardware-info", '\0', ArgType_t::req_arg, k_opt_hardware_info},
    {"no-debugger", '\0', ArgType_t::no_arg, k_opt_no_debugger},
    {"basic-sync", '\0', ArgType_t::req_arg, k_opt_basic_sync},
    {"basic-line-mode", '\0', ArgType_t::req_arg, k_opt_basic_line_mode},
    {"caps-mode", '\0', ArgType_t::req_arg, k_opt_caps_mode},
    {"tui-render", '\0', ArgType_t::req_arg, k_opt_tui_render},
};

auto parse_tui_render_mode(const char* arg, TuiRenderMode* out_mode) -> bool {
  if (arg == nullptr || out_mode == nullptr) {
    return false;
  }
  if (std::strcmp(arg, "block") == 0 || std::strcmp(arg, "simple") == 0) {
    *out_mode = TUI_RENDER_BLOCK;
    return true;
  }
  if (std::strcmp(arg, "smart") == 0 || std::strcmp(arg, "shape") == 0) {
    *out_mode = TUI_RENDER_SMART;
    return true;
  }
  return false;
}

auto append_extra_arg(AppConfig* config, const char* arg) -> void {
  if (config == nullptr || arg == nullptr) {
    return;
  }
  if (config->argc_extra >= 0 &&
      config->argc_extra < static_cast<int>(argv_extra_max)) {
    config->argv_extra.at(static_cast<size_t>(config->argc_extra)) = arg;
    config->argc_extra++;
  }
}

auto find_option_by_long_name(const char* name, size_t len)
    -> const OptionDef_t* {
  for (const auto& opt : k_options) {
    if (std::strncmp(opt.long_name, name, len) == 0 &&
        opt.long_name[len] == '\0') {
      return &opt;
    }
  }
  return nullptr;
}

auto find_option_by_short_name(char c) -> const OptionDef_t* {
  if (c == '\0') {
    return nullptr;
  }
  for (const auto& opt : k_options) {
    if (opt.short_name == c) {
      return &opt;
    }
  }
  return nullptr;
}

auto apply_option(OptId_t id, const char* val, AppConfig* config) -> int {
  switch (id) {
    case k_opt_d1:
      util_safe_strcpy(config->disk_path.at(0).data(), val, path_max_len);
      break;
    case k_opt_d2:
      util_safe_strcpy(config->disk_path.at(1).data(), val, path_max_len);
      break;
    case k_opt_hd1:
      util_safe_strcpy(config->harddisk_path.at(0).data(), val, path_max_len);
      break;
    case k_opt_hd2:
      util_safe_strcpy(config->harddisk_path.at(1).data(), val, path_max_len);
      break;
    case k_opt_autoboot:
    case k_opt_boot:
      config->is_boot = true;
      break;
    case k_opt_config:
      util_safe_strcpy(config->config_path.data(), val, path_max_len);
      break;
    case k_opt_fullscreen:
      config->is_fullscreen = true;
      config->is_fullscreen_explicit = true;
      break;
    case k_opt_help:
      config->intent = INTENT_HELP;
      return 1;
    case k_opt_log:
      config->is_log = true;
      break;
    case k_opt_benchmark:
      config->is_benchmark = true;
      config->intent = INTENT_DIAGNOSTIC;
      break;
    case k_opt_pal:
      config->is_pal = true;
      config->is_pal_explicit = true;
      break;
    case k_opt_program:
      util_safe_strcpy(config->program_path.data(), val, path_max_len);
      break;
    case k_opt_rom:
      util_safe_strcpy(config->rom_path.data(), val, path_max_len);
      break;
    case k_opt_snapshot:
      util_safe_strcpy(config->snapshot_path.data(), val, path_max_len);
      break;
    case k_opt_script:
      util_safe_strcpy(config->debugger_script.data(), val, path_max_len);
      break;
    case k_opt_verbose:
      config->is_verbose = true;
      break;
    case k_opt_test_cpu:
      util_safe_strcpy(config->test_cpu_file.data(), val, path_max_len);
      config->intent = INTENT_DIAGNOSTIC;
      break;
    case k_opt_test_trap:
      config->test_cpu_trap =
          static_cast<uint16_t>(std::strtol(val, nullptr, 0));
      break;
    case k_opt_test_6502:
      config->apple2_type = A2TYPE_APPLE2PLUS;
      config->apple2_type_explicit = true;
      break;
    case k_opt_test_65c02:
      config->apple2_type = A2TYPE_APPLE2EENHANCED;
      config->apple2_type_explicit = true;
      break;
    case k_opt_audio_dump:
      util_safe_strcpy(config->audio_dump_path.data(), val, path_max_len);
      break;
    case k_opt_list_hardware:
      config->is_list_hardware = true;
      config->intent = INTENT_DIAGNOSTIC;
      break;
    case k_opt_hardware_info:
      util_safe_strcpy(config->hardware_info_name.data(), val, path_max_len);
      config->intent = INTENT_DIAGNOSTIC;
      break;
    case k_opt_no_debugger:
      config->disable_debugger = true;
      break;
    case k_opt_basic_sync:
      util_safe_strcpy(config->basic_sync_file.data(), val, path_max_len);
      break;
    case k_opt_basic_line_mode:
      config->basic_line_mode =
          (val != nullptr &&
           (std::strcmp(val, "positional") == 0 || std::strcmp(val, "1") == 0))
              ? 1
              : 0;
      break;
    case k_opt_caps_mode:
      config->caps_lock_mode =
          (val != nullptr &&
           (std::strcmp(val, "emulated") == 0 || std::strcmp(val, "1") == 0))
              ? caps_mode_emulated
              : caps_mode_host;
      break;
    case k_opt_tui_render:
      if (!parse_tui_render_mode(val, &config->tui_render_mode)) {
        fprintf(stderr,
                "error: Invalid --tui-render mode '%s'. Expected 'smart' or "
                "'block'.\n",
                val != nullptr ? val : "");
        config->intent = INTENT_ERROR;
        return -1;
      }
      config->tui_render_mode_explicit = true;
      break;
    case k_opt_unknown:
    default:
      break;
  }
  return 0;
}

}  // namespace

auto app_args_print_help() -> void {
#ifdef LINAPPLE_FRONTEND_NAME
  printf("LinApple Emulator (Frontend: %s)\n", LINAPPLE_FRONTEND_NAME);
#else
  printf("LinApple Emulator\n");
#endif
  printf("Usage: linapple [options]\n");
  printf("Options:\n");
  printf("  -1, --d1 <file>        Insert disk image in drive 1\n");
  printf("  -2, --d2 <file>        Insert disk image in drive 2\n");
  printf(
      "  --hd1 <file>           Insert hard disk image in drive 1 (Slot 7)\n");
  printf(
      "  --hd2 <file>           Insert hard disk image in drive 2 (Slot 7)\n");
  printf("  -a, --autoboot         Boot the computer immediately\n");
  printf("  -b, --boot             Synonym for --autoboot\n");
  printf("  -c, --config <file>    Use specified configuration file\n");
  printf("  -f, --fullscreen       Start in fullscreen mode\n");
  printf("  -h, --help             Display this help message\n");
  printf("  -l, --log              Enable logging to console\n");
  printf("  -m, --benchmark        Run a video benchmark and exit\n");
  printf("  -p, --pal              Enable PAL video mode\n");
  printf("  -P, --program <file>   Load APL/PRG program file\n");
  printf("  -R, --rom <file>       Load custom system ROM file at runtime\n");
  printf("  -s, --snapshot <file>  Load state from snapshot file\n");
  printf("  -v, --verbose          Enable verbose performance logging\n");
  printf("  -x, --script <file>    Execute debugger script on startup\n");
  printf(
      "  -T, --test-cpu <file>  Run 6502 functional test from binary file\n");
  printf("  -X, --test-trap <addr> Expected trap address for test-cpu (hex)\n");
  printf("  -6, --test-6502        Set Apple2+ mode for testing\n");
  printf("  -C, --test-65c02       Set Enhanced //e mode for testing\n");
  printf("  -A, --audio-dump <file> Dump audio to a RIFF WAV file\n");
  printf("  --list-hardware        List all emulated hardware components\n");
  printf(
      "  --hardware-info <name> Show detailed info for a hardware component\n");
  printf(
      "  --no-debugger          Disable the integrated debugger at runtime\n");
  printf(
      "  --basic-sync <file>    Enable bidirectional host BASIC live-sync\n");
  printf(
      "  --basic-line-mode <mode> Set line numbering mode "
      "(explicit/positional)\n");
  printf(
      "  --caps-mode <mode>     Set Caps Lock mode: host or emulated (default: "
      "host)\n");
  printf(
      "  --tui-render <mode>    Set TUI graphics render mode: smart (default) "
      "or block\n");

  printf("\nBuilt-in System ROMs:\n");
#if ENABLE_ROM_APPLE2
  printf("  - Apple ][\n");
#endif
#if ENABLE_ROM_APPLE2PLUS
  printf("  - Apple ][+\n");
#endif
#if ENABLE_ROM_APPLE2_JPLUS
  printf("  - Apple ][ J-Plus\n");
#endif
#if ENABLE_ROM_APPLE2E
  printf("  - Apple //e (Unenhanced)\n");
#endif
#if ENABLE_ROM_APPLE2ENHANCED
  printf("  - Apple //e (Enhanced)\n");
#endif
#if ENABLE_ROM_CLONE_BASE64A
  printf("  - Base64A (Clone)\n");
#endif
#if ENABLE_ROM_CLONE_PRAVETS
  printf("  - Pravets 82 / 8M / 8C (Clones)\n");
#endif
#if ENABLE_ROM_CLONE_TK3000E
  printf("  - TK3000 //e (Clone)\n");
#endif

  printf("\nBuilt-in Peripheral ROMs:\n");
#if ENABLE_ROM_DISK2
  printf("  - Disk II (16-sector & 13-sector)\n");
#endif
}

auto app_args_parse(int argc, char** argv, AppConfig* config) -> int {
  if (config == nullptr || argv == nullptr || argc < 1) {
    return -1;
  }
  app_config_default(config);

  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (arg == nullptr) {
      continue;
    }

    if (arg[0] == '-' && arg[1] == '-') {
      if (arg[2] == '\0') {
        for (int j = i + 1; j < argc; ++j) {
          append_extra_arg(config, argv[j]);
        }
        break;
      }

      const char* opt_name = arg + 2;
      const char* eq = std::strchr(opt_name, '=');
      size_t name_len = (eq != nullptr) ? static_cast<size_t>(eq - opt_name)
                                        : std::strlen(opt_name);
      const OptionDef_t* opt = find_option_by_long_name(opt_name, name_len);
      if (opt == nullptr) {
        append_extra_arg(config, arg);
        continue;
      }

      const char* val = nullptr;
      if (opt->arg_type == ArgType_t::req_arg) {
        if (eq != nullptr) {
          val = eq + 1;
        } else if (i + 1 < argc) {
          val = argv[++i];
        } else {
          fprintf(stderr, "error: Option requires an argument.\n");
          config->intent = INTENT_ERROR;
          return -1;
        }
      }

      int res = apply_option(opt->id, val, config);
      if (res < 0) {
        return -1;
      }
      if (res == 1) {
        return 0;
      }
      continue;
    }

    if (arg[0] == '-' && arg[1] != '\0') {
      bool handled = false;
      for (size_t c = 1; arg[c] != '\0'; ++c) {
        const OptionDef_t* opt = find_option_by_short_name(arg[c]);
        if (opt == nullptr) {
          append_extra_arg(config, arg);
          handled = true;
          break;
        }

        const char* val = nullptr;
        if (opt->arg_type == ArgType_t::req_arg) {
          if (arg[c + 1] != '\0') {
            val = &arg[c + 1];
          } else if (i + 1 < argc) {
            val = argv[++i];
          } else {
            fprintf(stderr, "error: Option requires an argument.\n");
            config->intent = INTENT_ERROR;
            return -1;
          }
          int res = apply_option(opt->id, val, config);
          if (res < 0) {
            return -1;
          }
          if (res == 1) {
            return 0;
          }
          handled = true;
          break;
        }

        int res = apply_option(opt->id, nullptr, config);
        if (res < 0) {
          return -1;
        }
        if (res == 1) {
          return 0;
        }
      }
      if (!handled) {
        continue;
      }
      continue;
    }

    append_extra_arg(config, arg);
  }

  return 0;
}
