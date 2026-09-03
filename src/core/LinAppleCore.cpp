// SPDX-License-Identifier: GPL-2.0-only
#include "core/LinAppleCore.h"

// Core emulator lifecycle, cycle accounting, and binary program file loading
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-type-cstyle-cast, misc-include-cleaner,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays,
// cppcoreguidelines-owning-memory, google-runtime-int,
// cppcoreguidelines-init-variables,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// clang-diagnostic-missing-braces)

#include <curl/curl.h>
#include <strings.h>

#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if ENABLE_DEBUGGER
#include "Debugger/Debug.h"
#endif
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/Video.h"
#include "apple2/peripherals/joystick/Joystick.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "apple2/peripherals/mockingboard/Mockingboard.h"
#include "apple2/peripherals/printer/Printer.h"
#include "apple2/peripherals/super_serial_card/SuperSerial.h"
#include "core/Asset.h"
#include "core/AudioMixer.h"
#include "core/BasicLiveSync.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "core/ProgramLoader.h"
#include "core/Util_Path.h"

using Logger::error;
using Logger::info;

static const char TITLE_APPLE_2_[] = "Apple ][ Emulator";
static const char TITLE_APPLE_2_PLUS_[] = "Apple ][+ Emulator";
static const char TITLE_APPLE_2E_[] = "Apple //e Emulator";
static const char TITLE_APPLE_2E_ENHANCED_[] = "Enhanced Apple //e Emulator";

const char* g_app_title = TITLE_APPLE_2E_ENHANCED_;
char videoDriverName[video_driver_name_max_len]{};

eApple2Type g_apple2_type = A2TYPE_APPLE2EENHANCED;
eApple2Language g_language = A2LANG_US;

uint64_t cumulative_cycles = 0;
uint64_t cycle_num = 0;
uint32_t emul_msec = 0;
bool g_full_speed = false;
bool hdd_enabled = false;

SystemState_t g_state = {MODE_LOGO,
                         false,
                         false,
                         SPEED_NORMAL,
                         SCREEN_WIDTH,
                         SCREEN_HEIGHT,
                         false,
                         0,
                         {""},
                         {""},
                         {""},
                         {""},
                         {"Printer.txt"},
                         {""},
                         {""},
                         {""},
                         {"anonymous:mymail@hotmail.com"},
                         {""},
                         true,
                         17030,
                         false};

double g_current_clk_6502 = CLOCK_6502;
int g_cpu_cycles_feedback = 0;
uint32_t g_cycles_this_frame = 0;

bool g_disable_direct_sound = false;

uint32_t g_slot4 = CT_Mockingboard;
CURL* g_curl = nullptr;

auto get_title_apple_2() -> const char* { return TITLE_APPLE_2_; }
auto get_title_apple_2_plus() -> const char* { return TITLE_APPLE_2_PLUS_; }
auto get_title_apple_2e() -> const char* { return TITLE_APPLE_2E_; }
auto get_title_apple_2e_enhanced() -> const char* {
  return TITLE_APPLE_2E_ENHANCED_;
}

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
  bool peripheral_active = peripheral_is_any_active();
  bool should_turbo = peripheral_active && (g_state.needsprecision == 0);

  if (should_turbo && !s_was_turbo) {
    s_turbo_start_ms = linapple_get_ticks();
    Logger::perf("Full-speed disk mode engaged\n");
  } else if (!should_turbo && s_was_turbo) {
    uint32_t elapsed = linapple_get_ticks() - s_turbo_start_ms;
    Logger::perf("Full-speed disk mode disengaged after %ums\n", elapsed);
    audio_mixer_clear_buffers();
  }

  s_was_turbo = should_turbo;
  g_full_speed = should_turbo;
  return should_turbo;
}

auto linapple_init() -> int {
  mem_pre_initialize();
  if (!asset_init()) {
    return -1;
  }
  video_create_color_mix_map();
  audio_mixer_initialize();

  if (mem_initialize() != 0) {
    return -1;
  }
  cpu_initialize();
  video_initialize();

  peripheral_manager_init();
  peripheral_register_internal();
  return 0;
}

auto linapple_register_peripherals() -> void { peripheral_register_internal(); }

auto linapple_shutdown() -> void {
  peripheral_manager_shutdown();
  peripheral_plugins_shutdown();
  audio_mixer_destroy();
  video_destroy();
  mem_destroy();
  asset_quit();
}

auto linapple_cpu_test(const char* test_file, uint16_t trap_addr) -> void {
  if (test_file == nullptr) {
    return;
  }
  if (linapple_init() != 0) {
    error("Failed to initialize core for CPU test\n");
    return;
  }
  if (linapple_load_program(test_file) != 0) {
    error("Failed to load test file: %s\n", test_file);
    return;
  }
  cpu_get_registers()->pc = 0x0400;  // NMOS 6502 functional test entry
  uint64_t count = 0;
  while (count < cpu_test_max_cycles) {
    uint32_t executed = cpu_execute(1);
    if (executed == 0) {
      break;
    }
    cycle_num += executed;
    g_cumulative_cycles += executed;
    count += executed;
    if (cpu_get_registers()->pc == trap_addr) {
      printf("CPU trapped at 0x%04X after %" PRIu64 " cycles\n",
             cpu_get_registers()->pc, count);
      break;
    }
  }
  linapple_shutdown();
}

