#include "frontends/common/AppArgs.h"

#include <getopt.h>

#include <array>
#include <cstdio>
#include <cstdlib>

#include "core/Log.h"
#include "core/Util_Text.h"

static constexpr int OPT_LIST_HARDWARE = 0x100;
static constexpr int OPT_HARDWARE_INFO = 0x101;

static const std::array<struct option, 22> OptionTable = {
    {{"d1", required_argument, nullptr, '1'},
     {"d2", required_argument, nullptr, '2'},
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
     {"list-hardware", no_argument, nullptr, OPT_LIST_HARDWARE},
     {"hardware-info", required_argument, nullptr, OPT_HARDWARE_INFO},
     {nullptr, 0, nullptr, 0}}};

static const char* OptString = "1:2:abc:fhlmpP:s:vx:T:X:6CA:";

void AppArgs_PrintHelp() {
  printf("LinApple Emulator\n");
  printf("Usage: linapple [options]\n");
  printf("Options:\n");
  printf("  -1, --d1 <file>        Insert disk image in drive 1\n");
  printf("  -2, --d2 <file>        Insert disk image in drive 2\n");
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
}

auto AppArgs_Parse(int argc, char* argv[], AppConfig* outConfig) -> int {
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
        Util_SafeStrCpy(outConfig->szDiskPath.at(0).data(), optarg,
                        PATH_MAX_LEN);
        break;
      case '2':
        Util_SafeStrCpy(outConfig->szDiskPath.at(1).data(), optarg,
                        PATH_MAX_LEN);
        break;
      case 'a':
      case 'b':
        outConfig->bBoot = true;
        break;
      case 'c':
        Util_SafeStrCpy(outConfig->szConfigPath.data(), optarg, PATH_MAX_LEN);
        break;
      case 'f':
        outConfig->bFullscreen = true;
        break;
      case 'l':
        outConfig->bLog = true;
        break;
      case 'm':
        outConfig->bBenchmark = true;
        outConfig->intent = INTENT_DIAGNOSTIC;
        break;
      case 'p':
        outConfig->bPAL = true;
        break;
      case 'P':
        Util_SafeStrCpy(outConfig->szProgramPath.data(), optarg, PATH_MAX_LEN);
        break;
      case 's':
        Util_SafeStrCpy(outConfig->szSnapshotPath.data(), optarg, PATH_MAX_LEN);
        break;
      case 'v':
        outConfig->bVerbose = true;
        Logger::SetVerbosity(LogLevel::kPerf);
        break;
      case 'x':
        Util_SafeStrCpy(outConfig->szDebuggerScript.data(), optarg,
                        PATH_MAX_LEN);
        break;
      case 'T':
        Util_SafeStrCpy(outConfig->szTestCpuFile.data(), optarg, PATH_MAX_LEN);
        outConfig->intent = INTENT_DIAGNOSTIC;
        break;
      case 'X':
        outConfig->uTestCpuTrap =
            static_cast<uint16_t>(strtol(optarg, nullptr, 0));
        break;
      case '6':
        outConfig->apple2Type = A2TYPE_APPLE2PLUS;
        break;
      case 'C':
        outConfig->apple2Type = A2TYPE_APPLE2EENHANCED;
        break;
      case 'A':
        Util_SafeStrCpy(outConfig->szAudioDumpPath.data(), optarg,
                        PATH_MAX_LEN);
        break;
      case OPT_LIST_HARDWARE:
        outConfig->bListHardware = true;
        outConfig->intent = INTENT_DIAGNOSTIC;
        break;
      case OPT_HARDWARE_INFO:
        Util_SafeStrCpy(outConfig->szHardwareInfoName.data(), optarg,
                        PATH_MAX_LEN);
        outConfig->intent = INTENT_DIAGNOSTIC;
        break;
      case 'h':
        outConfig->intent = INTENT_HELP;
        return 0;
      case '?':
        // Pass-through: unknown option or missing argument
        // If optopt is set, it means a required argument was missing.
        if (optopt != 0) {
          fprintf(stderr, "Error: Option -%c requires an argument.\n", optopt);
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
