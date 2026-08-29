// NOLINTBEGIN(misc-include-cleaner) - glibc internal getopt headers
#include "frontends/common/AppArgs.h"

#include <getopt.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Util_Text.h"
#include "frontends/common/AppConfig.h"

static constexpr int opt_list_hardware = 0x100;
static constexpr int opt_hardware_info = 0x101;
static constexpr int opt_no_debugger = 0x102;
static constexpr int opt_hd1 = 0x103;
static constexpr int opt_hd2 = 0x104;
static constexpr int opt_basic_sync = 0x105;
static constexpr int opt_basic_line_mode = 0x106;
static constexpr int opt_caps_mode = 0x107;

static const std::array<struct option, 28> OptionTable = {
    {{"d1", required_argument, nullptr, '1'},
     {"d2", required_argument, nullptr, '2'},
     {"hd1", required_argument, nullptr, opt_hd1},
     {"hd2", required_argument, nullptr, opt_hd2},
     {"autoboot", no_argument, nullptr, 'a'},
     {"boot", no_argument, nullptr, 'b'},
     {"config", required_argument, nullptr, 'c'},
     {"fullscreen", no_argument, nullptr, 'f'},
     {"help", no_argument, nullptr, 'h'},
     {"log", no_argument, nullptr, 'l'},
     {"benchmark", no_argument, nullptr, 'm'},
     {"pal", no_argument, nullptr, 'p'},
     {"program", required_argument, nullptr, 'P'},
     {"snapshot", required_argument, nullptr, 's'},
     {"script", required_argument, nullptr, 'x'},
     {"test-cpu", required_argument, nullptr, 'T'},
     {"test-trap", required_argument, nullptr, 'X'},
     {"test-6502", no_argument, nullptr, '6'},
     {"test-65c02", no_argument, nullptr, 'C'},
     {"verbose", no_argument, nullptr, 'v'},
     {"audio-dump", required_argument, nullptr, 'A'},
     {"list-hardware", no_argument, nullptr, opt_list_hardware},
     {"hardware-info", required_argument, nullptr, opt_hardware_info},
     {"no-debugger", no_argument, nullptr, opt_no_debugger},
     {"basic-sync", required_argument, nullptr, opt_basic_sync},
     {"basic-line-mode", required_argument, nullptr, opt_basic_line_mode},
     {"caps-mode", required_argument, nullptr, opt_caps_mode},
     {nullptr, 0, nullptr, 0}}};

static const char* OptString = "1:2:abc:fhlmpP:s:vx:T:X:6CA:";

void AppArgs_PrintHelp() {
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
  printf("  -s, --snapshot <f>     Load state from snapshot file\n");
  printf("  -v, --verbose          Enable verbose performance logging\n");
  printf("  -x, --script <file>    Execute debugger script on startup\n");
  printf(
      "  -T, --test-cpu <f>     Run 6502 functional test from binary file\n");
  printf("  -X, --test-trap <n>    Expected trap address for test-cpu (hex)\n");
  printf("  -6, --test-6502        Set Apple2+ mode for testing\n");
  printf("  -C, --test-65c02       Set Enhanced //e mode for testing\n");
  printf("  -A, --audio-dump <f>   Dump audio to a RIFF WAV file\n");
  printf("  --list-hardware        List all emulated hardware components\n");
  printf(
      "  --hardware-info <name> Show detailed info for a hardware component\n");
  printf(
      "  --no-debugger          Disable the integrated debugger at runtime\n");
  printf(
      "  --basic-sync <file>    Enable bidirectional host BASIC live-sync\n");
  printf(
      "  --basic-line-mode <m>  Set line numbering mode "
      "(explicit/positional)\n");
  printf(
      "  --caps-mode <mode>     Set Caps Lock mode: host or emulated (default: "
      "host)\n");
}