auto linapple_load_program(const char* path) -> int {
  if (path == nullptr) {
    return static_cast<int>(program_load_not_a_program);
  }
  auto res = program_loader_try_load(path);
  if (res == program_load_ok) {
    return 0;
  }
  if (res != program_load_not_a_program) {
    return static_cast<int>(res);
  }

  // Avoid trying to load known disk image formats as raw programs
  const char* ext = std::strrchr(path, '.');
  if (ext != nullptr) {
    static const char* disk_exts[] = {".woz", ".dsk", ".nib",
                                      ".2mg", ".po",  ".do"};
    for (auto d_ext : disk_exts) {
      if (strcasecmp(ext, d_ext) == 0) {
        return static_cast<int>(program_load_not_a_program);
      }
    }
  }

  // Raw binary fallback
  FilePtr_t f{std::fopen(path, "rb"), std::fclose};
  if (!f) {
    return static_cast<int>(program_load_not_a_program);
  }

  int64_t size = Path::file_size(f.get());
  if (size <= 0 || size > 65536) {
    return static_cast<int>(program_load_not_a_program);
  }

  uint16_t load_addr = (size == 65536) ? 0x0000 : 0x0800;

  if (static_cast<size_t>(load_addr) + static_cast<size_t>(size) > 65536) {
    return static_cast<int>(program_load_not_a_program);
  }

  if (std::fread(mem + load_addr, 1, static_cast<size_t>(size), f.get()) !=
      static_cast<size_t>(size)) {
    return static_cast<int>(program_load_file_error);
  }

  memset(memdirty, 0xFF, NUM_PAGES_48K);
  cpu_get_registers()->pc = load_addr;
  return static_cast<int>(program_load_ok);
}

static auto internal_run_cycles(uint32_t dw_cycles) -> uint32_t {
  if (dw_cycles == 0) {
    return 0;
  }

  uint32_t executed_cycles = cpu_execute(dw_cycles);
  cycle_num += executed_cycles;
  cumulative_cycles = g_cumulative_cycles;

  peripheral_manager_think(executed_cycles);
  video_update_vbl(executed_cycles);

  return executed_cycles;
}

auto linapple_run_frame(uint32_t cycles) -> uint32_t {
  if (g_state.mode == MODE_RUNNING) {
    uint32_t executed = 0;
    if (should_run_full_speed()) {
      for (int i = 0; i < full_speed_disk_iterations; i++) {
        executed += internal_run_cycles(cycles);
        if (!peripheral_is_any_active()) {
          break;
        }
      }
    } else {
      executed = internal_run_cycles(cycles);
    }

    peripheral_manager_on_vblank(true);
    basic_sync_update();

    if (g_video_cb != nullptr && g_frame_ready) {
      uint32_t* output = video_get_output_buffer();
      g_video_cb(output, video_width, video_height, video_width * 4);
      g_frame_ready = false;
    }
    return executed;
  }
  return 0;
}

auto linapple_set_key_state(uint8_t apple_code, bool down) -> void {
  KeyboardEvent_t ev = {
      apple_code, static_cast<uint8_t>(down ? 1 : 0), 0, 0, 0, 0, {0, 0, 0}};
  peripheral_command(0, keyboard_cmd_event, &ev, sizeof(ev));
}

auto linapple_set_caps_lock_state(bool enabled) -> void {
  uint8_t caps = enabled ? 1 : 0;
  peripheral_command(0, keyboard_cmd_set_caps, &caps, 1);
  peripheral_manager_think(0);
}

auto linapple_get_caps_lock_state() -> bool {
  KeyboardModifiers_t mods = {};
  size_t sz = sizeof(mods);
  peripheral_query(0, keyboard_query_mods, &mods, &sz);
  return mods.caps != 0;
}

auto linapple_toggle_caps_lock_state() -> bool {
  bool new_state = !linapple_get_caps_lock_state();
  linapple_set_caps_lock_state(new_state);
  return new_state;
}

auto linapple_set_apple_key(int key, bool down) -> void {
  KeyboardModifiers_t mods = {};
  size_t sz = sizeof(mods);
  peripheral_query(0, keyboard_query_mods, &mods, &sz);
  if (key == 0) {
    mods.gui = down ? 1U : 0U;
  } else {
    mods.alt = down ? 1U : 0U;
  }
  peripheral_command(0, keyboard_cmd_set_mods, &mods, sizeof(mods));
}

auto linapple_set_joystick_axis(int axis, int value) -> void {
  JoystickTrimPayload_t payload = {axis == 0, static_cast<int16_t>(value)};
  peripheral_command(0, JOY_CMD_SET_TRIM, &payload, sizeof(payload));
}

auto linapple_set_joystick_button(int button, bool down) -> void {
  JoystickButtonPayload_t payload = {static_cast<uint8_t>(button), down};
  peripheral_command(0, JOY_CMD_SET_BUTTON, &payload, sizeof(payload));
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-type-cstyle-cast, misc-include-cleaner,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays,
// cppcoreguidelines-owning-memory, google-runtime-int,
// cppcoreguidelines-init-variables,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// clang-diagnostic-missing-braces)
