// SPDX-License-Identifier: GPL-2.0-only
#include "core/LinAppleCore.h"

#include <strings.h>

#include <algorithm>

#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SwitchInputs.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/Asset.h"
#include "core/BasicLiveSync.h"
#include "core/Log.h"
#include "core/ProgramLoader.h"
#include "core/Registry.h"

using Logger::error;

const char* app_title = title_apple_2e_enhanced;

Apple2Type current_apple2_type = A2TYPE_APPLE2EENHANCED;
Apple2Language current_language = A2LANG_US;

uint32_t emul_msec = 0;
bool full_speed = false;

constexpr uint32_t default_cycles_per_frame = 17030;
constexpr int bytes_per_pixel = 4;
constexpr double speed_subnormal_base = 0.5;
constexpr double speed_subnormal_scale = 0.05;
constexpr double speed_normal_divisor = 10.0;

SystemState system_state = {
    app_mode_logo,
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
    default_cycles_per_frame,
    false,
};

double current_clk_6502 = clock_6502;

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

LinappleVideoCallback video_cb = nullptr;
LinappleTitleCallback title_cb = nullptr;

uint32_t turbo_start_ms = 0;
bool was_turbo = false;
bool user_turbo = false;
bool disk_turbo_enabled = true;

auto is_disk_turbo() -> bool {
  return disk_turbo_enabled && peripheral_is_any_active();
}

auto is_user_turbo() -> bool {
  return user_turbo || (system_state.speed >= emulation_speed_max);
}

