// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/imgui/ImGuiApp.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "apple2/Apple2Types.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AudioMixer.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/HarddiskFrontend.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/common/SaveStateManager.h"
#include "frontends/common/imgui/ImGuiBackend.h"
#include "frontends/common/imgui/ImGuiLauncher.h"
#include "frontends/common/imgui/ImGuiPanels.h"

namespace {

constexpr const char* cfg_gui_keep_aspect = "GUI Keep Aspect";
constexpr const char* cfg_gui_visible = "GUI Show Chrome";
constexpr const char* cfg_gui_host_kbd = "GUI Host Keyboard Layout";
constexpr const char* cfg_budget_video = "Budget Video";

struct ImGuiAppState {
  bool initialized{false};
  bool visible{true};
  bool keep_aspect{true};
  bool host_keyboard_layout{true};
  bool restore_disk_turbo{false};
  bool audio_muted{false};
  bool disk_turbo{true};
  float top_bar_height{0.0F};
  float bottom_bar_height{0.0F};
  ImGuiHostCallbacks host{};
  ImGuiPanelFlags panels{};
  std::string toast_message;
  uint64_t toast_expire_ms{0};
};

ImGuiAppState app_state{};

constexpr float rounding_window = 6.0F;
constexpr float rounding_popup = 5.0F;
constexpr float rounding_frame = 4.0F;
constexpr float padding_window_x = 12.0F;
constexpr float padding_window_y = 10.0F;
constexpr float padding_frame_x = 8.0F;
constexpr float padding_frame_y = 5.0F;
constexpr float spacing_item_x = 8.0F;
constexpr float spacing_item_y = 6.0F;
constexpr float spacing_inner_x = 6.0F;
constexpr float spacing_inner_y = 4.0F;
constexpr float scrollbar_width = 12.0F;
constexpr float fullscreen_reveal_margin = 32.0F;
constexpr float half_scale = 0.5F;
constexpr float status_bar_min_height = 26.0F;
constexpr float led_radius = 4.5F;
constexpr float led_spacing = 14.0F;
constexpr float toast_margin = 14.0F;
constexpr uint64_t toast_duration_ms = 2800;
constexpr double hz_per_mhz = 1000000.0;
constexpr int quick_slot_count = 10;
constexpr uint32_t speed_half = 5;
constexpr uint32_t speed_double = 20;

constexpr std::array<ImGuiModelOption, 5> model_options = {
    {
        {A2TYPE_APPLE2, "Apple ][ (Original)"},
        {A2TYPE_APPLE2PLUS, "Apple ][+"},
        {A2TYPE_APPLE2JPLUS, "Apple ][ J-Plus"},
        {A2TYPE_APPLE2E, "Apple //e (Unenhanced)"},
        {A2TYPE_APPLE2EENHANCED, "Enhanced Apple //e"},
    },
};

struct ThemeColorEntry {
  ImGuiCol index;
  float r;
  float g;
  float b;
  float a;
};

// Modern dark slate palette with warm amber/gold interactive accents.
// NOLINTBEGIN(readability-magic-numbers)
// Justification: RGBA color table entries are self-documenting visual values.
constexpr std::array<ThemeColorEntry, 33> theme_colors = {
    {
        {ImGuiCol_Text, 0.92F, 0.94F, 0.96F, 1.00F},
        {ImGuiCol_TextDisabled, 0.50F, 0.54F, 0.60F, 1.00F},
        {ImGuiCol_WindowBg, 0.09F, 0.11F, 0.14F, 0.96F},
        {ImGuiCol_ChildBg, 0.11F, 0.13F, 0.17F, 0.90F},
        {ImGuiCol_PopupBg, 0.11F, 0.13F, 0.17F, 0.98F},
        {ImGuiCol_Border, 0.22F, 0.26F, 0.33F, 0.85F},
        {ImGuiCol_BorderShadow, 0.00F, 0.00F, 0.00F, 0.00F},
        {ImGuiCol_FrameBg, 0.15F, 0.18F, 0.23F, 1.00F},
        {ImGuiCol_FrameBgHovered, 0.21F, 0.25F, 0.32F, 1.00F},
        {ImGuiCol_FrameBgActive, 0.25F, 0.30F, 0.38F, 1.00F},
        {ImGuiCol_TitleBg, 0.08F, 0.09F, 0.12F, 1.00F},
        {ImGuiCol_TitleBgActive, 0.12F, 0.15F, 0.20F, 1.00F},
        {ImGuiCol_TitleBgCollapsed, 0.08F, 0.09F, 0.12F, 0.75F},
        {ImGuiCol_MenuBarBg, 0.10F, 0.12F, 0.16F, 1.00F},
        {ImGuiCol_ScrollbarBg, 0.07F, 0.08F, 0.11F, 0.80F},
        {ImGuiCol_ScrollbarGrab, 0.26F, 0.31F, 0.39F, 1.00F},
        {ImGuiCol_ScrollbarGrabHovered, 0.35F, 0.41F, 0.51F, 1.00F},
        {ImGuiCol_ScrollbarGrabActive, 0.45F, 0.53F, 0.65F, 1.00F},
        {ImGuiCol_CheckMark, 0.95F, 0.68F, 0.22F, 1.00F},
        {ImGuiCol_SliderGrab, 0.88F, 0.60F, 0.18F, 1.00F},
        {ImGuiCol_SliderGrabActive, 0.98F, 0.72F, 0.28F, 1.00F},
        {ImGuiCol_Button, 0.18F, 0.22F, 0.28F, 1.00F},
        {ImGuiCol_ButtonHovered, 0.26F, 0.32F, 0.41F, 1.00F},
        {ImGuiCol_ButtonActive, 0.86F, 0.58F, 0.16F, 1.00F},
        {ImGuiCol_Header, 0.20F, 0.25F, 0.33F, 0.85F},
        {ImGuiCol_HeaderHovered, 0.86F, 0.58F, 0.16F, 0.55F},
        {ImGuiCol_HeaderActive, 0.86F, 0.58F, 0.16F, 0.85F},
        {ImGuiCol_Separator, 0.22F, 0.26F, 0.33F, 0.85F},
        {ImGuiCol_SeparatorHovered, 0.86F, 0.58F, 0.16F, 0.78F},
        {ImGuiCol_SeparatorActive, 0.95F, 0.68F, 0.22F, 1.00F},
        {ImGuiCol_Tab, 0.13F, 0.16F, 0.21F, 1.00F},
        {ImGuiCol_TabHovered, 0.86F, 0.58F, 0.16F, 0.65F},
        {ImGuiCol_TabSelected, 0.22F, 0.27F, 0.35F, 1.00F},
    },
};

constexpr ImVec4 color_running(0.24F, 0.82F, 0.48F, 1.00F);
constexpr ImVec4 color_paused(0.95F, 0.72F, 0.22F, 1.00F);
constexpr ImVec4 color_turbo(0.96F, 0.42F, 0.24F, 1.00F);
constexpr ImVec4 color_debug(0.68F, 0.46F, 0.96F, 1.00F);
constexpr ImVec4 color_idle(0.45F, 0.52F, 0.62F, 1.00F);
constexpr ImVec4 color_led_off(0.24F, 0.28F, 0.34F, 1.00F);
constexpr ImVec4 color_led_read(0.22F, 0.84F, 0.46F, 1.00F);
constexpr ImVec4 color_led_write(0.94F, 0.28F, 0.28F, 1.00F);
constexpr ImVec4 color_led_prot(0.94F, 0.68F, 0.18F, 1.00F);
constexpr ImVec4 color_toast_bg(0.12F, 0.15F, 0.20F, 0.94F);
// NOLINTEND(readability-magic-numbers)

constexpr std::array<const char*, VT_NUM_MODES> video_mode_names = {
    "Monochrome (Custom)",  "Color (Standard)",       "Color (Text Optimized)",
    "Color (TV Emulation)", "Color (Half-Shift Dim)", "Monochrome (Amber)",
    "Monochrome (Green)",   "Monochrome (White)",
};

auto extract_basename(const char* path) -> const char* {
  if (path == nullptr) {
    return "";
  }
  const char* slash = std::strrchr(path, '/');
  if (slash != nullptr && slash[1] != '\0') {
    return slash + 1;
  }
  return path;
}

auto has_extension_ci(const char* path, const char* ext) -> bool {
  if (path == nullptr || ext == nullptr) {
    return false;
  }
  const size_t path_len = std::strlen(path);
  const size_t ext_len = std::strlen(ext);
  if (path_len < ext_len) {
    return false;
  }
  const char* suffix = path + (path_len - ext_len);
  for (size_t i = 0; i < ext_len; ++i) {
    const auto a = static_cast<unsigned char>(
        std::tolower(static_cast<unsigned char>(suffix[i])));
    const auto b = static_cast<unsigned char>(
        std::tolower(static_cast<unsigned char>(ext[i])));
    if (a != b) {
      return false;
    }
  }
  return true;
}

auto apple2_short_name(Apple2Type type) noexcept -> const char* {
  switch (type) {
    case A2TYPE_APPLE2:
      return "Apple ][";
    case A2TYPE_APPLE2PLUS:
      return "Apple ][+";
    case A2TYPE_APPLE2JPLUS:
      return "Apple ][ J-Plus";
    case A2TYPE_APPLE2E:
      return "Apple //e";
    case A2TYPE_APPLE2EENHANCED:
      return "Apple //e+";
    case A2TYPE_CLONE_PRAVETS82:
      return "Pravets 82";
    case A2TYPE_CLONE_PRAVETS8M:
      return "Pravets 8M";
    case A2TYPE_CLONE_PRAVETS8C:
      return "Pravets 8C";
    case A2TYPE_CLONE_BASE64A:
      return "Base 64A";
    case A2TYPE_CLONE_TK3000E:
      return "TK-3000 //e";
    default:
      return "Apple II";
  }
}

auto apply_theme(float dpi_scale) -> void {
  ImGuiStyle& style = ImGui::GetStyle();
  style = ImGuiStyle();

  style.WindowRounding = rounding_window;
  style.ChildRounding = rounding_popup;
  style.FrameRounding = rounding_frame;
  style.PopupRounding = rounding_popup;
  style.ScrollbarRounding = rounding_window;
  style.GrabRounding = rounding_frame;
  style.TabRounding = rounding_frame;

  style.WindowPadding = ImVec2(padding_window_x, padding_window_y);
  style.FramePadding = ImVec2(padding_frame_x, padding_frame_y);
  style.ItemSpacing = ImVec2(spacing_item_x, spacing_item_y);
  style.ItemInnerSpacing = ImVec2(spacing_inner_x, spacing_inner_y);
  style.WindowBorderSize = 1.0F;
  style.FrameBorderSize = 0.0F;
  style.PopupBorderSize = 1.0F;
  style.ScrollbarSize = scrollbar_width;

  for (const ThemeColorEntry& entry : theme_colors) {
    style.Colors[entry.index] = ImVec4(entry.r, entry.g, entry.b, entry.a);
  }

  if (dpi_scale > 1.0F) {
    style.ScaleAllSizes(dpi_scale);
  }
}

auto any_panel_open() noexcept -> bool {
  return system_state.mode == app_mode_logo || app_state.panels.show_drives ||
         app_state.panels.show_save_states || app_state.panels.show_settings ||
         app_state.panels.show_shortcuts || app_state.panels.show_about;
}

auto should_show_chrome() noexcept -> bool {
  if (!app_state.visible) {
    return false;
  }
  const bool fs = (app_state.host.is_fullscreen != nullptr &&
                   app_state.host.is_fullscreen());
  if (fs) {
    const ImVec2 mouse_pos = ImGui::GetIO().MousePos;
    const bool near_top =
        (mouse_pos.y >= 0.0F && mouse_pos.y <= fullscreen_reveal_margin);
    return near_top || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
  }
  return true;
}

auto toggle_pause() -> void {
  if (app_state.host.release_mouse_capture != nullptr) {
    app_state.host.release_mouse_capture();
  }
  if (system_state.mode == app_mode_running) {
    system_state.mode = app_mode_paused;
    audio_mixer_set_fade(fade_out);
    imgui_app_notify("Emulation paused");
  } else if (system_state.mode == app_mode_paused) {
    system_state.mode = app_mode_running;
    audio_mixer_set_fade(fade_in);
    imgui_app_notify("Emulation resumed");
  }
  if (system_state.mode != app_mode_logo &&
      system_state.mode != app_mode_debug) {
    video_redraw_screen();
  }
  system_state.reset_timing = true;
}

auto set_video_mode(uint32_t mode) -> void {
  if (mode >= VT_NUM_MODES) {
    return;
  }
  videotype = mode;
  video_reinitialize();
  if (system_state.mode != app_mode_logo) {
    video_redraw_screen();
  }
}

auto format_compact_name(const char* name, char* out, size_t out_size,
                         size_t max_chars) -> const char* {
  if (out == nullptr || out_size == 0) {
    return "";
  }
  if (name == nullptr || name[0] == '\0') {
    std::snprintf(out, out_size, "Empty");
    return out;
  }
  const size_t len = std::strlen(name);
  const size_t limit = std::min(max_chars, out_size - 1);
  if (len <= limit) {
    std::snprintf(out, out_size, "%s", name);
    return out;
  }
  constexpr size_t ellipsis_len = 3;
  if (limit <= ellipsis_len) {
    std::snprintf(out, out_size, "%.*s", static_cast<int>(limit), name);
    return out;
  }
  std::snprintf(out, out_size, "%.*s...",
                static_cast<int>(limit - ellipsis_len), name);
  return out;
}

auto draw_led_dot(ImVec4 color) -> void {
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float cy = pos.y + (ImGui::GetTextLineHeight() * half_scale);
  draw_list->AddCircleFilled(ImVec2(pos.x + led_radius, cy), led_radius,
                             ImGui::ColorConvertFloat4ToU32(color));
  ImGui::Dummy(ImVec2(led_spacing, ImGui::GetTextLineHeight()));
}

auto draw_menu_bar() -> void {
  if (!ImGui::BeginMainMenuBar()) {
    return;
  }
  app_state.top_bar_height = ImGui::GetWindowSize().y;

  DiskStatus disk_status{};
  size_t dsize = sizeof(disk_status);
  const bool has_disk =
      (peripheral_query(disk_default_slot, disk_query_status, &disk_status,
                        &dsize) == peripheral_ok);
  const int hd_slot = harddisk_frontend_slot();

  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("Insert Disk 1...", "F3")) {
      imgui_app_open_dialog(imgui_dialog_open_disk1);
    }
    if (ImGui::MenuItem("Eject Disk 1", "Ctrl+F3", false,
                        has_disk && disk_status.drive0_loaded != 0)) {
      imgui_app_eject_disk(0);
    }
    if (ImGui::MenuItem("Insert Disk 2...", "F4")) {
      imgui_app_open_dialog(imgui_dialog_open_disk2);
    }
    if (ImGui::MenuItem("Eject Disk 2", "Ctrl+F4", false,
                        has_disk && disk_status.drive1_loaded != 0)) {
      imgui_app_eject_disk(1);
    }
    if (ImGui::MenuItem("Swap Drives", "F5", false, has_disk)) {
      imgui_app_swap_drives();
    }