auto app_args_parse(int argc, char** argv, AppConfig_t* outConfig) -> int {
  if (outConfig == nullptr) {
    return -1;
  }
  AppConfig_Default(outConfig);

  int opt = -1;
  int opt_idx = -1;
  opterr = 0;  // Suppress getopt error messages
  optind = 1;  // Reset for multiple calls if necessary

  while ((opt = getopt_long(argc, argv, OptString, OptionTable.data(),
                            &opt_idx)) != -1) {
    switch (opt) {
      case '1':
        Util_SafeStrCpy(outConfig->disk_path.at(0).data(), optarg,
                        path_max_len);
        break;
      case '2':
        Util_SafeStrCpy(outConfig->disk_path.at(1).data(), optarg,
                        path_max_len);
        break;
      case 'a':
      case 'b':
        outConfig->is_boot = true;
        break;
      case 'c':
        Util_SafeStrCpy(outConfig->config_path.data(), optarg, path_max_len);
        break;
      case 'f':
        outConfig->is_fullscreen = true;
        break;
      case 'l':
        outConfig->is_log = true;
        break;
      case 'm':
        outConfig->is_benchmark = true;
        outConfig->intent = INTENT_DIAGNOSTIC;
        break;
      case 'p':
        outConfig->is_pal = true;
        break;
      case 'P':
        Util_SafeStrCpy(outConfig->program_path.data(), optarg, path_max_len);
        break;
      case 's':
        Util_SafeStrCpy(outConfig->snapshot_path.data(), optarg, path_max_len);
        break;
      case 'v':
        outConfig->is_verbose = true;
        Logger::set_verbosity(LogLevel_t::k_perf);
        break;
      case 'x':
        Util_SafeStrCpy(outConfig->debugger_script.data(), optarg,
                        path_max_len);
        break;
      case 'T':
        Util_SafeStrCpy(outConfig->test_cpu_file.data(), optarg, path_max_len);
        outConfig->intent = INTENT_DIAGNOSTIC;
        break;
      case 'X':
        outConfig->test_cpu_trap =
            static_cast<uint16_t>(strtol(optarg, nullptr, 0));
        break;
      case '6':
        outConfig->apple2_type = A2TYPE_APPLE2PLUS;
        outConfig->apple2_type_explicit = true;
        break;
      case 'C':
        outConfig->apple2_type = A2TYPE_APPLE2EENHANCED;
        outConfig->apple2_type_explicit = true;
        break;
      case 'A':
        Util_SafeStrCpy(outConfig->audio_dump_path.data(), optarg,
                        path_max_len);
        break;
      case opt_list_hardware:
        outConfig->is_list_hardware = true;
        outConfig->intent = INTENT_DIAGNOSTIC;
        break;
      case opt_hardware_info:
        Util_SafeStrCpy(outConfig->hardware_info_name.data(), optarg,
                        path_max_len);
        outConfig->intent = INTENT_DIAGNOSTIC;
        break;
      case opt_no_debugger:
        outConfig->disable_debugger = true;
        break;
      case opt_hd1:
        Util_SafeStrCpy(outConfig->harddisk_path.at(0).data(), optarg,
                        path_max_len);
        break;
      case opt_hd2:
        Util_SafeStrCpy(outConfig->harddisk_path.at(1).data(), optarg,
                        path_max_len);
        break;
      case opt_basic_sync:
        Util_SafeStrCpy(outConfig->basic_sync_file.data(), optarg,
                        path_max_len);
        break;
      case opt_basic_line_mode:
        if (std::strcmp(optarg, "positional") == 0 ||
            std::strcmp(optarg, "1") == 0) {
          outConfig->basic_line_mode = 1;
        } else {
          outConfig->basic_line_mode = 0;
        }
        break;
      case opt_caps_mode:
        if (std::strcmp(optarg, "emulated") == 0 ||
            std::strcmp(optarg, "1") == 0) {
          outConfig->caps_lock_mode = CAPS_MODE_EMULATED;
        } else {
          outConfig->caps_lock_mode = CAPS_MODE_HOST;
        }
        break;
      case 'h':
        outConfig->intent = INTENT_HELP;
        return 0;
      case '?':
        // Pass-through: unknown option or missing argument
        // If optopt is set, it means a required argument was missing.
        if (optopt != 0) {
          fprintf(stderr, "error: Option -%c requires an argument.\n", optopt);
          outConfig->intent = INTENT_ERROR;
          return -1;
        }
        if (outConfig->argc_extra < ARGV_EXTRA_MAX) {
          // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
          outConfig->argv_extra.at(
              static_cast<size_t>(outConfig->argc_extra++)) = argv[optind - 1];
        }
        break;
      default:
        break;
    }
  }

  // Collect remaining non-option arguments
  while (optind < argc) {
    if (outConfig->argc_extra < ARGV_EXTRA_MAX) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      outConfig->argv_extra.at(static_cast<size_t>(outConfig->argc_extra++)) =
          argv[optind++];
    } else {
      optind++;
    }
  }

  return 0;
}

// NOLINTEND(misc-include-cleaner)