auto should_run_full_speed() -> bool {
  const bool disk_turbo = is_disk_turbo();
  const bool user_turbo_active = is_user_turbo();
  const bool should_turbo = disk_turbo || user_turbo_active;

  if (should_turbo && !was_turbo) {
    turbo_start_ms = linapple_get_ticks();
    Logger::perf("Full-speed mode engaged (disk=%d, user=%d)\n",
                 disk_turbo ? 1 : 0, user_turbo_active ? 1 : 0);
  } else if (!should_turbo && was_turbo) {
    const uint32_t elapsed = linapple_get_ticks() - turbo_start_ms;
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

auto linapple_set_video_callback(LinappleVideoCallback cb) -> void {
  video_cb = cb;
}

auto linapple_set_audio_channel_callback(FrontendAudioChannelCallback cb)
    -> void {
  frontend_audio_channel_cb = cb;
}

auto linapple_set_audio_source_register_callback(
    FrontendAudioSourceRegisterCallback cb) -> void {
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
    FrontendAudioSourceUnregisterCallback cb) -> void {
  frontend_audio_unregister_cb = cb;
}

auto linapple_set_title_callback(LinappleTitleCallback cb) -> void {
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

  // The resting levels depend on the model and on whether a keyboard hangs on
  // the connector, which the bridge knows and no card does; a //e with none
  // reads PB0 and PB1 high and self-tests at reset (IIe Technical Note #9).
  const bool keyboard_present = peripheral_present(0, "linapple.keyboard");
  switch_inputs_reset_configuration(!is_apple2(), keyboard_present);
  if (!is_apple2() && !keyboard_present) {
    Logger::warning(
        "no keyboard card: the //e runs with its keyboard unplugged and "
        "self-tests unless a game controller is configured\n");
  }
  return 0;
}

auto linapple_register_peripherals() -> void { peripheral_register_internal(); }

auto linapple_request_card_for_run(const char* id) -> void {
  peripheral_request_card_for_run(id);
}

auto linapple_requested_slot() -> int { return peripheral_requested_slot(); }

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
  // The //e MMU has no RESET' pin. It recognises the 6502's reset fingerprint,
  // three page-1 accesses then $FFFC, and turns every soft switch off as that
  // fetch begins, so the vector always comes out of ROM even with high RAM
  // read-enabled (Sather, Understanding the Apple IIe, 4-14 to 4-15 and
  // 5-23). The 16K RAM card has no such hook: RESET' at its slot changes
  // nothing, and a II Plus fetches the vector from whatever is read-enabled
  // (Sather, Understanding the Apple II, 5-28 and 5-30).
  if (!is_apple2()) {
    mem_reset_paging();
  }
  cpu_reset();
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
  // Whatever card holds the slot answers for it, under either card's query id;
  // a slot whose card answers neither falls back to the descriptors, which
  // list what either card could mount.
  for (const uint32_t query_id : {
           static_cast<uint32_t>(disk_query_supported_extensions),
           static_cast<uint32_t>(harddisk_query_supported_extensions),
       }) {
    exts_size = buffer_size;
    if (peripheral_query(slot, query_id, out_buffer, &exts_size) ==
        peripheral_ok) {
      return std::strlen(out_buffer);
    }
  }
  out_buffer[0] = '\0';
  for (const char* id : {"linapple.disk_II", "linapple.harddisk"}) {
    Peripheral_t* descriptor = peripheral_find_internal(id);
    if (descriptor == nullptr || descriptor->query == nullptr) {
      continue;
    }
    const uint32_t query_id =
        (std::strcmp(id, "linapple.disk_II") == 0)
            ? static_cast<uint32_t>(disk_query_supported_extensions)
            : static_cast<uint32_t>(harddisk_query_supported_extensions);
    char list[256] = {};
    exts_size = sizeof(list);
    if (descriptor->query(nullptr, query_id, list, &exts_size) !=
        peripheral_ok) {
      continue;
    }
    const size_t used = std::strlen(out_buffer);
    if (used > 0 && used + 1 < buffer_size) {
      out_buffer[used] = ';';
      out_buffer[used + 1] = '\0';
    }
    std::strncat(out_buffer, list, buffer_size - std::strlen(out_buffer) - 1);
  }
  return std::strlen(out_buffer);
}

auto linapple_is_supported_disk_image(const char* path) -> bool {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  const char* ext = std::strrchr(path, '.');
  if (ext == nullptr) {
    return false;
  }
  // Both cards' descriptors, with no instance: what the machine could mount,
  // whether or not a card is in a slot right now.
  for (const char* id : {"linapple.disk_II", "linapple.harddisk"}) {
    Peripheral_t* descriptor = peripheral_find_internal(id);
    if (descriptor == nullptr || descriptor->query == nullptr) {
      continue;
    }
    const uint32_t query_id =
        (std::strcmp(id, "linapple.disk_II") == 0)
            ? static_cast<uint32_t>(disk_query_supported_extensions)
            : static_cast<uint32_t>(harddisk_query_supported_extensions);
    char list[256] = {};
    size_t exts_size = sizeof(list);
    if (descriptor->query(nullptr, query_id, list, &exts_size) ==
            peripheral_ok &&
        extension_matches_list(ext, list)) {
      return true;
    }
  }
  return false;
}

auto linapple_load_program(const char* path) -> int {
  if (path == nullptr || path[0] == '\0') {
    return static_cast<int>(program_load_not_a_program);
  }

  if (linapple_is_supported_disk_image(path)) {
    return static_cast<int>(program_load_not_a_program);
  }

  const auto res = program_loader_try_load(path);
  if (res == program_load_ok) {
    return 0;
  }
  if (res != program_load_not_a_program) {
    return static_cast<int>(res);
  }

  const auto raw_res = program_loader_load_raw(path, 0x0800);
  if (raw_res == program_load_ok) {
    return 0;
  }
  return static_cast<int>(raw_res);
}

// One batch cut at the cycles cards asked to be woken at: the Disk II and the
// Mockingboard keep an intra-frame mark against the batch's executed count,
// so the slices must continue one frame-relative count.
static auto internal_run_cycles(uint32_t cycles) -> uint32_t {
  if (cycles == 0) {
    return 0;
  }

  cpu_begin_frame(cycles);
  uint32_t executed_cycles = 0;
  do {
    peripheral_service_events(cpu_get_cumulative_cycles());
    executed_cycles = cpu_execute_slice(cycles, peripheral_next_event_cycle());
  } while (executed_cycles < cycles);

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

  const uint32_t executed = run_frame_cycles(cycles);

  peripheral_manager_on_vblank(true);
  basic_sync_update();

  if (video_cb != nullptr && video_is_frame_ready()) {
    const uint32_t* output = video_get_output_buffer();
    video_cb(output, video_width, video_height, video_width * bytes_per_pixel);
    video_clear_frame_ready();
  }
  return executed;
}

auto linapple_get_speed() noexcept -> uint32_t { return system_state.speed; }

auto linapple_set_speed(uint32_t speed) noexcept -> void {
  system_state.speed = std::min(speed, emulation_speed_max);
}

auto linapple_speed_increase() noexcept -> uint32_t {
  system_state.speed = std::min(system_state.speed + 2, emulation_speed_max);
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
  const uint32_t base_cycles = (system_state.clks_per_frame > 0)
                                   ? system_state.clks_per_frame
                                   : default_cycles_per_frame;
  if (system_state.speed == emulation_speed_normal) {
    return base_cycles;
  }
  double multiplier = 1.0;
  if (system_state.speed < emulation_speed_normal) {
    multiplier =
        speed_subnormal_base +
        (static_cast<double>(system_state.speed) * speed_subnormal_scale);
  } else {
    multiplier = static_cast<double>(system_state.speed) / speed_normal_divisor;
  }
  return static_cast<uint32_t>(static_cast<double>(base_cycles) * multiplier);
}

auto linapple_get_turbo() noexcept -> bool { return user_turbo; }

auto linapple_set_turbo(bool turbo) noexcept -> void { user_turbo = turbo; }

auto linapple_toggle_turbo() noexcept -> bool {
  user_turbo = !user_turbo;
  return user_turbo;
}

auto linapple_set_disk_turbo(bool enabled) noexcept -> bool {
  const bool previous = disk_turbo_enabled;
  disk_turbo_enabled = enabled;
  return previous;
}

auto linapple_is_full_speed() noexcept -> bool { return full_speed; }

auto linapple_get_app_title() noexcept -> const char* { return app_title; }

auto linapple_get_clock_hz() noexcept -> double { return current_clk_6502; }

auto linapple_get_apple2_type() noexcept -> Apple2Type {
  return current_apple2_type;
}

auto linapple_set_apple2_type(Apple2Type type) noexcept -> void {
  current_apple2_type = type;
}

auto linapple_get_language() noexcept -> Apple2Language {
  return current_language;
}

auto linapple_set_language(Apple2Language lang) noexcept -> void {
  current_language = lang;
}

auto linapple_set_key(uint32_t host_key, uint8_t apple_code, bool down)
    -> void {
  const KeyboardKeyEvent_t ev = {
      host_key,
      apple_code,
      static_cast<uint8_t>(down ? 1 : 0),
      {0, 0, 0, 0, 0, 0},
  };
  peripheral_command(0, keyboard_cmd_key, &ev, sizeof(ev));
}

auto linapple_set_key_state(uint8_t apple_code, bool down) -> void {
  linapple_set_key(apple_code, apple_code, down);
}

auto linapple_set_rept(bool down) -> void {
  const uint8_t level = down ? 1 : 0;
  peripheral_command(0, keyboard_cmd_rept, &level, sizeof(level));
}

// The Apple keys and shift are switches on the motherboard's lines, not codes
// in the card's latch, so the card's release covers only the matrix keys.
auto linapple_set_key_release_all() -> void {
  peripheral_command(0, keyboard_cmd_release_all, nullptr, 0);
  linapple_set_modifiers(false, false, false, false);
}

namespace {

struct HostModifierLevels {
  bool shift = false;
  bool ctrl = false;
  bool open_apple = false;
  bool solid_apple = false;
};

HostModifierLevels modifier_levels;
bool rocker_switch = false;

}  // namespace

auto linapple_get_modifiers(bool* shift, bool* ctrl, bool* open_apple,
                            bool* solid_apple) -> void {
  if (shift != nullptr) {
    *shift = modifier_levels.shift;
  }
  if (ctrl != nullptr) {
    *ctrl = modifier_levels.ctrl;
  }
  if (open_apple != nullptr) {
    *open_apple = modifier_levels.open_apple;
  }
  if (solid_apple != nullptr) {
    *solid_apple = modifier_levels.solid_apple;
  }
}

auto linapple_set_rocker_switch(bool local) -> void {
  rocker_switch = local;
  video_set_rocker_switch(local);
}

auto linapple_get_rocker_switch() -> bool { return rocker_switch; }

// The //e wires Open Apple and Solid Apple in parallel with the connector's
// PB0 and PB1, and the shift-key mod runs shift to PB2 (Apple IIe Technical
// Reference Manual, pp. 13 and 41), so the levels are the keyboard's side of
// a wired-OR with a controller's buttons on every model. Shift and control
// themselves are already in the code the host translated.
auto linapple_set_modifiers(bool shift, bool ctrl, bool open_apple,
                            bool solid_apple) -> void {
  modifier_levels.shift = shift;
  modifier_levels.ctrl = ctrl;
  modifier_levels.open_apple = open_apple;
  modifier_levels.solid_apple = solid_apple;
  switch_inputs_set_level(0, switch_source_keyboard, open_apple);
  switch_inputs_set_level(1, switch_source_keyboard, solid_apple);
  switch_inputs_set_level(2, switch_source_keyboard, shift);
}

auto linapple_set_game_switch(uint8_t line, bool down) -> void {
  switch_inputs_set_level(line, switch_source_connector, down);
}

auto linapple_set_game_pulldowns(uint8_t mask) -> void {
  switch_inputs_set_connector_pulldowns(mask);
}

auto linapple_set_shift_key_mod(bool jumper_in) -> void {
  switch_inputs_set_shift_key_mod(jumper_in);
}