    if (hd_slot != harddisk_frontend_no_card) {
      ImGui::Separator();
      if (ImGui::MenuItem("Insert Hard Disk 1...", "Shift+F3")) {
        imgui_app_open_dialog(imgui_dialog_open_hdd1);
      }
      if (ImGui::MenuItem("Eject Hard Disk 1", "Ctrl+Shift+F3")) {
        imgui_app_eject_harddisk(0);
      }
      if (ImGui::MenuItem("Insert Hard Disk 2...", "Shift+F4")) {
        imgui_app_open_dialog(imgui_dialog_open_hdd2);
      }
      if (ImGui::MenuItem("Eject Hard Disk 2", "Ctrl+Shift+F4")) {
        imgui_app_eject_harddisk(1);
      }
    }

    ImGui::Separator();
    if (ImGui::BeginMenu("Quick Save Slot")) {
      for (int i = 0; i < quick_slot_count; ++i) {
        std::array<char, 32> label{};
        std::snprintf(label.data(), label.size(), "Save Slot %d", i);
        if (ImGui::MenuItem(label.data())) {
          imgui_app_quick_save(i);
        }
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Quick Load Slot")) {
      for (int i = 0; i < quick_slot_count; ++i) {
        std::array<char, 32> label{};
        std::snprintf(label.data(), label.size(), "Load Slot %d", i);
        const bool exists = imgui_panels_slot_exists(i);
        if (ImGui::MenuItem(label.data(), nullptr, false, exists)) {
          imgui_app_quick_load(i);
        }
      }
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Save State As...")) {
      imgui_app_open_dialog(imgui_dialog_save_state);
    }
    if (ImGui::MenuItem("Load State...")) {
      imgui_app_open_dialog(imgui_dialog_load_state);
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Save Screenshot (BMP)", "F8")) {
      const bool saved = (app_state.host.save_screenshot != nullptr &&
                          app_state.host.save_screenshot());
      imgui_app_notify(saved ? "Screenshot saved"
                             : "Failed to save screenshot");
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Quit", "F12")) {
      system_state.mode = app_mode_exit;
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Machine")) {
    if (system_state.mode == app_mode_logo) {
      if (ImGui::MenuItem("Boot / Start Apple II", "Ctrl+F2")) {
        imgui_app_boot_machine();
      }
      if (ImGui::MenuItem("Boot ROM BASIC (No Disk)")) {
        imgui_app_boot_rom_basic();
      }
    } else {
      const bool paused = (system_state.mode == app_mode_paused);
      if (ImGui::MenuItem(paused ? "Resume" : "Pause", "Pause")) {
        toggle_pause();
      }
      if (ImGui::MenuItem("Boot ROM BASIC (No Disk)")) {
        imgui_app_boot_rom_basic();
      }
      if (ImGui::MenuItem("Open Launcher...")) {
        imgui_app_return_to_launcher();
      }
    }

    if (ImGui::MenuItem("Soft Reset", "Ctrl+F10")) {
      peripheral_manager_reset();
      linapple_reset_soft();
      if (peripheral_is_any_active() && linapple_set_disk_turbo(false)) {
        app_state.restore_disk_turbo = true;
      }
      if (app_state.host.refresh_status_leds != nullptr) {
        app_state.host.refresh_status_leds();
      }
      imgui_app_notify("Soft reset");
    }
    if (ImGui::MenuItem("Hard Reset (Power Cycle)", "Ctrl+F2")) {
      imgui_app_boot_machine();
      imgui_app_notify("Hard reset");
    }

    ImGui::Separator();
    bool caps_on = keyboard_get_caps();
    if (ImGui::MenuItem("Uppercase Mode (Apple II Caps Lock)", nullptr,
                        &caps_on)) {
      imgui_app_toggle_uppercase_caps();
    }
    bool turbo = linapple_get_turbo();
    if (ImGui::MenuItem("Turbo Mode", "ScrollLock", &turbo)) {
      linapple_set_turbo(turbo);
      imgui_app_notify(turbo ? "Turbo mode enabled" : "Turbo mode disabled");
    }

    if (ImGui::BeginMenu("Emulation Speed")) {
      const uint32_t cur_speed = linapple_get_speed();
      if (ImGui::MenuItem("0.5x (Slow)", nullptr, cur_speed == speed_half)) {
        linapple_set_speed(speed_half);
      }
      if (ImGui::MenuItem("1.0x (Normal)", "KP *",
                          cur_speed == emulation_speed_normal)) {
        linapple_speed_reset();
      }
      if (ImGui::MenuItem("2.0x (Fast)", nullptr, cur_speed == speed_double)) {
        linapple_set_speed(speed_double);
      }
      if (ImGui::MenuItem("4.0x (Maximum)", nullptr,
                          cur_speed == emulation_speed_max)) {
        linapple_set_speed(emulation_speed_max);
      }
      ImGui::EndMenu();
    }

    ImGui::Separator();
    if (ImGui::BeginMenu("Apple II Model (Restarts)")) {
      const Apple2Type cur = linapple_get_apple2_type();
      for (const ImGuiModelOption& opt : model_options) {
        if (ImGui::MenuItem(opt.label, nullptr, cur == opt.type) &&
            opt.type != cur) {
          imgui_app_request_model_change(opt.type);
        }
      }
      ImGui::EndMenu();
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("View")) {
    if (ImGui::BeginMenu("Video Mode")) {
      for (uint32_t m = 0; m < VT_NUM_MODES; ++m) {
        if (ImGui::MenuItem(video_mode_names.at(m), nullptr, videotype == m)) {
          set_video_mode(m);
        }
      }
      ImGui::EndMenu();
    }
    bool budget = get_budget_video();
    if (ImGui::MenuItem("Budget Video (Fast Scanlines)", "Shift+F9", &budget)) {
      set_budget_video(budget);
    }
    bool keep_aspect = app_state.keep_aspect;
    if (ImGui::MenuItem("Preserve Aspect Ratio", nullptr, &keep_aspect)) {
      app_state.keep_aspect = keep_aspect;
    }
    ImGui::Separator();
    bool fs = (app_state.host.is_fullscreen != nullptr &&
               app_state.host.is_fullscreen());
    if (ImGui::MenuItem("Fullscreen", "F6", &fs)) {
      imgui_app_toggle_fullscreen();
    }
    if (ImGui::MenuItem("Hide Interface Chrome", "Super+M")) {
      app_state.visible = false;
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Tools")) {
    ImGui::MenuItem("Disk & Drives Manager", nullptr,
                    &app_state.panels.show_drives);
    ImGui::MenuItem("Save States", nullptr, &app_state.panels.show_save_states);
    ImGui::MenuItem("Settings", nullptr, &app_state.panels.show_settings);
#if ENABLE_DEBUGGER
    if (!system_state.disable_debugger &&
        app_state.host.toggle_debugger != nullptr) {
      ImGui::Separator();
      const bool in_debug = (app_state.host.is_debugger_active != nullptr &&
                             app_state.host.is_debugger_active());
      if (ImGui::MenuItem("Integrated Debugger", "F7", in_debug)) {
        app_state.host.toggle_debugger();
      }
    }
#endif
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Help")) {
    ImGui::MenuItem("Keyboard Shortcuts", "F1",
                    &app_state.panels.show_shortcuts);
    ImGui::MenuItem("About LinApple", nullptr, &app_state.panels.show_about);
    ImGui::EndMenu();
  }

  ImGui::EndMainMenuBar();
}

auto draw_status_bar() -> void {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float bar_height = std::max(status_bar_min_height,
                                    ImGui::GetFrameHeight() + spacing_inner_y);
  app_state.bottom_bar_height = bar_height;

  ImGui::SetNextWindowPos(
      ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - bar_height));
  ImGui::SetNextWindowSize(ImVec2(vp->Size.x, bar_height));

  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
      ImGuiWindowFlags_NoScrollbar;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(padding_frame_x, spacing_inner_y));
  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));

  if (ImGui::Begin("##LinAppleStatusBar", nullptr, flags)) {
    const char* mode_text = "IDLE";
    ImVec4 mode_color = color_idle;
    if (linapple_get_turbo() && system_state.mode == app_mode_running) {
      mode_text = "TURBO";
      mode_color = color_turbo;
    } else if (system_state.mode == app_mode_running) {
      mode_text = "RUNNING";
      mode_color = color_running;
    } else if (system_state.mode == app_mode_paused) {
      mode_text = "PAUSED";
      mode_color = color_paused;
    } else if (system_state.mode == app_mode_debug ||
               system_state.mode == app_mode_stepping) {
      mode_text = "DEBUG";
      mode_color = color_debug;
    } else if (system_state.mode == app_mode_logo) {
      mode_text = "READY";
      mode_color = color_idle;
    }

    ImGui::TextColored(mode_color, "%s", mode_text);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    const bool caps_active = keyboard_get_caps();
    ImGui::TextColored(caps_active ? color_paused : color_idle, "CAPS");
    if (ImGui::IsItemClicked()) {
      imgui_app_toggle_uppercase_caps();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
          "Apple II Uppercase (Caps Lock): %s\nClick to toggle (uses "
          "independent emulated Caps Lock)",
          caps_active ? "ON" : "OFF");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    DiskStatus dstatus{};
    size_t dsize = sizeof(dstatus);
    if (peripheral_query(disk_default_slot, disk_query_status, &dstatus,
                         &dsize) == peripheral_ok) {
      ImVec4 d1_col = color_led_off;
      if (dstatus.drive0_spinning != 0) {
        d1_col =
            (dstatus.drive0_writing != 0) ? color_led_write : color_led_read;
      } else if (dstatus.drive0_loaded != 0 &&
                 dstatus.drive0_write_protected != 0) {
        d1_col = color_led_prot;
      }

      draw_led_dot(d1_col);
      ImGui::SameLine(0.0F, 0.0F);
      constexpr size_t compact_name_max = 14;
      constexpr size_t narrow_name_max = 10;
      constexpr size_t compact_buf_len = 24;
      const float win_w = ImGui::GetWindowWidth();
      constexpr float narrow_status_width = 680.0F;
      const size_t max_drive_chars =
          (win_w < narrow_status_width) ? narrow_name_max : compact_name_max;
      std::array<char, compact_buf_len> d1_buf{};
      const char* d1_name = format_compact_name(
          dstatus.drive0_loaded != 0 ? dstatus.drive0_name : nullptr,
          d1_buf.data(), d1_buf.size(), max_drive_chars);
      ImGui::Text("D1: %s", d1_name);
      if (ImGui::IsItemClicked()) {
        app_state.panels.show_drives = true;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Drive 1: %s\nClick to open Disk & Drives Manager",
                          dstatus.drive0_loaded != 0 ? dstatus.drive0_full_path
                                                     : "No disk inserted");
      }

      ImGui::SameLine();
      ImGui::TextDisabled("|");
      ImGui::SameLine();

      ImVec4 d2_col = color_led_off;
      if (dstatus.drive1_spinning != 0) {
        d2_col =
            (dstatus.drive1_writing != 0) ? color_led_write : color_led_read;
      } else if (dstatus.drive1_loaded != 0 &&
                 dstatus.drive1_write_protected != 0) {
        d2_col = color_led_prot;
      }

      draw_led_dot(d2_col);
      ImGui::SameLine(0.0F, 0.0F);
      std::array<char, compact_buf_len> d2_buf{};
      const char* d2_name = format_compact_name(
          dstatus.drive1_loaded != 0 ? dstatus.drive1_name : nullptr,
          d2_buf.data(), d2_buf.size(), max_drive_chars);
      ImGui::Text("D2: %s", d2_name);
      if (ImGui::IsItemClicked()) {
        app_state.panels.show_drives = true;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Drive 2: %s\nClick to open Disk & Drives Manager",
                          dstatus.drive1_loaded != 0 ? dstatus.drive1_full_path
                                                     : "No disk inserted");
      }
    }

    const int hd_slot = harddisk_frontend_slot();
    if (hd_slot != harddisk_frontend_no_card) {
      HarddiskStatus hstatus{};
      size_t hsize = sizeof(hstatus);
      if (peripheral_query(hd_slot, harddisk_query_status, &hstatus, &hsize) ==
          peripheral_ok) {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImVec4 hd_col = color_led_off;
        if (hstatus.activity_status == harddisk_status_write) {
          hd_col = color_led_write;
        } else if (hstatus.activity_status == harddisk_status_read) {
          hd_col = color_led_read;
        }
        draw_led_dot(hd_col);
        ImGui::SameLine(0.0F, 0.0F);
        ImGui::TextUnformatted("HDD");
        if (ImGui::IsItemClicked()) {
          app_state.panels.show_drives = true;
        }
      }
    }

    std::array<char, 128> right_info{};
    const double mhz = linapple_get_clock_hz() / hz_per_mhz;
    const float fps = ImGui::GetIO().Framerate;
    constexpr float compact_telemetry_width = 740.0F;
    if (ImGui::GetWindowWidth() < compact_telemetry_width) {
      std::snprintf(right_info.data(), right_info.size(), "%s | %.0f FPS",
                    apple2_short_name(linapple_get_apple2_type()),
                    static_cast<double>(fps));
    } else {
      std::snprintf(right_info.data(), right_info.size(),
                    "%s  |  %s  |  %.2f MHz  |  %.0f FPS",
                    apple2_short_name(linapple_get_apple2_type()),
                    system_state.video_scanner_ntsc ? "NTSC" : "PAL", mhz,
                    static_cast<double>(fps));
    }

    const float text_w = ImGui::CalcTextSize(right_info.data()).x;
    const float target_x = ImGui::GetWindowWidth() - text_w - padding_frame_x;
    const float left_end_x =
        ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
    if (target_x > left_end_x + spacing_item_x) {
      ImGui::SameLine(target_x);
      ImGui::TextDisabled("%s", right_info.data());
    }
  }
  ImGui::End();

  ImGui::PopStyleColor();
  constexpr int status_style_vars = 3;
  ImGui::PopStyleVar(status_style_vars);
}

