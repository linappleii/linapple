// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/AppArgs.h"

// NOLINTNEXTLINE(misc-include-cleaner) Justification: glibc forwards getopt declarations from internal bits headers
#include <getopt.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "frontends/common/AppConfig.h"

enum OptId_t : int {
  k_opt_list_hardware = 0x100,
  k_opt_hardware_info = 0x101,
  k_opt_no_debugger = 0x102,
  k_opt_hd1 = 0x103,
  k_opt_hd2 = 0x104,
  k_opt_basic_sync = 0x105,
  k_opt_basic_line_mode = 0x106,
  k_opt_caps_mode = 0x107,
  k_opt_tui_render = 0x108,
};

// NOLINTBEGIN(misc-include-cleaner) Justification: GNU glibc forwards getopt declarations from internal bits headers
static constexpr struct option k_option_table[] = {
    {"d1", required_argument, nullptr, '1'},
    {"d2", required_argument, nullptr, '2'},
    {"hd1", required_argument, nullptr, k_opt_hd1},
    {"hd2", required_argument, nullptr, k_opt_hd2},
    {"autoboot", no_argument, nullptr, 'a'},
    {"boot", no_argument, nullptr, 'b'},
    {"config", required_argument, nullptr, 'c'},
    {"fullscreen", no_argument, nullptr, 'f'},
    {"help", no_argument, nullptr, 'h'},
    {"log", no_argument, nullptr, 'l'},
    {"benchmark", no_argument, nullptr, 'm'},
    {"pal", no_argument, nullptr, 'p'},
    {"program", required_argument, nullptr, 'P'},
    {"rom", required_argument, nullptr, 'R'},
    {"snapshot", required_argument, nullptr, 's'},
    {"script", required_argument, nullptr, 'x'},
    {"test-cpu", required_argument, nullptr, 'T'},
    {"test-trap", required_argument, nullptr, 'X'},
    {"test-6502", no_argument, nullptr, '6'},
    {"test-65c02", no_argument, nullptr, 'C'},
    {"verbose", no_argument, nullptr, 'v'},
    {"audio-dump", required_argument, nullptr, 'A'},
    {"list-hardware", no_argument, nullptr, k_opt_list_hardware},
    {"hardware-info", required_argument, nullptr, k_opt_hardware_info},
    {"no-debugger", no_argument, nullptr, k_opt_no_debugger},
    {"basic-sync", required_argument, nullptr, k_opt_basic_sync},
    {"basic-line-mode", required_argument, nullptr, k_opt_basic_line_mode},
    {"caps-mode", required_argument, nullptr, k_opt_caps_mode},
    {"tui-render", required_argument, nullptr, k_opt_tui_render},
    {nullptr, 0, nullptr, 0}};
// NOLINTEND(misc-include-cleaner)

static const char* const k_opt_string = ":1:2:abc:fhlmpP:R:s:vx:T:X:6CA:";

static auto parse_tui_render_mode(const char* arg, TuiRenderMode_t* out_mode)
    -> bool {
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

static auto append_extra_arg(AppConfig_t* config, const char* arg) -> void {
  if (config == nullptr || arg == nullptr) {
    return;
  }
  if (config->argc_extra >= 0 && config->argc_extra < ARGV_EXTRA_MAX) {
    config->argv_extra.at(static_cast<size_t>(config->argc_extra)) = arg;
    config->argc_extra++;
  }
}

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
#if ENABLE_ROM_SSC
  printf("  - Super Serial Card (SSC)\n");
#endif
#if ENABLE_ROM_MOUSE
  printf("  - Apple II Mouse Interface\n");
#endif
}

