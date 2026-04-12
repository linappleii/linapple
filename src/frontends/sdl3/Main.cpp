#include "Common.h"
#include <SDL3/SDL.h>
#include <getopt.h>
#include <cstdio>
#include <cstdlib>

#include "AppleWin.h"
#include "apple2/Keyboard.h"
#include "apple2/Speaker.h"
#include "apple2/Disk.h"
#include "apple2/Mockingboard.h"
#include "apple2/SoundCore.h"
#include "apple2/Video.h"
#include "Frame.h"
#include "Log.h"
#include <X11/Xlib.h>
#include "Common_Globals.h"
#include "Util_Text.h"
#include "Debugger/Debug.h"
#include "DiskChoose.h"

// SDL Audio Stream for Frontend
bool g_bDSAvailable = false;
SDL_AudioStream *g_audioStream = NULL;

static void SDLCALL sdl3AudioCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
  (void)userdata;
  (void)total_amount;
  if (additional_amount <= 0) return;

  int16_t *temp_buf = (int16_t *)SDL_malloc(additional_amount);
  if (!temp_buf) return;

  int num_samples = additional_amount / (sizeof(int16_t) * 2); // stereo
  SoundCore_GetSamples(temp_buf, num_samples * 2);

  SDL_PutAudioStreamData(stream, temp_buf, additional_amount);
  SDL_free(temp_buf);
}

bool DSInit() {
  if (g_audioStream) return true;

  SDL_AudioSpec desired;
  desired.freq = SPKR_SAMPLE_RATE;
  desired.channels = 2;
  desired.format = SDL_AUDIO_S16;

  g_audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, sdl3AudioCallback, NULL);
  if (g_audioStream == NULL) {
    printf("Unable to open SDL audio: %s\n", SDL_GetError());
    return false;
  }

  SDL_ResumeAudioStreamDevice(g_audioStream);
  g_bDSAvailable = true;
  return true;
}

void DSShutdown() {
  if (g_audioStream) {
    SDL_DestroyAudioStream(g_audioStream);
    g_audioStream = NULL;
  }
}

void Sys_Input() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (g_state.mode == MODE_DISK_CHOOSE) {
      DiskChoose_Tick(&event);
      continue;
    }
    FrameDispatchMessage(&event);
  }
}

void Sys_Think(uint32_t dwCycles) {
  if (g_state.mode == MODE_RUNNING) {
    ContinueExecution(dwCycles);
    MB_EndOfVideoFrame();
  } else if (g_state.mode == MODE_STEPPING) {
    SingleStep(false);
  } else if (g_state.mode == MODE_LOGO) {
    DrawAppleContent();
  } else if (g_state.mode == MODE_DEBUG) {
    // Debugger handles its own logic
  }
}

void Sys_Draw() {
  if (g_state.mode == MODE_DISK_CHOOSE) {
    DiskChoose_Draw();
  } else {
    DrawFrameWindow();
  }
}

void EnterMessageLoop() {
  const int TICKS_PER_SECOND = 60;
  const int SKIP_TICKS = 1000 / TICKS_PER_SECOND;
  const int MAX_FRAMESKIP = 5;
  const uint32_t CYCLES_PER_TICK = 17030; // ~1.023 MHz / 60

  uint64_t next_game_tick = SDL_GetTicks();

  while (g_state.mode != MODE_EXIT) {
    int loops = 0;
    while (SDL_GetTicks() >= next_game_tick && loops < MAX_FRAMESKIP) {
      Sys_Input();
      Sys_Think(CYCLES_PER_TICK);
      next_game_tick += SKIP_TICKS;
      loops++;
    }

    uint64_t now = SDL_GetTicks();
    if (now < next_game_tick) {
        SDL_Delay((uint32_t)(next_game_tick - now));
    } else {
        // If we are way behind, reset next_game_tick to avoid massive catch-up loop
        if (now > next_game_tick + 1000) {
            next_game_tick = now;
        }
    }

    Sys_Draw();
  }
}

static void PrintHelp() {
  printf("Linapple - Apple ][ emulator\n");
  printf("Usage: linapple [options]\n");
  printf("Options:\n");
  printf("  -1, --d1 <file>      Load disk image in drive 1\n");
  printf("  -2, --d2 <file>      Load disk image in drive 2\n");
  printf("  -b, --boot           Boot disk in drive 1\n");
  printf("  -f, --fullscreen     Start in fullscreen mode\n");
  printf("  -p, --pal            Use PAL video timing (default NTSC)\n");
  printf("  -v, --verbose        Enable verbose logging\n");
  printf("  -h, --help           Display this help\n");
}

int main(int argc, char* argv[]) {
  int opt;
  const char* szImageName_drive1 = NULL;
  const char* szImageName_drive2 = NULL;
  const char* szSnapshotFile = NULL;
  const char* szConfigurationFile = NULL;
  const char* szTestFile = NULL;
  bool bBoot = false;
  bool bBenchMark = false;
  bool bSetFullScreen = false;
  bool bLog = false;
  bool bPAL = false;
  bool bTestCpu = false;

  static struct option OptionTable[] = {
    {"d1",           required_argument, 0, '1'},
    {"d2",           required_argument, 0, '2'},
    {"debug-script", required_argument, 0, 'x'},
    {"help",         no_argument,       0, 'h'},
    {"pal",          no_argument,       0, 'p'},
    {"state",        required_argument, 0, 's'},
    {"test-cpu",     required_argument, 0, 'T'},
    {"test-6502",    no_argument,       0, '6'},
    {"test-65c02",   no_argument,       0, 'C'},
    {"verbose",      no_argument,       0, 'v'},
    {0, 0, 0, 0}
  };

  while ((opt = getopt_long(argc, argv, "1:2:abc:fhlmpr:s:vx:T:6C", OptionTable, &optind)) != -1) {
    switch (opt) {
      case '1': szImageName_drive1 = optarg; break;
      case '2': szImageName_drive2 = optarg; break;
      case 's': szSnapshotFile = optarg; break;
      case 'c': szConfigurationFile = optarg; break;
      case 'b': bBoot = true; break;
      case 'm': bBenchMark = true; break;
      case 'f': bSetFullScreen = true; break;
      case 'l': bLog = true; break;
      case 'p': bPAL = true; break;
      case 'v': Logger::SetVerbosity(LogLevel::Perf); break;
      case 'x': Util_SafeStrCpy(g_state.sDebuggerScript, optarg, MAX_PATH); break;
      case 'T': bTestCpu = true; szTestFile = optarg; break;
      case '6': g_Apple2Type = A2TYPE_APPLE2PLUS; break;
      case 'C': g_Apple2Type = A2TYPE_APPLE2EENHANCED; break;
      case 'h': PrintHelp(); return 0;
      default:
        fprintf(stderr, "Check --help for proper usage.\n");
        return 255;
    }
  }

  if (SysInit(bLog) != 0) return 1;

  if (bBoot) {
    g_state.mode = MODE_RUNNING;
  }

  if (bTestCpu) {
    CpuTestHeadless(szTestFile);
    SysShutdown();
    return 0;
  }

  do {
    g_state.restart = false;
    if (SessionInit(szConfigurationFile, bSetFullScreen,
                    szImageName_drive1, szImageName_drive2,
                    szSnapshotFile, bBoot, bPAL) != 0) {
      break;
    }

    if (bBoot) {
        VideoRedrawScreen();
    }

    if (bBenchMark) VideoBenchmark();
    else EnterMessageLoop();

    SessionShutdown();
  } while (g_state.restart);

  SysShutdown();
  return 0;
}