auto draw_toast_overlay() -> void {
  if (app_state.toast_message.empty() ||
      app_state.host.get_ticks_ms == nullptr) {
    return;
  }
  if (app_state.host.get_ticks_ms() >= app_state.toast_expire_ms) {
    app_state.toast_message.clear();
    return;
  }

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImVec2 pos(
      vp->Pos.x + vp->Size.x - toast_margin,
      vp->Pos.y + vp->Size.y - app_state.bottom_bar_height - toast_margin);
  ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(1.0F, 1.0F));

  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

  ImGui::PushStyleColor(ImGuiCol_WindowBg, color_toast_bg);
  if (ImGui::Begin("##LinAppleToast", nullptr, flags)) {
    ImGui::TextUnformatted(app_state.toast_message.c_str());
  }
  ImGui::End();
  ImGui::PopStyleColor();
}

}  // namespace

auto imgui_compute_content_rect(int win_w, int win_h, float top_inset,
                                float bottom_inset, bool keep_aspect) noexcept
    -> ImGuiViewportRect {
  if (win_w <= 0 || win_h <= 0) {
    return ImGuiViewportRect{0.0F, 0.0F, 0.0F, 0.0F};
  }

  const float clamped_top = std::max(0.0F, top_inset);
  const float clamped_bottom = std::max(0.0F, bottom_inset);
  const float avail_w = static_cast<float>(win_w);
  const float avail_h =
      std::max(1.0F, static_cast<float>(win_h) - clamped_top - clamped_bottom);

  if (!keep_aspect) {
    return ImGuiViewportRect{0.0F, clamped_top, avail_w, avail_h};
  }

  constexpr float apple_aspect =
      static_cast<float>(SCREEN_WIDTH) / static_cast<float>(SCREEN_HEIGHT);
  float target_w = avail_w;
  float target_h = target_w / apple_aspect;
  if (target_h > avail_h) {
    target_h = avail_h;
    target_w = target_h * apple_aspect;
  }

  const float offset_x = (avail_w - target_w) * half_scale;
  const float offset_y = clamped_top + ((avail_h - target_h) * half_scale);
  return ImGuiViewportRect{offset_x, offset_y, target_w, target_h};
}

