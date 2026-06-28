// SPDX-License-Identifier: GPL-2.0-only
#include "core/LinAppleCore.h"

#include <strings.h>

#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Debugger/Debug.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/joystick/Joystick.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "apple2/peripherals/printer/Printer.h"
#include "apple2/peripherals/super_serial_card/SuperSerial.h"
#include "core/AudioMixer.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/Log.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/ProgramLoader.h"
#include "core/Asset.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-type-cstyle-cast,misc-include-cleaner,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,cppcoreguidelines-owning-memory,google-runtime-int,cppcoreguidelines-init-variables):
// Core emulator lifecycle, cycle accounting, and binary program file loading
using Logger::Error;
using Logger::Info;

namespace {

constexpr uint64_t cpu_test_max_cycles = 100000000;
constexpr int full_speed_disk_iterations = 100;

static LinappleVideoCallback_t g_video_cb = nullptr;
static LinappleTitleCallback_t g_title_cb = nullptr;

static uint32_t s_turbo_start_ms = 0;
static bool s_was_turbo = false;

}  // namespace

extern LinappleAudioCallback_t g_frontendAudioCB;
extern LinappleAudioCallback_t g_frontendMockAudioCB;

auto linapple_set_video_callback(LinappleVideoCallback_t cb) -> void {
  g_video_cb = cb;
}

auto linapple_set_audio_callback(LinappleAudioCallback_t cb) -> void {
  g_frontendAudioCB = cb;
}

auto linapple_set_mock_audio_callback(LinappleAudioCallback_t cb) -> void {
  g_frontendMockAudioCB = cb;
}

auto linapple_set_title_callback(LinappleTitleCallback_t cb) -> void {
  g_title_cb = cb;
}

auto linapple_update_title(const char* title) -> void {
  if (title == nullptr) {
    return;
  }
  if (g_title_cb != nullptr) {
    g_title_cb(title);
  }
}

auto linapple_get_ticks() -> uint32_t {
  static auto start_time = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time)
      .count();
}

static auto should_run_full_speed() -> bool {
  bool peripheral_active = Peripheral_IsAnyActive();
  bool should_turbo = peripheral_active && (g_state.needsprecision == 0);

  if (should_turbo && !s_was_turbo) {
    s_turbo_start_ms = linapple_get_ticks();
    Logger::Perf("Full-speed disk mode engaged\n");
  } else if (!should_turbo && s_was_turbo) {
    uint32_t elapsed = linapple_get_ticks() - s_turbo_start_ms;
    Logger::Perf("Full-speed disk mode disengaged after %ums\n", elapsed);
    audio_mixer_clear_buffers();
  }

  s_was_turbo = should_turbo;
  g_bFullSpeed = should_turbo;
  return should_turbo;
}

auto linapple_init() -> void {
  MemPreInitialize();
  Asset_Init();
  CreateColorMixMap();
  audio_mixer_initialize();

  MemInitialize();
  CpuInitialize();
  VideoInitialize();

  Peripheral_Manager_Init();
  Peripheral_Register_Internal();
}

auto linapple_register_peripherals() -> void { Peripheral_Register_Internal(); }

auto linapple_shutdown() -> void {
  Peripheral_Manager_Shutdown();
  Peripheral_Plugins_Shutdown();
  audio_mixer_destroy();
  VideoDestroy();
  MemDestroy();
  Asset_Quit();
}

auto linapple_cpu_test(const char* test_file, uint16_t trap_addr) -> void {
  if (test_file == nullptr) {
    return;
  }
  linapple_init();
  if (linapple_load_program(test_file) != 0) {
    Error("Failed to load test file: %s\n", test_file);
    return;
  }
  CpuGetRegisters()->pc = 0x0400;  // NMOS 6502 functional test entry
  uint64_t count = 0;
  while (count < cpu_test_max_cycles) {
    uint32_t executed = CpuExecute(1);
    if (executed == 0) {
      break;
    }
    cyclenum += executed;
    g_nCumulativeCycles += executed;
    count += executed;
    if (CpuGetRegisters()->pc == trap_addr) {
      printf("CPU trapped at 0x%04X after %" PRIu64 " cycles\n",
             CpuGetRegisters()->pc, count);
      break;
    }
  }
  linapple_shutdown();
}

