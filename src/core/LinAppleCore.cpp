#include "LinAppleCore.h"
#include "core/Common.h"
#include <cstdio>
#include <vector>
#include "apple2/Keyboard.h"
#include "apple2/Speaker.h"
#include "apple2/Disk.h"
#include "apple2/DiskImage.h"
#include "apple2/Harddisk.h"
#include "apple2/Mockingboard.h"
#include "apple2/SoundCore.h"
#include "apple2/MouseInterface.h"
#include "apple2/Video.h"
#include "apple2/Memory.h"
#include "apple2/CPU.h"
#include "apple2/Clock.h"
#include "apple2/Timer.h"
#include "apple2/SerialComms.h"
#include "apple2/Joystick.h"
#include "apple2/Riff.h"
#include "Debugger/Debug.h"
#include "core/Common_Globals.h"
#include "core/Log.h"
#include "apple2/Structs.h"
#include "apple2/ParallelPrinter.h"

// Forward declarations for coupled frontend functions (to be decoupled in later phases)
extern void SSCFrontend_Update(struct SuperSerialCard*, uint32_t);
extern void PrinterFrontend_Update(uint32_t);
extern void UpdateDisplay(int);
extern struct SuperSerialCard sg_SSC;

static LinappleVideoCallback g_videoCB = nullptr;
static LinappleAudioCallback g_audioCB = nullptr;
static LinappleTitleCallback g_titleCB = nullptr;

static uint8_t g_nRepeatKey = 0;
static uint32_t g_nRepeatDelayCycles = 0;
static bool g_bRepeating = false;

const uint32_t KEY_REPEAT_INITIAL_DELAY = 512000;
const uint32_t KEY_REPEAT_RATE = 68000;

void Linapple_KeyboardThink(uint32_t dwCycles) {
  if (g_nRepeatKey == 0) return;
  g_nRepeatDelayCycles += dwCycles;
  if (!g_bRepeating) {
    if (g_nRepeatDelayCycles >= KEY_REPEAT_INITIAL_DELAY) {
      g_bRepeating = true;
      g_nRepeatDelayCycles = 0;
      KeybPushAppleKey(g_nRepeatKey);
    }
  } else {
    if (g_nRepeatDelayCycles >= KEY_REPEAT_RATE) {
      g_nRepeatDelayCycles = 0;
      KeybPushAppleKey(g_nRepeatKey);
    }
  }
}

extern "C" void Linapple_SetKeyState(uint8_t apple_code, bool bDown) {
  if (bDown) {
    if (g_nRepeatKey == apple_code) return;
    g_nRepeatKey = apple_code;
    g_nRepeatDelayCycles = 0;
    g_bRepeating = false;
    if (apple_code) KeybPushAppleKey(apple_code);
  } else {
    if (g_nRepeatKey == apple_code) {
      g_nRepeatKey = 0;
      g_bRepeating = false;
    }
  }
}

void Linapple_SetCapsLockState(bool bEnabled) {
  KeybSetCapsLock(bEnabled);
}

extern "C" void Linapple_SetAppleKey(int apple_key, bool bDown) {
  JoySetRawButton(apple_key, bDown);
}

extern "C" void Linapple_SetJoystickAxis(int axis, int value) {
    // axis 0=X, 1=Y
    // value -32768 to 32767
    static int s_joyX = 127;
    static int s_joyY = 127;
    int joy_val = ((value + 32768) * 255) / 65535;
    if (axis == 0) s_joyX = joy_val;
    else if (axis == 1) s_joyY = joy_val;
    JoySetRawPosition(0, s_joyX, s_joyY);
}

extern "C" void Linapple_SetJoystickButton(int button, bool down) {
    JoySetRawButton(button, down);
}

void Linapple_SetVideoCallback(LinappleVideoCallback cb) {
    g_videoCB = cb;
}

void Linapple_SetAudioCallback(LinappleAudioCallback cb) {
    g_audioCB = cb;
}

void Linapple_SetTitleCallback(LinappleTitleCallback cb) {
    g_titleCB = cb;
}

// Internal bridge functions
void Linapple_UpdateTitle(const char* title) {
    if (g_titleCB) {
        g_titleCB(title);
    }
}