auto imgui_app_init(const ImGuiHostCallbacks& host, float dpi_scale) -> void {
  app_state.host = host;
  app_state.initialized = true;
  app_state.top_bar_height = 0.0F;
  app_state.bottom_bar_height = 0.0F;
  app_state.toast_message.clear();
  app_state.toast_expire_ms = 0;

  // Restore persisted GUI preferences when present in configuration.
  uint32_t cfg_val = 1;
  if (config_load_int(cfg_sec_preferences, cfg_gui_keep_aspect, &cfg_val)) {
    app_state.keep_aspect = (cfg_val != 0);
  }
  cfg_val = 1;
  if (config_load_int(cfg_sec_preferences, cfg_gui_visible, &cfg_val)) {
    app_state.visible = (cfg_val != 0);
  }
  cfg_val = 1;
  if (config_load_int(cfg_sec_preferences, cfg_gui_host_kbd, &cfg_val)) {
    app_state.host_keyboard_layout = (cfg_val != 0);
  }
  cfg_val = 0;
  if (config_load_int(cfg_sec_configuration, cfg_budget_video, &cfg_val)) {
    set_budget_video(cfg_val != 0);
  }
  cfg_val = 1;
  if (config_load_int(cfg_sec_configuration, "Disk Turbo", &cfg_val)) {
    app_state.disk_turbo = (cfg_val != 0);
  } else {
    app_state.disk_turbo = true;
  }
  app_state.audio_muted = false;

  apply_theme(dpi_scale);
  imgui_panels_invalidate_caches();
  imgui_launcher_mark_dirty();
}

