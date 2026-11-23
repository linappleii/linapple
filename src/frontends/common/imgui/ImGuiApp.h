// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>

#include "apple2/Apple2Types.h"
#include "frontends/common/imgui/ImGuiBackend.h"

struct ImGuiViewportRect {
  float x;
  float y;
  float w;
  float h;
};

struct ImGuiPanelFlags {
  bool show_drives{false};
  bool show_save_states{false};
  bool show_settings{false};
  bool show_shortcuts{false};
  bool show_about{false};
};

struct ImGuiModelOption {
  Apple2Type type;
  const char* label;
};

// Pure layout helper computing the Apple II viewport rect inside a window
// after reserving top and bottom chrome bars.
auto imgui_compute_content_rect(int win_w, int win_h, float top_inset,
                                float bottom_inset, bool keep_aspect) noexcept
    -> ImGuiViewportRect;

auto imgui_app_init(const ImGuiHostCallbacks& host, float dpi_scale) -> void;
auto imgui_app_shutdown() -> void;
auto imgui_app_is_initialized() noexcept -> bool;

auto imgui_app_draw_frame() -> void;
auto imgui_app_handle_dialog_result(ImGuiDialogAction action, const char* path,
                                    const char* extra) -> void;
auto imgui_app_handle_file_drop(const char* path) -> void;

auto imgui_app_is_visible() noexcept -> bool;
auto imgui_app_set_visible(bool visible) noexcept -> void;
auto imgui_app_toggle_visible() noexcept -> void;

auto imgui_app_get_keep_aspect() noexcept -> bool;
auto imgui_app_set_keep_aspect(bool keep_aspect) noexcept -> void;
auto imgui_app_get_host_keyboard_layout() noexcept -> bool;
auto imgui_app_set_host_keyboard_layout(bool enabled) noexcept -> void;

auto imgui_app_wants_host_cursor(bool is_fullscreen) noexcept -> bool;
auto imgui_app_get_content_rect(int win_w, int win_h) noexcept
    -> ImGuiViewportRect;

auto imgui_app_panel_flags() noexcept -> ImGuiPanelFlags&;
auto imgui_app_notify(const char* message) -> void;
auto imgui_app_open_dialog(ImGuiDialogAction action,
                           const char* extra_param = nullptr) -> void;

auto imgui_app_model_options(size_t* out_count) noexcept
    -> const ImGuiModelOption*;
auto imgui_app_request_model_change(Apple2Type type) -> void;
auto imgui_app_request_restart() -> void;
auto imgui_app_reinit_joystick() -> void;
auto imgui_app_save_configuration() -> bool;

auto imgui_app_toggle_uppercase_caps() -> void;
auto imgui_app_is_audio_muted() noexcept -> bool;
auto imgui_app_set_audio_muted(bool muted) -> void;
auto imgui_app_get_disk_turbo() noexcept -> bool;
auto imgui_app_set_disk_turbo(bool enabled) -> void;

auto imgui_app_mount_disk(int drive, const char* path, bool write_protected)
    -> bool;
auto imgui_app_eject_disk(int drive) -> void;
auto imgui_app_swap_drives() -> void;
auto imgui_app_mount_harddisk(int drive, const char* path, bool write_protected)
    -> bool;
auto imgui_app_eject_harddisk(int drive) -> void;

auto imgui_app_boot_machine() -> void;
auto imgui_app_boot_rom_basic() -> void;
auto imgui_app_return_to_launcher() -> void;
auto imgui_app_quick_save(int slot) -> void;
auto imgui_app_quick_load(int slot) -> void;
auto imgui_app_toggle_fullscreen() -> void;