// NOLINTBEGIN(misc-include-cleaner) Justification: GNU glibc forwards getopt declarations from internal bits headers
auto app_args_parse(int argc, char** argv, AppConfig_t* config) -> int {
  if (config == nullptr || argv == nullptr || argc < 1) {
    return -1;
  }
  app_config_default(config);

  int opt = -1;
  int opt_idx = -1;
  opterr = 0;
  optind = 1;

  while ((opt = getopt_long(argc, argv, k_opt_string, k_option_table,
                            &opt_idx)) != -1) {
    switch (opt) {
      case '1':
        util_safe_strcpy(config->disk_path.at(0).data(), optarg, path_max_len);
        break;
      case '2':
        util_safe_strcpy(config->disk_path.at(1).data(), optarg, path_max_len);
        break;
      case 'a':
      case 'b':
        config->is_boot = true;
        break;
      case 'c':
        util_safe_strcpy(config->config_path.data(), optarg, path_max_len);
        break;
      case 'f':
        config->is_fullscreen = true;
        config->is_fullscreen_explicit = true;
        break;
      case 'l':
        config->is_log = true;
        break;
      case 'm':
        config->is_benchmark = true;
        config->intent = INTENT_DIAGNOSTIC;
        break;
      case 'p':
        config->is_pal = true;
        config->is_pal_explicit = true;
        break;
      case 'P':
        util_safe_strcpy(config->program_path.data(), optarg, path_max_len);
        break;
      case 'R':
        util_safe_strcpy(config->rom_path.data(), optarg, path_max_len);
        break;
      case 's':
        util_safe_strcpy(config->snapshot_path.data(), optarg, path_max_len);
        break;
      case 'v':
        config->is_verbose = true;
        Logger::set_verbosity(LogLevel_t::perf);
        break;
      case 'x':
        util_safe_strcpy(config->debugger_script.data(), optarg, path_max_len);
        break;
      case 'T':
        util_safe_strcpy(config->test_cpu_file.data(), optarg, path_max_len);
        config->intent = INTENT_DIAGNOSTIC;
        break;
      case 'X':
        config->test_cpu_trap =
            static_cast<uint16_t>(strtol(optarg, nullptr, 0));
        break;
      case '6':
        config->apple2_type = A2TYPE_APPLE2PLUS;
        config->apple2_type_explicit = true;
        break;
      case 'C':
        config->apple2_type = A2TYPE_APPLE2EENHANCED;
        config->apple2_type_explicit = true;
        break;
      case 'A':
        util_safe_strcpy(config->audio_dump_path.data(), optarg, path_max_len);
        break;
      case k_opt_list_hardware:
        config->is_list_hardware = true;
        config->intent = INTENT_DIAGNOSTIC;
        break;
      case k_opt_hardware_info:
        util_safe_strcpy(config->hardware_info_name.data(), optarg,
                         path_max_len);
        config->intent = INTENT_DIAGNOSTIC;
        break;
      case k_opt_no_debugger:
        config->disable_debugger = true;
        break;
      case k_opt_hd1:
        util_safe_strcpy(config->harddisk_path.at(0).data(), optarg,
                         path_max_len);
        break;
      case k_opt_hd2:
        util_safe_strcpy(config->harddisk_path.at(1).data(), optarg,
                         path_max_len);
        break;
      case k_opt_basic_sync:
        util_safe_strcpy(config->basic_sync_file.data(), optarg, path_max_len);
        break;
      case k_opt_basic_line_mode:
        config->basic_line_mode =
            (optarg != nullptr && (std::strcmp(optarg, "positional") == 0 ||
                                   std::strcmp(optarg, "1") == 0))
                ? 1
                : 0;
        break;
      case k_opt_caps_mode:
        config->caps_lock_mode =
            (optarg != nullptr && (std::strcmp(optarg, "emulated") == 0 ||
                                   std::strcmp(optarg, "1") == 0))
                ? caps_mode_emulated
                : caps_mode_host;
        break;
      case k_opt_tui_render:
        if (!parse_tui_render_mode(optarg, &config->tui_render_mode)) {
          fprintf(stderr,
                  "error: Invalid --tui-render mode '%s'. Expected 'smart' or "
                  "'block'.\n",
                  optarg != nullptr ? optarg : "");
          config->intent = INTENT_ERROR;
          return -1;
        }
        config->tui_render_mode_explicit = true;
        break;
      case 'h':
        config->intent = INTENT_HELP;
        return 0;
      case ':':
        fprintf(stderr, "error: Option requires an argument.\n");
        config->intent = INTENT_ERROR;
        return -1;
      case '?':
        if (optind > 0 && optind <= argc) {
          // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) Justification: argv is an unmanaged array passed from main()
          append_extra_arg(config, argv[optind - 1]);
        }
        break;
      default:
        break;
    }
  }

  while (optind < argc) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) Justification: argv is an unmanaged array passed from main()
    append_extra_arg(config, argv[optind]);
    optind++;
  }

  return 0;
}
// NOLINTEND(misc-include-cleaner)