auto imgui_app_shutdown() -> void {
  app_state.initialized = false;
  app_state.host = ImGuiHostCallbacks{};
  app_state.top_bar_height = 0.0F;
  app_state.bottom_bar_height = 0.0F;
  app_state.toast_message.clear();
  app_state.toast_expire_ms = 0;
}

auto imgui_app_is_initialized() noexcept -> bool {
  return app_state.initialized;
}

auto imgui_app_draw_frame() -> void {
  if (!app_state.initialized) {
    return;
  }

  const bool show_chrome = should_show_chrome();
  if (show_chrome) {
    draw_menu_bar();
    draw_status_bar();
  } else {
    app_state.top_bar_height = 0.0F;
    app_state.bottom_bar_height = 0.0F;
  }

  imgui_draw_launcher();
  imgui_draw_drives_panel(&app_state.panels.show_drives);
  imgui_draw_save_states_panel(&app_state.panels.show_save_states);
  imgui_draw_settings_panel(&app_state.panels.show_settings);
  imgui_draw_shortcuts_panel(&app_state.panels.show_shortcuts);
  imgui_draw_about_panel(&app_state.panels.show_about);
  draw_toast_overlay();
}

auto imgui_app_handle_dialog_result(ImGuiDialogAction action, const char* path,
                                    const char* extra) -> void {
  if (path == nullptr || path[0] == '\0') {
    return;
  }

  switch (action) {
    case imgui_dialog_open_disk1:
      imgui_app_mount_disk(0, path, false);
      break;
    case imgui_dialog_open_disk1_and_boot:
      if (imgui_app_mount_disk(0, path, false)) {
        imgui_app_boot_machine();
      }
      break;
    case imgui_dialog_open_disk2:
      imgui_app_mount_disk(1, path, false);
      break;
    case imgui_dialog_choose_disk_dir:
      util_safe_strcpy(system_state.current_dir.data(), path,
                       system_state.current_dir.size());
      Configuration::instance().set_string(cfg_sec_preferences,
                                           cfg_pref_start_dir,
                                           system_state.current_dir.data());
      (void)Configuration::instance().save();
      imgui_launcher_mark_dirty();
      imgui_app_notify("Updated disk library folder");
      break;
    case imgui_dialog_open_hdd1:
      imgui_app_mount_harddisk(0, path, false);
      break;
    case imgui_dialog_open_hdd2:
      imgui_app_mount_harddisk(1, path, false);
      break;
    case imgui_dialog_load_state:
      save_state_set_filename(path);
      if (save_state_load()) {
        if (app_state.host.release_mouse_capture != nullptr) {
          app_state.host.release_mouse_capture();
        }
        std::array<char, 256> msg{};
        std::snprintf(msg.data(), msg.size(), "Loaded state: %s",
                      extract_basename(path));
        imgui_app_notify(msg.data());
      } else {
        imgui_app_notify("Failed to load save state");
      }
      break;
    case imgui_dialog_save_state:
      save_state_set_filename(path);
      save_state_save();
      imgui_panels_refresh_save_slots();
      {
        std::array<char, 256> msg{};
        std::snprintf(msg.data(), msg.size(), "Saved state: %s",
                      extract_basename(path));
        imgui_app_notify(msg.data());
      }
      break;
    case imgui_dialog_create_disk: {
      DiskCreateImageCmd cmd{};
      util_safe_strcpy(cmd.path, path, sizeof(cmd.path));
      util_safe_strcpy(cmd.format_name, extra != nullptr ? extra : "",
                       sizeof(cmd.format_name));
      if (peripheral_command(disk_default_slot, disk_cmd_create_image, &cmd,
                             sizeof(cmd)) == peripheral_ok) {
        std::array<char, 256> msg{};
        std::snprintf(msg.data(), msg.size(), "Created blank disk: %s",
                      extract_basename(path));
        imgui_app_notify(msg.data());
      } else {
        imgui_app_notify("Failed to create blank disk image");
      }
      break;
    }
    case imgui_dialog_none:
    default:
      break;
  }
}

