// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

enum ImGuiDialogAction : uint8_t {
  imgui_dialog_none = 0,
  imgui_dialog_open_disk1,
  imgui_dialog_open_disk1_and_boot,
  imgui_dialog_open_disk2,
  imgui_dialog_open_hdd1,
  imgui_dialog_open_hdd2,
  imgui_dialog_load_state,
  imgui_dialog_save_state,
  imgui_dialog_create_disk,
  imgui_dialog_choose_disk_dir,
};

// Backend-agnostic callbacks implemented by the host frontend (e.g. SDL3).
struct ImGuiHostCallbacks {
  auto (*open_file_dialog)(ImGuiDialogAction action, const char* default_dir,
                           const char* extra_param) -> void;
  auto (*save_screenshot)() -> bool;
  auto (*toggle_fullscreen)() -> void;
  auto (*is_fullscreen)() -> bool;
  auto (*toggle_debugger)() -> void;
  auto (*is_debugger_active)() -> bool;
  auto (*boot_or_reset)() -> void;
  auto (*request_restart)() -> void;
  auto (*quick_save_slot)(int slot) -> bool;
  auto (*quick_load_slot)(int slot) -> bool;
  auto (*refresh_status_leds)() -> void;
  auto (*release_mouse_capture)() -> void;
  auto (*reinit_joystick)() -> void;
  auto (*get_ticks_ms)() -> uint64_t;
};
