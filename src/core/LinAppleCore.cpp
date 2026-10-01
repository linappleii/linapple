// SPDX-License-Identifier: GPL-2.0-only
#include "core/LinAppleCore.h"

#include <strings.h>

#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/Asset.h"
#include "core/BasicLiveSync.h"
#include "core/Log.h"
#include "core/ProgramLoader.h"
#include "core/Registry.h"

using Logger::error;

const char* app_title = title_apple_2e_enhanced;

eApple2Type current_apple2_type = A2TYPE_APPLE2EENHANCED;
eApple2Language current_language = A2LANG_US;

uint32_t emul_msec = 0;
bool full_speed = false;
bool hdd_enabled = false;

SystemState_t system_state = {app_mode_logo,
                              false,
                              false,
                              emulation_speed_normal,
                              SCREEN_WIDTH,
                              SCREEN_HEIGHT,
                              false,
                              {""},
                              {""},
                              {""},
                              {""},
                              {""},
                              {""},
                              {""},
                              {"anonymous:mymail@hotmail.com"},
                              {""},
                              true,
                              17030,
                              false};

double current_clk_6502 = CLOCK_6502;

auto get_title_apple_2() noexcept -> const char* { return title_apple_2; }
auto get_title_apple_2_plus() noexcept -> const char* {
  return title_apple_2_plus;
}
auto get_title_apple_2e() noexcept -> const char* { return title_apple_2e; }
auto get_title_apple_2e_enhanced() noexcept -> const char* {
  return title_apple_2e_enhanced;
}