auto imgui_app_handle_file_drop(const char* path) -> void {
  if (path == nullptr || path[0] == '\0') {
    return;
  }

  if (has_extension_ci(path, ".aws")) {
    save_state_set_filename(path);
    if (save_state_load()) {
      if (app_state.host.release_mouse_capture != nullptr) {
        app_state.host.release_mouse_capture();
      }
      std::array<char, 256> msg{};
      std::snprintf(msg.data(), msg.size(), "Loaded state: %s",
                    extract_basename(path));
      imgui_app_notify(msg.data());
    } else {
      imgui_app_notify("Failed to load dropped save state");
    }
    return;
  }

  if (has_extension_ci(path, ".hdv") &&
      harddisk_frontend_slot() != harddisk_frontend_no_card) {
    imgui_app_mount_harddisk(0, path, false);
    return;
  }

  if (linapple_is_supported_disk_image(path)) {
    const bool was_logo = (system_state.mode == app_mode_logo);
    if (imgui_app_mount_disk(0, path, false) && was_logo) {
      imgui_app_boot_machine();
    }
    return;
  }

  imgui_app_notify("Unsupported dropped file format");
}

auto imgui_app_is_visible() noexcept -> bool { return app_state.visible; }

auto imgui_app_set_visible(bool visible) noexcept -> void {
  app_state.visible = visible;
}

auto imgui_app_toggle_visible() noexcept -> void {
  app_state.visible = !app_state.visible;
}

auto imgui_app_get_keep_aspect() noexcept -> bool {
  return app_state.keep_aspect;
}

auto imgui_app_set_keep_aspect(bool keep_aspect) noexcept -> void {
  app_state.keep_aspect = keep_aspect;
}

auto imgui_app_get_host_keyboard_layout() noexcept -> bool {
  return app_state.host_keyboard_layout;
}

auto imgui_app_set_host_keyboard_layout(bool enabled) noexcept -> void {
  app_state.host_keyboard_layout = enabled;
}

auto imgui_app_wants_host_cursor(bool is_fullscreen) noexcept -> bool {
  if (!app_state.initialized) {
    return false;
  }
  if (any_panel_open() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
    return true;
  }
  if (!is_fullscreen) {
    return true;
  }
  return should_show_chrome();
}