auto linapple_load_program(const char* path) -> int {
  if (path == nullptr) {
    return static_cast<int>(PROGRAM_LOAD_NOT_A_PROGRAM);
  }
  auto res = ProgramLoader_TryLoad(path);
  if (res == PROGRAM_LOAD_OK) {
    return 0;
  }
  if (res != PROGRAM_LOAD_NOT_A_PROGRAM) {
    return static_cast<int>(res);
  }

  // Avoid trying to load known disk image formats as raw programs
  const char* ext = std::strrchr(path, '.');
  if (ext != nullptr) {
    static const char* disk_exts[] = {".woz", ".dsk", ".nib",
                                      ".2mg", ".po",  ".do"};
    for (auto d_ext : disk_exts) {
      if (strcasecmp(ext, d_ext) == 0) {
        return static_cast<int>(PROGRAM_LOAD_NOT_A_PROGRAM);
      }
    }
  }

  // Raw binary fallback
  FILE* f = std::fopen(path, "rb");
  if (f == nullptr) {
    return static_cast<int>(PROGRAM_LOAD_NOT_A_PROGRAM);
  }

  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);

  if (size <= 0 || size > 65536) {
    std::fclose(f);
    return static_cast<int>(PROGRAM_LOAD_NOT_A_PROGRAM);
  }

  uint16_t load_addr = (size == 65536) ? 0x0000 : 0x0800;

  if (static_cast<size_t>(load_addr) + static_cast<size_t>(size) > 65536) {
    std::fclose(f);
    return static_cast<int>(PROGRAM_LOAD_NOT_A_PROGRAM);
  }

  if (std::fread(mem + load_addr, 1, size, f) != static_cast<size_t>(size)) {
    std::fclose(f);
    return static_cast<int>(PROGRAM_LOAD_FILE_ERROR);
  }
  std::fclose(f);

  memset(memdirty, 0xFF, NUM_PAGES_48K);
  CpuGetRegisters()->pc = load_addr;
  return static_cast<int>(PROGRAM_LOAD_OK);
}

static auto internal_run_cycles(uint32_t dw_cycles) -> uint32_t {
  if (dw_cycles == 0) {
    return 0;
  }

  uint32_t executed_cycles = CpuExecute(dw_cycles);
  cyclenum += executed_cycles;
  cumulativecycles = g_nCumulativeCycles;

  Peripheral_Manager_Think(executed_cycles);
  VideoUpdateVbl(executed_cycles);

  return executed_cycles;
}

auto linapple_run_frame(uint32_t cycles) -> uint32_t {
  if (g_state.mode == MODE_RUNNING) {
    uint32_t executed = 0;
    if (should_run_full_speed()) {
      for (int i = 0; i < full_speed_disk_iterations; i++) {
        executed += internal_run_cycles(cycles);
        if (!Peripheral_IsAnyActive()) {
          break;
        }
      }
    } else {
      executed = internal_run_cycles(cycles);
    }

    Peripheral_Manager_OnVBlank(true);

    if (g_video_cb != nullptr && g_bFrameReady) {
      uint32_t* output = VideoGetOutputBuffer();
      g_video_cb(output, VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_WIDTH * 4);
      g_bFrameReady = false;
    }
    return executed;
  }
  return 0;
}

auto linapple_set_key_state(uint8_t apple_code, bool down) -> void {
  KeyboardEvent_t ev = {
      apple_code, static_cast<uint8_t>(down ? 1 : 0), 0, 0, 0, 0, {0, 0, 0}};
  Peripheral_Command(0, keyboard_cmd_event, &ev, sizeof(ev));
}

auto linapple_set_caps_lock_state(bool enabled) -> void {
  uint8_t caps = enabled ? 1 : 0;
  Peripheral_Command(0, keyboard_cmd_set_caps, &caps, 1);
}

auto linapple_set_apple_key(int key, bool down) -> void {
  KeyboardModifiers_t mods = {};
  size_t sz = sizeof(mods);
  Peripheral_Query(0, keyboard_query_mods, &mods, &sz);
  if (key == 0) {
    mods.gui = down ? 1U : 0U;
  } else {
    mods.alt = down ? 1U : 0U;
  }
  Peripheral_Command(0, keyboard_cmd_set_mods, &mods, sizeof(mods));
}

auto linapple_set_joystick_axis(int axis, int value) -> void {
  JoystickTrimPayload_t payload = {axis == 0, static_cast<int16_t>(value)};
  Peripheral_Command(0, JOY_CMD_SET_TRIM, &payload, sizeof(payload));
}

auto linapple_set_joystick_button(int button, bool down) -> void {
  JoystickButtonPayload_t payload = {static_cast<uint8_t>(button), down};
  Peripheral_Command(0, JOY_CMD_SET_BUTTON, &payload, sizeof(payload));
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-type-cstyle-cast,misc-include-cleaner,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,cppcoreguidelines-owning-memory,google-runtime-int,cppcoreguidelines-init-variables)
