/*
linapple : An Apple //e emulator for Linux

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski, Nick Westgate

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "core/LinAppleCore.h"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "Debugger/Debug.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SaveState.h"
#include "apple2/SoundCore.h"
#include "apple2/Video.h"
#include "apple2/peripherals/joystick/Joystick.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "apple2/peripherals/printer/Printer.h"
#include "apple2/peripherals/super_serial_card/SuperSerial.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/Log.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/ProgramLoader.h"
#include "core/asset.h"

using Logger::Error;
using Logger::Info;

// Constants from original implementation
const int APPLE_MEM_SIZE = 65536;
const uint16_t CPU_TEST_START_PC = 0x0800;
const uint16_t CPU_TEST_TRAP_PC = 0x3469;  // Example trap PC
const uint64_t CPU_TEST_MAX_CYCLES = 100000000;
const int FULL_SPEED_DISK_ITERATIONS = 100;

// Repetition logic moved to Keyboard peripheral.

// Callbacks
static LinappleVideoCallback g_videoCB = nullptr;
static LinappleTitleCallback g_titleCB = nullptr;
extern LinappleAudioCallback g_frontendAudioCB;
extern LinappleAudioCallback g_frontendMockAudioCB;

static uint32_t s_turboStartMs = 0;
static bool s_wasTurbo = false;

void Linapple_SetVideoCallback(LinappleVideoCallback cb) { g_videoCB = cb; }
void Linapple_SetAudioCallback(LinappleAudioCallback cb) {
  g_frontendAudioCB = cb;
}
void Linapple_SetMockAudioCallback(LinappleAudioCallback cb) {
  g_frontendMockAudioCB = cb;
}
void Linapple_SetTitleCallback(LinappleTitleCallback cb) { g_titleCB = cb; }

void Linapple_UpdateTitle(const char* title) {
  if (g_titleCB) {
    g_titleCB(title);
  }
}

auto Linapple_GetTicks() -> uint32_t {
  static auto start_time = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time)
      .count();
}

static auto ShouldRunFullSpeed() -> bool {
  bool peripheral_active = Peripheral_IsAnyActive();

  bool shouldTurbo = peripheral_active && (g_state.needsprecision == 0);

  if (shouldTurbo && !s_wasTurbo) {
    s_turboStartMs = Linapple_GetTicks();
    Logger::Perf("Full-speed disk mode engaged\n");
  } else if (!shouldTurbo && s_wasTurbo) {
    uint32_t elapsed = Linapple_GetTicks() - s_turboStartMs;
    Logger::Perf("Full-speed disk mode disengaged after %ums\n", elapsed);
  }

  s_wasTurbo = shouldTurbo;
  g_bFullSpeed = shouldTurbo;
  return shouldTurbo;
}

void Linapple_Init() {
  MemPreInitialize();
  Asset_Init();
  CreateColorMixMap();
  SoundCore_Initialize();

  MemInitialize();
  CpuInitialize();
  VideoInitialize();

  Peripheral_Manager_Init();
  Peripheral_Register_Internal();
}

void Linapple_RegisterPeripherals() { Peripheral_Register_Internal(); }

void Linapple_Shutdown() {
  Peripheral_Manager_Shutdown();
  Peripheral_Plugins_Shutdown();
  SoundCore_Destroy();
  VideoDestroy();
  MemDestroy();
  Asset_Quit();
}

void Linapple_CpuTest(const char* szTestFile, uint16_t trap_addr) {
  Linapple_Init();
  if (Linapple_LoadProgram(szTestFile) != 0) {
    Error("Failed to load test file: %s\n", szTestFile);
    return;
  }
  regs.pc = 0x0400;  // NMOS 6502 functional test entry
  uint64_t count = 0;
  while (count < CPU_TEST_MAX_CYCLES) {
    uint32_t executed = CpuExecute(1);
    cyclenum += executed;
    g_nCumulativeCycles += executed;
    count += executed;
    if (regs.pc == trap_addr) {
      printf("CPU trapped at 0x%04X after %" PRIu64 " cycles\n", regs.pc,
             count);
      break;
    }
  }
  Linapple_Shutdown();
}

int Linapple_LoadProgram(const char* path) {
  auto res = ProgramLoader_TryLoad(path);
  if (res == PROGRAM_LOAD_OK) return 0;
  if (res != PROGRAM_LOAD_NOT_A_PROGRAM) return static_cast<int>(res);

  // Avoid trying to load known disk image formats as raw programs
  const char* ext = strrchr(path, '.');
  if (ext) {
    static const char* disk_exts[] = {".woz", ".dsk", ".nib",
                                      ".2mg", ".po",  ".do"};
    for (auto d_ext : disk_exts) {
      if (strcasecmp(ext, d_ext) == 0)
        return static_cast<int>(PROGRAM_LOAD_NOT_A_PROGRAM);
    }
  }

  // Raw binary fallback
  FILE* f = fopen(path, "rb");
  if (!f) return static_cast<int>(PROGRAM_LOAD_NOT_A_PROGRAM);

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (size <= 0 || size > 65536) {
    fclose(f);
    return static_cast<int>(PROGRAM_LOAD_NOT_A_PROGRAM);
  }

  uint16_t load_addr = (size == 65536) ? 0x0000 : 0x800;

  // Defensive check: ensure the file fits in the remaining memory buffer to
  // prevent overflow.
  if (static_cast<size_t>(load_addr) + static_cast<size_t>(size) > 65536) {
    fclose(f);
    return static_cast<int>(PROGRAM_LOAD_NOT_A_PROGRAM);
  }

  if (fread(mem + load_addr, 1, size, f) != static_cast<size_t>(size)) {
    fclose(f);
    return static_cast<int>(PROGRAM_LOAD_FILE_ERROR);
  }
  fclose(f);

  memset(memdirty, 0xFF, NUM_PAGES_48K);
  regs.pc = load_addr;
  return static_cast<int>(PROGRAM_LOAD_OK);
}

static auto Internal_RunCycles(uint32_t dwCycles) -> uint32_t {
  if (dwCycles == 0) return 0;

  uint32_t dwExecutedCycles = CpuExecute(dwCycles);
  cyclenum += dwExecutedCycles;
  cumulativecycles = g_nCumulativeCycles;

  Peripheral_Manager_Think(dwExecutedCycles);

  VideoUpdateVbl(dwExecutedCycles);

  return dwExecutedCycles;
}

auto Linapple_RunFrame(uint32_t cycles) -> uint32_t {
  if (g_state.mode == MODE_RUNNING) {
    uint32_t executed = 0;
    if (ShouldRunFullSpeed()) {
      for (int i = 0; i < FULL_SPEED_DISK_ITERATIONS; i++) {
        executed += Internal_RunCycles(cycles);
        if (!Peripheral_IsAnyActive()) break;
      }
    } else {
      executed = Internal_RunCycles(cycles);
    }

    Peripheral_Manager_OnVBlank(true);

    if (g_videoCB && g_bFrameReady) {
      uint32_t* output = VideoGetOutputBuffer();
      g_videoCB(output, VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_WIDTH * 4);
      g_bFrameReady = false;
    }
    return executed;
  }
  return 0;
}

void Linapple_SetKeyState(uint8_t apple_code, bool down) {
  KeyboardEvent_t ev = {apple_code, (uint8_t)(down ? 1 : 0)};
  Peripheral_Command(0, keyb_cmd_event, &ev, sizeof(ev));
}

void Linapple_SetCapsLockState(bool enabled) {
  uint8_t caps = enabled ? 1 : 0;
  Peripheral_Command(0, keyb_cmd_set_caps, &caps, 1);
}

void Linapple_SetAppleKey(int key, bool down) {
  // Read current modifier state first so we only toggle the one apple key.
  KeyboardModifiers_t mods = {};
  size_t sz = sizeof(mods);
  Peripheral_Query(0, keyb_query_mods, &mods, &sz);
  if (key == 0) {
    mods.gui = down ? 1U : 0U;
  } else {
    mods.alt = down ? 1U : 0U;
  }
  Peripheral_Command(0, keyb_cmd_set_mods, &mods, sizeof(mods));
}

void Linapple_SetJoystickAxis(int axis, int value) {
  JoystickTrimPayload_t payload = {axis == 0, static_cast<int16_t>(value)};
  Peripheral_Command(0, JOY_CMD_SET_TRIM, &payload, sizeof(payload));
}

void Linapple_SetJoystickButton(int button, bool down) {
  JoystickButtonPayload_t payload = {static_cast<uint8_t>(button), down};
  Peripheral_Command(0, JOY_CMD_SET_BUTTON, &payload, sizeof(payload));
}