auto imgui_app_get_content_rect(int win_w, int win_h) noexcept
    -> ImGuiViewportRect {
  const bool fs = (app_state.host.is_fullscreen != nullptr &&
                   app_state.host.is_fullscreen());
  const bool reserve_bars = app_state.visible && !fs;
  const float top_inset = reserve_bars ? app_state.top_bar_height : 0.0F;
  const float bottom_inset = reserve_bars ? app_state.bottom_bar_height : 0.0F;
  return imgui_compute_content_rect(win_w, win_h, top_inset, bottom_inset,
                                    app_state.keep_aspect);
}

auto imgui_app_panel_flags() noexcept -> ImGuiPanelFlags& {
  return app_state.panels;
}

auto imgui_app_notify(const char* message) -> void {
  if (message == nullptr || message[0] == '\0') {
    return;
  }
  app_state.toast_message = message;
  const uint64_t now = (app_state.host.get_ticks_ms != nullptr)
                           ? app_state.host.get_ticks_ms()
                           : 0;
  app_state.toast_expire_ms = now + toast_duration_ms;
}

auto imgui_app_open_dialog(ImGuiDialogAction action, const char* extra_param)
    -> void {
  if (app_state.host.open_file_dialog == nullptr ||
      action == imgui_dialog_none) {
    return;
  }
  const char* default_dir = system_state.current_dir.data();
  if (action == imgui_dialog_open_hdd1 || action == imgui_dialog_open_hdd2) {
    default_dir = system_state.hdd_dir.data();
  } else if (action == imgui_dialog_load_state ||
             action == imgui_dialog_save_state) {
    default_dir = system_state.save_state_dir.data();
  }
  app_state.host.open_file_dialog(action, default_dir, extra_param);
}

auto imgui_app_model_options(size_t* out_count) noexcept
    -> const ImGuiModelOption* {
  if (out_count != nullptr) {
    *out_count = model_options.size();
  }
  return model_options.data();
}

auto imgui_app_request_model_change(Apple2Type type) -> void {
  Configuration::instance().set_int(cfg_sec_configuration, cfg_apple2_type,
                                    static_cast<int>(type));
  (void)Configuration::instance().save();
  linapple_set_apple2_type(type);
  imgui_app_request_restart();
}

auto imgui_app_request_restart() -> void {
  if (app_state.host.request_restart != nullptr) {
    app_state.host.request_restart();
  }
}

auto imgui_app_reinit_joystick() -> void {
  if (app_state.host.reinit_joystick != nullptr) {
    app_state.host.reinit_joystick();
  }
}

auto imgui_app_toggle_uppercase_caps() -> void {
  const bool next = !keyboard_get_caps();
  // Switch to independent emulated Caps Lock mode so toggling from the GUI
  // persists regardless of the host keyboard's Caps Lock LED state.
  keyboard_set_caps_mode(caps_mode_emulated);
  keyboard_set_caps(next);
  imgui_app_notify(next ? "Apple II Uppercase (CAPS LOCK): ON"
                        : "Apple II Uppercase (CAPS LOCK): OFF");
}

auto imgui_app_is_audio_muted() noexcept -> bool {
  return app_state.audio_muted;
}

auto imgui_app_set_audio_muted(bool muted) -> void {
  app_state.audio_muted = muted;
  audio_mixer_set_fade(muted ? fade_out : fade_in);
  imgui_app_notify(muted ? "Audio muted" : "Audio unmuted");
}

auto imgui_app_get_disk_turbo() noexcept -> bool {
  return app_state.disk_turbo;
}

auto imgui_app_set_disk_turbo(bool enabled) -> void {
  app_state.disk_turbo = enabled;
  app_state.restore_disk_turbo = false;
  (void)linapple_set_disk_turbo(enabled);
}

auto imgui_app_save_configuration() -> bool {
  Configuration::instance().set_int(cfg_sec_configuration, "Video Emulation",
                                    static_cast<int>(videotype));
  Configuration::instance().set_int(cfg_sec_configuration, "Emulation Speed",
                                    static_cast<int>(system_state.speed));
  Configuration::instance().set_int(cfg_sec_configuration, "Fullscreen",
                                    system_state.fullscreen ? 1 : 0);
  Configuration::instance().set_int(cfg_sec_configuration, cfg_show_leds,
                                    show_leds ? 1 : 0);
  Configuration::instance().set_int(cfg_sec_configuration,
                                    cfg_save_state_on_exit,
                                    save_state_on_exit ? 1 : 0);
  Configuration::instance().set_int(cfg_sec_configuration, cfg_budget_video,
                                    get_budget_video() ? 1 : 0);
  Configuration::instance().set_int(cfg_sec_configuration, "Disk Turbo",
                                    app_state.disk_turbo ? 1 : 0);
  Configuration::instance().set_int(cfg_sec_configuration, "Caps Lock Mode",
                                    keyboard_get_caps_mode());
  Configuration::instance().set_int(cfg_sec_configuration, cfg_keyb_type,
                                    static_cast<int>(keyboard_get_layout()));
  Configuration::instance().set_int(cfg_sec_configuration,
                                    cfg_keyb_charset_switch,
                                    linapple_get_rocker_switch() ? 1 : 0);
  Configuration::instance().set_int(cfg_sec_preferences, cfg_gui_keep_aspect,
                                    app_state.keep_aspect ? 1 : 0);
  Configuration::instance().set_int(cfg_sec_preferences, cfg_gui_visible,
                                    app_state.visible ? 1 : 0);
  Configuration::instance().set_int(cfg_sec_preferences, cfg_gui_host_kbd,
                                    app_state.host_keyboard_layout ? 1 : 0);
  const bool ok = Configuration::instance().save();
  imgui_app_notify(ok ? "Configuration saved"
                      : "Failed to save configuration (read-only?)");
  return ok;
}

auto imgui_app_mount_disk(int drive, const char* path, bool write_protected)
    -> bool {
  if (drive < 0 || drive >= disk_drive_count || path == nullptr ||
      path[0] == '\0') {
    return false;
  }

  DiskInsertCmd cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  cmd.write_protected = write_protected ? 1 : 0;
  util_safe_strcpy(cmd.path, path, sizeof(cmd.path));

  if (peripheral_command(disk_default_slot, disk_cmd_insert, &cmd,
                         sizeof(cmd)) == peripheral_ok) {
    if (app_state.restore_disk_turbo) {
      linapple_set_disk_turbo(true);
      app_state.restore_disk_turbo = false;
    }
    app_controller_save_disk_config(drive);
    if (app_state.host.refresh_status_leds != nullptr) {
      app_state.host.refresh_status_leds();
    }
    std::array<char, 256> msg{};
    std::snprintf(msg.data(), msg.size(), "Drive %d mounted: %s", drive + 1,
                  extract_basename(path));
    imgui_app_notify(msg.data());
    return true;
  }

  imgui_app_notify("Failed to mount disk image");
  return false;
}