namespace {

constexpr uint64_t cpu_test_max_cycles = 100000000;
constexpr int full_speed_disk_iterations = 100;

static LinappleVideoCallback_t video_cb = nullptr;
static LinappleTitleCallback_t title_cb = nullptr;

static uint32_t turbo_start_ms = 0;
static bool was_turbo = false;
static bool user_turbo = false;
static bool disk_turbo_enabled = true;

auto is_disk_turbo() -> bool {
  return disk_turbo_enabled && peripheral_is_any_active();
}

auto is_user_turbo() -> bool {
  return user_turbo || (system_state.speed >= emulation_speed_max);
}

auto should_run_full_speed() -> bool {
  bool disk_turbo = is_disk_turbo();
  bool user_turbo_active = is_user_turbo();
  bool should_turbo = disk_turbo || user_turbo_active;

  if (should_turbo && !was_turbo) {
    turbo_start_ms = linapple_get_ticks();
    Logger::perf("Full-speed mode engaged (disk=%d, user=%d)\n",
                 disk_turbo ? 1 : 0, user_turbo_active ? 1 : 0);
  } else if (!should_turbo && was_turbo) {
    uint32_t elapsed = linapple_get_ticks() - turbo_start_ms;
    Logger::perf("Full-speed mode disengaged after %ums\n", elapsed);
  }

  was_turbo = should_turbo;
  full_speed = should_turbo;
  return should_turbo;
}

auto extension_matches_list(const char* ext, const char* list) -> bool {
  if (ext == nullptr || list == nullptr || *list == '\0') {
    return false;
  }
  if (*ext == '.') {
    ++ext;
  }
  const size_t ext_len = std::strlen(ext);
  if (ext_len == 0) {
    return false;
  }
  const char* p = list;
  while (*p != '\0') {
    while (*p == ';' || *p == ' ' || *p == ',') {
      ++p;
    }
    if (*p == '\0') {
      break;
    }
    const char* start = p;
    while (*p != '\0' && *p != ';' && *p != ' ' && *p != ',') {
      ++p;
    }
    const size_t len = static_cast<size_t>(p - start);
    if (len == ext_len && strncasecmp(ext, start, len) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

extern FrontendAudioChannelCallback_t frontend_audio_channel_cb;
extern FrontendAudioSourceRegisterCallback_t frontend_audio_register_cb;
extern FrontendAudioSourceUnregisterCallback_t frontend_audio_unregister_cb;

auto linapple_set_video_callback(LinappleVideoCallback_t cb) -> void {
  video_cb = cb;
}

auto linapple_set_audio_channel_callback(FrontendAudioChannelCallback_t cb)
    -> void {
  frontend_audio_channel_cb = cb;
}

auto linapple_set_audio_source_register_callback(
    FrontendAudioSourceRegisterCallback_t cb) -> void {
  frontend_audio_register_cb = cb;
  // A late subscriber is the normal case, not the exception: every frontend
  // installs this callback from its audio init, which runs after
  // app_controller_initialize has already registered the internal speaker
  // and whatever is in the slots. Replaying the current state is what makes
  // the init order stop mattering.
  if (cb != nullptr) {
    peripheral_announce_audio_sources();
  }
}

auto linapple_set_audio_source_unregister_callback(
    FrontendAudioSourceUnregisterCallback_t cb) -> void {
  frontend_audio_unregister_cb = cb;
}

auto linapple_set_title_callback(LinappleTitleCallback_t cb) -> void {
  title_cb = cb;
}

auto linapple_update_title(const char* title) -> void {
  if (title == nullptr) {
    return;
  }
  if (title_cb != nullptr) {
    title_cb(title);
  }
}

auto linapple_get_ticks() noexcept -> uint32_t {
  static auto start_time = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time)
      .count();
}

auto linapple_init() -> int {
  uint32_t disk_turbo = 1;
  config_load_int("Configuration", "Disk Turbo", &disk_turbo);
  disk_turbo_enabled = (disk_turbo != 0);

  mem_pre_initialize();
  if (!asset_init()) {
    linapple_shutdown();
    return -1;
  }
  video_create_color_mix_map();

  if (mem_initialize() != 0) {
    linapple_shutdown();
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
  basic_sync_shutdown();
  peripheral_manager_shutdown();
  peripheral_plugins_shutdown();
  video_destroy();
  mem_destroy();
  asset_quit();
}

auto linapple_reset_hard() -> void {
  mem_reset();
  video_reset_state();
  cpu_reset();
  peripheral_manager_reset();
}

auto linapple_reset_soft() -> void {
  cpu_reset();
  if (!is_apple2()) {
    mem_reset_paging();
  }
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
    cpu_add_cumulative_cycles(executed);
    count += executed;
    if (cpu_get_registers()->pc == trap_addr) {
      printf("CPU trapped at 0x%04X after %" PRIu64 " cycles\n",
             cpu_get_registers()->pc, count);
      break;
    }
  }
  if (cpu_get_registers()->pc != trap_addr) {
    printf("CPU DID NOT TRAP. final pc=0x%04X after %" PRIu64 " cycles\n",
           cpu_get_registers()->pc, count);
  }
  linapple_shutdown();
}

auto linapple_get_supported_disk_extensions(int slot, char* out_buffer,
                                            size_t buffer_size) -> size_t {
  if (out_buffer == nullptr || buffer_size == 0) {
    return 0;
  }
  out_buffer[0] = '\0';
  size_t exts_size = buffer_size;
  if (slot == 7) {
    if (peripheral_query(7, harddisk_query_supported_extensions, out_buffer,
                         &exts_size) == peripheral_ok) {
      return std::strlen(out_buffer);
    }
    auto* p = peripheral_find_internal("linapple.harddisk");
    if (p != nullptr && p->query != nullptr) {
      if (p->query(nullptr, harddisk_query_supported_extensions, out_buffer,
                   &exts_size) == peripheral_ok) {
        return std::strlen(out_buffer);
      }
    }
  } else {
    int target_slot = (slot > 0) ? slot : disk_default_slot;
    if (peripheral_query(target_slot, disk_query_supported_extensions,
                         out_buffer, &exts_size) == peripheral_ok) {
      return std::strlen(out_buffer);
    }
    auto* p = peripheral_find_internal("linapple.disk_II");
    if (p != nullptr && p->query != nullptr) {
      if (p->query(nullptr, disk_query_supported_extensions, out_buffer,
                   &exts_size) == peripheral_ok) {
        return std::strlen(out_buffer);
      }
    }
  }
  return 0;
}

auto linapple_is_supported_disk_image(const char* path) -> bool {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  const char* ext = std::strrchr(path, '.');
  if (ext == nullptr) {
    return false;
  }
  constexpr size_t buf_size = 256;
  char floppy_exts[buf_size] = {};
  linapple_get_supported_disk_extensions(disk_default_slot, floppy_exts,
                                         sizeof(floppy_exts));
  if (extension_matches_list(ext, floppy_exts)) {
    return true;
  }
  char hdd_exts[buf_size] = {};
  linapple_get_supported_disk_extensions(7, hdd_exts, sizeof(hdd_exts));
  return extension_matches_list(ext, hdd_exts);
}

auto linapple_load_program(const char* path) -> int {
  if (path == nullptr || path[0] == '\0') {
    return static_cast<int>(program_load_not_a_program);
  }

  if (linapple_is_supported_disk_image(path)) {
    return static_cast<int>(program_load_not_a_program);
  }

  auto res = program_loader_try_load(path);
  if (res == program_load_ok) {
    return 0;
  }
  if (res != program_load_not_a_program) {
    return static_cast<int>(res);
  }

  auto raw_res = program_loader_load_raw(path, 0x0800);
  if (raw_res == program_load_ok) {
    return 0;
  }
  return static_cast<int>(raw_res);
}

static auto internal_run_cycles(uint32_t cycles) -> uint32_t {
  if (cycles == 0) {
    return 0;
  }

  uint32_t executed_cycles = cpu_execute(cycles);

  peripheral_manager_think(executed_cycles);
  video_update_vbl(executed_cycles);

  return executed_cycles;
}

static auto run_frame_cycles(uint32_t cycles) -> uint32_t {
  if (!should_run_full_speed()) {
    return internal_run_cycles(cycles);
  }

  const bool disk_turbo = is_disk_turbo();
  uint32_t executed = 0;
  for (int i = 0; i < full_speed_disk_iterations; ++i) {
    executed += internal_run_cycles(cycles);
    if (disk_turbo && !peripheral_is_any_active()) {
      break;
    }
  }
  return executed;
}

auto linapple_run_frame(uint32_t cycles) -> uint32_t {
  if (system_state.mode != app_mode_running) {
    return 0;
  }

  uint32_t executed = run_frame_cycles(cycles);

  peripheral_manager_on_vblank(true);
  basic_sync_update();

  if (video_cb != nullptr && video_is_frame_ready()) {
    uint32_t* output = video_get_output_buffer();
    video_cb(output, video_width, video_height, video_width * 4);
    video_clear_frame_ready();
  }
  return executed;
}

auto linapple_get_speed() noexcept -> uint32_t { return system_state.speed; }

auto linapple_set_speed(uint32_t speed) noexcept -> void {
  if (speed > emulation_speed_max) {
    speed = emulation_speed_max;
  }
  system_state.speed = speed;
}

auto linapple_speed_increase() noexcept -> uint32_t {
  uint32_t next_speed = system_state.speed + 2;
  if (next_speed > emulation_speed_max) {
    next_speed = emulation_speed_max;
  }
  system_state.speed = next_speed;
  return system_state.speed;
}

auto linapple_speed_decrease() noexcept -> uint32_t {
  if (system_state.speed > emulation_speed_min) {
    system_state.speed -= 1;
  }
  return system_state.speed;
}

auto linapple_speed_reset() noexcept -> uint32_t {
  system_state.speed = emulation_speed_normal;
  return system_state.speed;
}

auto linapple_get_frame_cycles() noexcept -> uint32_t {
  uint32_t base_cycles =
      (system_state.clks_per_frame > 0) ? system_state.clks_per_frame : 17030;
  if (system_state.speed == emulation_speed_normal) {
    return base_cycles;
  }
  double multiplier = 1.0;
  if (system_state.speed < emulation_speed_normal) {
    multiplier = 0.5 + static_cast<double>(system_state.speed) * 0.05;
  } else {
    multiplier = static_cast<double>(system_state.speed) / 10.0;
  }
  return static_cast<uint32_t>(static_cast<double>(base_cycles) * multiplier);
}

auto linapple_get_turbo() noexcept -> bool { return user_turbo; }

auto linapple_set_turbo(bool turbo) noexcept -> void { user_turbo = turbo; }

auto linapple_toggle_turbo() noexcept -> bool {
  user_turbo = !user_turbo;
  return user_turbo;
}

auto linapple_is_full_speed() noexcept -> bool { return full_speed; }

auto linapple_get_app_title() noexcept -> const char* { return app_title; }

auto linapple_get_clock_hz() noexcept -> double { return current_clk_6502; }

auto linapple_get_apple2_type() noexcept -> Apple2Type_t {
  return current_apple2_type;
}

auto linapple_set_apple2_type(Apple2Type_t type) noexcept -> void {
  current_apple2_type = type;
}

auto linapple_get_language() noexcept -> Apple2Language_t {
  return current_language;
}

auto linapple_set_language(Apple2Language_t lang) noexcept -> void {
  current_language = lang;
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