void Linapple_Init() {
  MemPreInitialize();
  ImageInitialize();
  DiskInitialize();
  CreateColorMixMap();
  SoundCore_Initialize();

  MemInitialize();
  CpuInitialize();
  VideoInitialize();
  SpkrInitialize();
  KeybReset();
  JoyReset();
  MB_Initialize();
  
  uint8_t* pCxRomPeripheral = MemGetAuxPtr(APPLE_SLOT_BEGIN);
  SSC_Initialize(&sg_SSC, pCxRomPeripheral, 2); // Slot 2
  PrintInitialize();
}

void Linapple_Shutdown() {
  PrintDestroy();
  MB_Destroy();
  DiskDestroy();
  VideoDestroy();
  MemDestroy();
  CpuDestroy();
  SoundCore_Destroy();
}

static int16_t g_spkrBuffer[8192];

void SpkrFrontend_Update(uint32_t dwExecutedCycles) {
  if (dwExecutedCycles == 0) return;

  static bool s_lastState = false;
  static double s_nextSampleCycle = 0;
  double clksPerSample = (double)g_fCurrentCLK6502 / SPKR_SAMPLE_RATE;

  SpkrEvent events[MAX_SPKR_EVENTS];
  int num_events = SpkrGetEvents(events, MAX_SPKR_EVENTS);
  int event_idx = 0;

  uint64_t startCycle = g_nCumulativeCycles - dwExecutedCycles;
  uint64_t endCycle = g_nCumulativeCycles;

  if (s_nextSampleCycle < (double)startCycle)
    s_nextSampleCycle = (double)startCycle;

  int numSamples = 0;
  while (s_nextSampleCycle < (double)endCycle && numSamples < 8190) {
    while (event_idx < num_events && (double)events[event_idx].cycle <= s_nextSampleCycle) {
      s_lastState = events[event_idx].state;
      event_idx++;
    }
    int16_t val = s_lastState ? 0x4000 : -0x4000;
    g_spkrBuffer[numSamples++] = val; // L
    g_spkrBuffer[numSamples++] = val; // R
    s_nextSampleCycle += clksPerSample;
  }

  if (numSamples > 0) {
    if (g_audioCB) {
        g_audioCB(g_spkrBuffer, numSamples);
    } else {
        // Fallback to legacy DSUpload if no callback registered
        DSUploadBuffer(g_spkrBuffer, numSamples);
    }
  }
}

static uint32_t Internal_RunCycles(uint32_t dwCycles) {
  if (dwCycles == 0) return 0;

  uint32_t dwExecutedCycles = CpuExecute(dwCycles);
  cyclenum += dwExecutedCycles;
  cumulativecycles = g_nCumulativeCycles;

  VideoUpdateVbl(dwExecutedCycles);
  JoyUpdatePosition(dwExecutedCycles);
  SSCFrontend_Update(&sg_SSC, dwExecutedCycles);
  PrinterFrontend_Update(dwExecutedCycles);
  MB_UpdateCycles(dwExecutedCycles);
  SpkrUpdate(dwExecutedCycles);
  SpkrFrontend_Update(dwExecutedCycles);
  Linapple_KeyboardThink(dwExecutedCycles);

  return dwExecutedCycles;
}

uint32_t Linapple_RunFrame(uint32_t cycles) {
  if (g_state.mode == MODE_RUNNING) {
    // Debugger check
    if (IsDebugSteppingAtFullSpeed()) {
        DebugContinueStepping();
        // MB_EndOfVideoFrame(); // ???
        return 0; // Not sure how to count cycles here yet
    }

    uint32_t executed = Internal_RunCycles(cycles);
    MB_EndOfVideoFrame();
    
    // Check for video callback
    if (g_videoCB && g_bFrameReady) {
        uint32_t* output = VideoGetOutputBuffer();
        g_videoCB(output, 560, 384, 560 * 4);
        g_bFrameReady = false;
    }
    return executed;
  } else if (g_state.mode == MODE_STEPPING) {
    // SingleStep logic
    uint32_t dwExecutedCycles = CpuExecute(1);
    cyclenum += dwExecutedCycles;
    g_nCumulativeCycles += dwExecutedCycles;
    VideoUpdateVbl(dwExecutedCycles);
    UpdateDisplay(UPDATE_ALL);
    return dwExecutedCycles;
  } else if (g_state.mode == MODE_LOGO) {
    // DrawAppleContent();
  }
  return 0;
}