auto imgui_app_eject_disk(int drive) -> void {
  if (drive < 0 || drive >= disk_drive_count) {
    return;
  }
  DiskEjectCmd cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  if (peripheral_command(disk_default_slot, disk_cmd_eject, &cmd,
                         sizeof(cmd)) == peripheral_ok) {
    app_controller_save_disk_config(drive);
    if (app_state.host.refresh_status_leds != nullptr) {
      app_state.host.refresh_status_leds();
    }
    std::array<char, 64> msg{};
    std::snprintf(msg.data(), msg.size(), "Ejected Drive %d", drive + 1);
    imgui_app_notify(msg.data());
  }
}

auto imgui_app_swap_drives() -> void {
  if (peripheral_command(disk_default_slot, disk_cmd_swap_drives, nullptr, 0) ==
      peripheral_ok) {
    app_controller_save_disk_config(0);
    app_controller_save_disk_config(1);
    if (app_state.host.refresh_status_leds != nullptr) {
      app_state.host.refresh_status_leds();
    }
    imgui_app_notify("Swapped Drive 1 and Drive 2");
  }
}

auto imgui_app_mount_harddisk(int drive, const char* path, bool write_protected)
    -> bool {
  if (drive < 0 || drive >= harddisk_drive_count || path == nullptr ||
      path[0] == '\0') {
    return false;
  }
  if (harddisk_frontend_insert(drive, path, write_protected) == 0) {
    if (app_state.host.refresh_status_leds != nullptr) {
      app_state.host.refresh_status_leds();
    }
    std::array<char, 256> msg{};
    std::snprintf(msg.data(), msg.size(), "Hard Disk %d mounted: %s", drive + 1,
                  extract_basename(path));
    imgui_app_notify(msg.data());
    return true;
  }
  return false;
}

auto imgui_app_eject_harddisk(int drive) -> void {
  if (drive < 0 || drive >= harddisk_drive_count) {
    return;
  }
  if (harddisk_frontend_eject(drive) == 0) {
    if (app_state.host.refresh_status_leds != nullptr) {
      app_state.host.refresh_status_leds();
    }
    std::array<char, 64> msg{};
    std::snprintf(msg.data(), msg.size(), "Ejected Hard Disk %d", drive + 1);
    imgui_app_notify(msg.data());
  }
}

auto imgui_app_boot_machine() -> void {
  if (app_state.restore_disk_turbo) {
    linapple_set_disk_turbo(true);
    app_state.restore_disk_turbo = false;
  }
  if (app_state.host.boot_or_reset != nullptr) {
    app_state.host.boot_or_reset();
  }
}

auto imgui_app_boot_rom_basic() -> void {
  if (app_state.host.is_debugger_active != nullptr &&
      app_state.host.is_debugger_active() &&
      app_state.host.toggle_debugger != nullptr) {
    app_state.host.toggle_debugger();
  }

  // Eject Drive 1 in memory without overwriting the user's saved Drive 1 path
  // in linapple.conf.
  DiskEjectCmd ecmd{};
  ecmd.drive = 0;
  (void)peripheral_command(disk_default_slot, disk_cmd_eject, &ecmd,
                           sizeof(ecmd));

  if (app_state.host.boot_or_reset != nullptr) {
    app_state.host.boot_or_reset();
  }

  // On autostart ROMs, step frames until the ROM finishes power-on init, then
  // issue a faithful hardware soft reset so the user can enter BASIC without
  // patching guest RAM behind the 6502's back.
  if (linapple_get_apple2_type() != A2TYPE_APPLE2) {
    const bool prev_disk_turbo =
        linapple_set_disk_turbo(false) || app_state.restore_disk_turbo;
    constexpr int max_init_frames = 30;
    const uint32_t frame_cycles = linapple_get_frame_cycles();
    for (int i = 0; i < max_init_frames; ++i) {
      linapple_run_frame(frame_cycles);
      if (peripheral_is_any_active()) {
        break;
      }
    }
    peripheral_manager_reset();
    linapple_reset_soft();
    // If peripheral_is_any_active() remains set after reset (until core
    // activity-clear on reset is merged), keep disk turbo disabled so BASIC
    // runs at 1.023 MHz instead of 100x full-speed batches, and restore it as
    // soon as a disk is mounted or booted.
    if (!peripheral_is_any_active()) {
      linapple_set_disk_turbo(prev_disk_turbo);
      app_state.restore_disk_turbo = false;
    } else {
      app_state.restore_disk_turbo = prev_disk_turbo;
    }
    linapple_run_frame(frame_cycles);
  }

  linapple_set_key_release_all();
  video_redraw_screen();
  if (app_state.host.refresh_status_leds != nullptr) {
    app_state.host.refresh_status_leds();
  }
  system_state.reset_timing = true;

  if (linapple_get_apple2_type() == A2TYPE_APPLE2) {
    imgui_app_notify("Booted into Apple ][ Monitor");
  } else {
    imgui_app_notify("Booted without disk (Ctrl+F10 for Soft Reset)");
  }
}

auto imgui_app_return_to_launcher() -> void {
  if (app_state.host.release_mouse_capture != nullptr) {
    app_state.host.release_mouse_capture();
  }
  audio_mixer_set_fade(fade_out);
  system_state.mode = app_mode_logo;
  imgui_launcher_mark_dirty();
  video_display_logo();
  video_set_frame_ready(true);
}

auto imgui_app_quick_save(int slot) -> void {
  if (app_state.host.quick_save_slot == nullptr) {
    return;
  }
  const bool ok = app_state.host.quick_save_slot(slot);
  imgui_panels_refresh_save_slots();
  std::array<char, 64> msg{};
  std::snprintf(msg.data(), msg.size(),
                ok ? "Saved Quick Slot %d" : "Failed to save Quick Slot %d",
                slot);
  imgui_app_notify(msg.data());
}

auto imgui_app_quick_load(int slot) -> void {
  if (app_state.host.quick_load_slot == nullptr) {
    return;
  }
  const bool ok = app_state.host.quick_load_slot(slot);
  std::array<char, 64> msg{};
  std::snprintf(msg.data(), msg.size(),
                ok ? "Loaded Quick Slot %d" : "Failed to load Quick Slot %d",
                slot);
  imgui_app_notify(msg.data());
}

auto imgui_app_toggle_fullscreen() -> void {
  if (app_state.host.toggle_fullscreen != nullptr) {
    app_state.host.toggle_fullscreen();
  }
}
