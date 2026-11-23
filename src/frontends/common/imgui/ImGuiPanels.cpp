// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/imgui/ImGuiPanels.h"

#include <imgui.h>
#include <sys/stat.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskFormatDriver.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "frontends/common/HarddiskFrontend.h"
#include "frontends/common/JoystickConfig.h"
#include "frontends/common/KeyboardTranslator.h"
#include "frontends/common/MouseFrontend.h"
#include "frontends/common/PrinterFrontend.h"
#include "frontends/common/SaveStateManager.h"
#include "frontends/common/SuperSerialFrontend.h"
#include "frontends/common/imgui/ImGuiApp.h"
#include "frontends/common/imgui/ImGuiBackend.h"

namespace {

constexpr float drives_window_width = 560.0F;
constexpr float drives_window_height = 460.0F;
constexpr float states_window_width = 480.0F;
constexpr float states_window_height = 420.0F;
constexpr float settings_window_width = 560.0F;
constexpr float settings_window_height = 430.0F;
constexpr float shortcuts_window_width = 520.0F;
constexpr float shortcuts_window_height = 430.0F;
constexpr float about_window_width = 400.0F;
constexpr float led_radius = 5.0F;
constexpr float led_spacing = 16.0F;
constexpr float half_scale = 0.5F;
constexpr float card_height_disk = 96.0F;
constexpr float card_height_hdd = 82.0F;
constexpr float format_combo_width = 220.0F;
constexpr float speed_slider_width = 240.0F;
constexpr float speed_normal_divisor = 10.0F;
constexpr double hz_per_mhz = 1000000.0;
constexpr int quick_slot_count = 10;

// NOLINTBEGIN(readability-magic-numbers)
// Justification: Visual indicator RGBA colors for drive status LEDs.
constexpr ImVec4 color_led_off(0.26F, 0.30F, 0.36F, 1.00F);
constexpr ImVec4 color_led_read(0.22F, 0.84F, 0.46F, 1.00F);
constexpr ImVec4 color_led_write(0.94F, 0.28F, 0.28F, 1.00F);
constexpr ImVec4 color_led_prot(0.94F, 0.68F, 0.18F, 1.00F);
constexpr ImVec4 color_accent(0.95F, 0.68F, 0.22F, 1.00F);
// NOLINTEND(readability-magic-numbers)

struct CachedSaveSlot {
  bool exists{false};
  std::array<char, 64> timestamp{};
};

struct PanelCaches {
  bool formats_loaded{false};
  std::vector<std::string> creatable_formats;
  bool slots_loaded{false};
  std::string slots_dir;
  std::array<CachedSaveSlot, quick_slot_count> slots{};
  bool settings_was_open{false};
  Apple2Type last_synced_model{A2TYPE_APPLE2EENHANCED};
  Apple2Type pending_model{A2TYPE_APPLE2EENHANCED};
};

PanelCaches panel_caches{};

constexpr std::array<const char*, VT_NUM_MODES> settings_video_modes = {
    "Monochrome (Custom)",  "Color (Standard)",       "Color (Text Optimized)",
    "Color (TV Emulation)", "Color (Half-Shift Dim)", "Monochrome (Amber)",
    "Monochrome (Green)",   "Monochrome (White)",
};

constexpr std::array<const char*, joystick_config_type_count> joystick_names = {
    "None", "Gamepad / Joystick", "Numpad", "Numpad (Self-Centering)", "Mouse",
};

constexpr std::array<const char*, 3> soundcard_names = {
    "None",
    "Mockingboard",
    "Phasor",
};

constexpr std::array<const char*, 2> caps_mode_names = {
    "Host Caps Lock",
    "Emulated (Independent)",
};

constexpr std::array<const char*, 2> mapping_mode_names = {
    "Symbolic",
    "Positional",
};

constexpr std::array<const char*, 4> quicksave_mode_names = {
    "Alt + 0..9",
    "Ctrl + 0..9",
    "Alt + Ctrl + 0..9",
    "Disabled",
};

struct LanguageOption {
  Apple2Language lang;
  const char* label;
};

constexpr std::array<LanguageOption, 6> language_options = {
    {
        {A2LANG_US, "US (Standard)"},
        {A2LANG_UK, "UK (British)"},
        {A2LANG_FR, "French"},
        {A2LANG_DE, "German"},
        {A2LANG_JP_ROMAN, "Japanese (Roman)"},
        {A2LANG_JP_KANA, "Japanese (Katakana)"},
    },
};

constexpr int max_volume_level = 15;

struct ShortcutRow {
  const char* keys;
  const char* description;
};

constexpr std::array<ShortcutRow, 17> shortcut_rows = {
    {
        {"Super + M", "Show / hide modern GUI chrome"},
        {"F1", "Classic in-frame help overlay"},
        {"Ctrl + F2", "Boot / hard reset Apple II"},
        {"F3 / Ctrl + F3", "Insert / eject Disk 1"},
        {"F4 / Ctrl + F4", "Insert / eject Disk 2"},
        {"Shift + F3 / F4", "Insert Hard Disk 1 / 2"},
        {"F5", "Swap Disk 1 and Disk 2"},
        {"F6", "Toggle fullscreen"},
        {"F7", "Toggle integrated 6502 debugger"},
        {"F8 / Shift + F8", "Save screenshot / save configuration"},
        {"F9 / Shift + F9", "Cycle video mode / toggle budget video"},
        {"Ctrl + F10", "Soft reset (Ctrl+Reset)"},
        {"F12", "Quit LinApple"},
        {"Pause", "Pause / resume emulation"},
        {"ScrollLock", "Toggle turbo (full speed) mode"},
        {"Numpad + / - / *", "Increase / decrease / reset speed"},
        {"Drag & Drop", "Mount disk, hard disk, or load .aws state"},
    },
};

auto draw_status_dot(ImVec4 color) -> void {
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float cy = pos.y + (ImGui::GetTextLineHeight() * half_scale);
  draw_list->AddCircleFilled(ImVec2(pos.x + led_radius, cy), led_radius,
                             ImGui::ColorConvertFloat4ToU32(color));
  ImGui::Dummy(ImVec2(led_spacing, ImGui::GetTextLineHeight()));
}

auto ensure_creatable_formats_loaded() -> void {
  if (panel_caches.formats_loaded) {
    return;
  }
  panel_caches.creatable_formats.clear();
  uint32_t count = 0;
  size_t count_size = sizeof(count);
  if (peripheral_query(disk_default_slot, disk_query_format_count, &count,
                       &count_size) == peripheral_ok) {
    for (uint32_t i = 0; i < count; ++i) {
      DiskFormatNameQuery q{};
      q.index = i;
      size_t qsize = sizeof(q);
      if (peripheral_query(disk_default_slot, disk_query_format_name, &q,
                           &qsize) == peripheral_ok &&
          (q.capabilities & disk_driver_cap_create) != 0 && q.name[0] != '\0') {
        panel_caches.creatable_formats.emplace_back(q.name);
      }
    }
  }
  panel_caches.formats_loaded = true;
}

auto ensure_save_slots_loaded() -> void {
  if (panel_caches.slots_loaded &&
      panel_caches.slots_dir == system_state.save_state_dir.data()) {
    return;
  }
  imgui_panels_refresh_save_slots();
}

auto draw_disk_drive_card(int drive_index, const char* title, uint8_t loaded,
                          uint8_t spinning, uint8_t writing,
                          uint8_t write_protected, const char* name,
                          const char* full_path) -> void {
  ImGui::PushID(drive_index);
  if (ImGui::BeginChild("##DiskCard", ImVec2(0.0F, card_height_disk),
                        ImGuiChildFlags_Borders)) {
    ImVec4 led_color = color_led_off;
    const char* state_label = "Empty";
    if (spinning != 0) {
      if (writing != 0) {
        led_color = color_led_write;
        state_label = "Writing";
      } else {
        led_color = color_led_read;
        state_label = "Reading";
      }
    } else if (loaded != 0) {
      if (write_protected != 0) {
        led_color = color_led_prot;
        state_label = "Write-Protected";
      } else {
        state_label = "Mounted";
      }
    }

    draw_status_dot(led_color);
    ImGui::SameLine(0.0F, 0.0F);
    ImGui::TextColored(color_accent, "%s", title);
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", state_label);

    if (loaded != 0) {
      const char* display =
          (name != nullptr && name[0] != '\0') ? name : full_path;
      ImGui::Text("%s", display);
      if (full_path != nullptr && full_path[0] != '\0' &&
          ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", full_path);
      }
    } else {
      ImGui::TextDisabled("No disk image inserted");
    }

    if (ImGui::Button("Insert...")) {
      imgui_app_open_dialog(drive_index == 0 ? imgui_dialog_open_disk1
                                             : imgui_dialog_open_disk2);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(loaded == 0);
    if (ImGui::Button("Eject")) {
      imgui_app_eject_disk(drive_index);
    }
    ImGui::SameLine();
    bool wp = (write_protected != 0);
    if (ImGui::Checkbox("Write Protect", &wp)) {
      DiskSetProtectCmd pcmd{};
      pcmd.drive = static_cast<uint8_t>(drive_index);
      pcmd.write_protected = wp ? 1 : 0;
      if (peripheral_command(disk_default_slot, disk_cmd_set_protect, &pcmd,
                             sizeof(pcmd)) == peripheral_ok) {
        std::array<char, 64> msg{};
        std::snprintf(msg.data(), msg.size(), "Drive %d write protect %s",
                      drive_index + 1, wp ? "ON" : "OFF");
        imgui_app_notify(msg.data());
      }
    }
    ImGui::EndDisabled();
  }
  ImGui::EndChild();
  ImGui::PopID();
}

auto draw_harddisk_drive_card(int drive_index, const char* title,
                              uint8_t loaded, uint8_t write_protected,
                              uint8_t activity_status, const char* name,
                              const char* full_path) -> void {
  constexpr int hdd_id_offset = 10;
  ImGui::PushID(drive_index + hdd_id_offset);
  if (ImGui::BeginChild("##HddCard", ImVec2(0.0F, card_height_hdd),
                        ImGuiChildFlags_Borders)) {
    ImVec4 led_color = color_led_off;
    const char* state_label = "Empty";
    if (activity_status == harddisk_status_write) {
      led_color = color_led_write;
      state_label = "Writing";
    } else if (activity_status == harddisk_status_read) {
      led_color = color_led_read;
      state_label = "Reading";
    } else if (loaded != 0) {
      if (write_protected != 0) {
        led_color = color_led_prot;
        state_label = "Read-Only";
      } else {
        state_label = "Mounted";
      }
    }

    draw_status_dot(led_color);
    ImGui::SameLine(0.0F, 0.0F);
    ImGui::TextColored(color_accent, "%s", title);
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", state_label);

    if (loaded != 0) {
      const char* display =
          (name != nullptr && name[0] != '\0') ? name : full_path;
      ImGui::Text("%s", display);
      if (full_path != nullptr && full_path[0] != '\0' &&
          ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", full_path);
      }
    } else {
      ImGui::TextDisabled("No hard disk image mounted");
    }

    if (ImGui::Button("Insert...")) {
      imgui_app_open_dialog(drive_index == 0 ? imgui_dialog_open_hdd1
                                             : imgui_dialog_open_hdd2);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(loaded == 0);
    if (ImGui::Button("Eject")) {
      imgui_app_eject_harddisk(drive_index);
    }
    ImGui::EndDisabled();
  }
  ImGui::EndChild();
  ImGui::PopID();
}

}  // namespace

auto imgui_panels_invalidate_caches() -> void {
  panel_caches.formats_loaded = false;
  panel_caches.slots_loaded = false;
  panel_caches.settings_was_open = false;
  panel_caches.last_synced_model = linapple_get_apple2_type();
  panel_caches.pending_model = panel_caches.last_synced_model;
}

auto imgui_panels_refresh_save_slots() -> void {
  panel_caches.slots_dir = system_state.save_state_dir.data();
  for (int i = 0; i < quick_slot_count; ++i) {
    CachedSaveSlot& slot = panel_caches.slots.at(static_cast<size_t>(i));
    std::array<char, path_max_len> fpath{};
    std::snprintf(
        fpath.data(), fpath.size(), "%.*s/SaveState%d.aws",
        static_cast<int>(std::strlen(system_state.save_state_dir.data())),
        system_state.save_state_dir.data(), i);

    struct stat st{};
    if (stat(fpath.data(), &st) != 0) {
      slot.exists = false;
      std::snprintf(slot.timestamp.data(), slot.timestamp.size(), "Empty");
      continue;
    }

    slot.exists = true;
    struct tm tm_buf{};
    if (localtime_r(&st.st_mtime, &tm_buf) == nullptr ||
        std::strftime(slot.timestamp.data(), slot.timestamp.size(),
                      "%Y-%m-%d %H:%M", &tm_buf) == 0) {
      std::snprintf(slot.timestamp.data(), slot.timestamp.size(), "Saved");
    }
  }
  panel_caches.slots_loaded = true;
}

auto imgui_panels_slot_exists(int slot) -> bool {
  if (slot < 0 || slot >= quick_slot_count) {
    return false;
  }
  ensure_save_slots_loaded();
  return panel_caches.slots.at(static_cast<size_t>(slot)).exists;
}

auto imgui_draw_drives_panel(bool* open) -> void {
  if (open == nullptr || !(*open)) {
    return;
  }

  ensure_creatable_formats_loaded();

  ImGui::SetNextWindowSize(ImVec2(drives_window_width, drives_window_height),
                           ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Disk & Drives Manager", open)) {
    ImGui::End();
    return;
  }

  DiskStatus dstatus{};
  size_t dsize = sizeof(dstatus);
  if (peripheral_query(disk_default_slot, disk_query_status, &dstatus,
                       &dsize) == peripheral_ok) {
    ImGui::TextUnformatted("Slot 6 — Disk II Controller");
    ImGui::Separator();

    draw_disk_drive_card(0, "Drive 1", dstatus.drive0_loaded,
                         dstatus.drive0_spinning, dstatus.drive0_writing,
                         dstatus.drive0_write_protected, dstatus.drive0_name,
                         dstatus.drive0_full_path);

    draw_disk_drive_card(1, "Drive 2", dstatus.drive1_loaded,
                         dstatus.drive1_spinning, dstatus.drive1_writing,
                         dstatus.drive1_write_protected, dstatus.drive1_name,
                         dstatus.drive1_full_path);

    if (ImGui::Button("Swap Drive 1 <-> Drive 2")) {
      imgui_app_swap_drives();
    }

    const std::vector<std::string>& creatable = panel_caches.creatable_formats;
    if (!creatable.empty()) {
      static int selected_format = 0;
      if (selected_format >= static_cast<int>(creatable.size())) {
        selected_format = 0;
      }

      ImGui::Spacing();
      ImGui::TextUnformatted("Create Blank Disk Image");
      ImGui::SetNextItemWidth(format_combo_width);
      if (ImGui::BeginCombo(
              "##BlankDiskFormat",
              creatable.at(static_cast<size_t>(selected_format)).c_str())) {
        for (size_t i = 0; i < creatable.size(); ++i) {
          const bool is_selected = (selected_format == static_cast<int>(i));
          if (ImGui::Selectable(creatable.at(i).c_str(), is_selected)) {
            selected_format = static_cast<int>(i);
          }
          if (is_selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
      ImGui::SameLine();
      if (ImGui::Button("Create...")) {
        imgui_app_open_dialog(
            imgui_dialog_create_disk,
            creatable.at(static_cast<size_t>(selected_format)).c_str());
      }
    }
  } else {
    ImGui::TextDisabled("No Disk II controller installed in Slot 6.");
  }

  const int hd_slot = harddisk_frontend_slot();
  if (hd_slot != harddisk_frontend_no_card) {
    HarddiskStatus hstatus{};
    size_t hsize = sizeof(hstatus);
    if (peripheral_query(hd_slot, harddisk_query_status, &hstatus, &hsize) ==
        peripheral_ok) {
      ImGui::Spacing();
      ImGui::Text("Slot %d — ProDOS Hard Disk", hd_slot);
      ImGui::Separator();

      draw_harddisk_drive_card(0, "Hard Disk 1", hstatus.drive0_loaded,
                               hstatus.drive0_write_protected,
                               hstatus.activity_status, hstatus.drive0_name,
                               hstatus.drive0_full_path);

      draw_harddisk_drive_card(1, "Hard Disk 2", hstatus.drive1_loaded,
                               hstatus.drive1_write_protected,
                               hstatus.activity_status, hstatus.drive1_name,
                               hstatus.drive1_full_path);
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextDisabled(
      "Tip: Drag and drop disk images directly onto the emulator window.");

  ImGui::End();
}

auto imgui_draw_save_states_panel(bool* open) -> void {
  if (open == nullptr || !(*open)) {
    return;
  }

  ensure_save_slots_loaded();

  ImGui::SetNextWindowSize(ImVec2(states_window_width, states_window_height),
                           ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Save States", open)) {
    ImGui::End();
    return;
  }

  ImGui::TextDisabled("Directory: %s", system_state.save_state_dir.data());
  if (ImGui::Button("Save State As...")) {
    imgui_app_open_dialog(imgui_dialog_save_state);
  }
  ImGui::SameLine();
  if (ImGui::Button("Load State From File...")) {
    imgui_app_open_dialog(imgui_dialog_load_state);
  }
  ImGui::SameLine();
  if (ImGui::Button("Refresh")) {
    imgui_panels_refresh_save_slots();
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextUnformatted("Quick Save Slots (0–9)");

  constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_BordersInnerH |
                                          ImGuiTableFlags_SizingStretchProp;
  constexpr int num_columns = 3;
  if (ImGui::BeginTable("##QuickSlotsTable", num_columns, table_flags)) {
    ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();

    for (int i = 0; i < quick_slot_count; ++i) {
      const CachedSaveSlot& slot =
          panel_caches.slots.at(static_cast<size_t>(i));
      ImGui::PushID(i);
      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      ImGui::Text("Slot %d", i);

      ImGui::TableSetColumnIndex(1);
      if (slot.exists) {
        ImGui::TextUnformatted(slot.timestamp.data());
      } else {
        ImGui::TextDisabled("Empty");
      }

      ImGui::TableSetColumnIndex(2);
      if (ImGui::SmallButton("Save")) {
        imgui_app_quick_save(i);
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(!slot.exists);
      if (ImGui::SmallButton("Load")) {
        imgui_app_quick_load(i);
      }
      ImGui::EndDisabled();

      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  ImGui::End();
}

auto imgui_draw_settings_panel(bool* open) -> void {
  if (open == nullptr || !(*open)) {
    panel_caches.settings_was_open = false;
    return;
  }

  const Apple2Type active_model = linapple_get_apple2_type();
  if (!panel_caches.settings_was_open ||
      active_model != panel_caches.last_synced_model) {
    panel_caches.pending_model = active_model;
    panel_caches.last_synced_model = active_model;
    panel_caches.settings_was_open = true;
  }

  ImGui::SetNextWindowSize(
      ImVec2(settings_window_width, settings_window_height),
      ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Settings", open)) {
    ImGui::End();
    return;
  }

  if (ImGui::BeginTabBar("##SettingsTabs")) {
    if (ImGui::BeginTabItem("Machine")) {
      size_t model_count = 0;
      const ImGuiModelOption* models = imgui_app_model_options(&model_count);
      const char* pending_label = "Enhanced Apple //e";
      for (size_t i = 0; i < model_count; ++i) {
        if (models[i].type == panel_caches.pending_model) {
          pending_label = models[i].label;
          break;
        }
      }

      ImGui::TextUnformatted("Apple II Model");
      if (ImGui::BeginCombo("##ModelCombo", pending_label)) {
        for (size_t i = 0; i < model_count; ++i) {
          const bool selected = (panel_caches.pending_model == models[i].type);
          if (ImGui::Selectable(models[i].label, selected)) {
            panel_caches.pending_model = models[i].type;
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
      if (panel_caches.pending_model != active_model) {
        if (ImGui::Button("Apply Model & Restart")) {
          imgui_app_request_model_change(panel_caches.pending_model);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(restarts emulation)");
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::TextUnformatted("Performance & Timing");

      int speed = static_cast<int>(linapple_get_speed());
      const float multiplier = static_cast<float>(speed) / speed_normal_divisor;
      constexpr size_t speed_label_len = 48;
      std::array<char, speed_label_len> speed_fmt{};
      std::snprintf(speed_fmt.data(), speed_fmt.size(), "%.1fx (%d)",
                    static_cast<double>(multiplier), speed);
      ImGui::SetNextItemWidth(speed_slider_width);
      if (ImGui::SliderInt(
              "Emulation Speed", &speed, static_cast<int>(emulation_speed_min),
              static_cast<int>(emulation_speed_max), speed_fmt.data())) {
        linapple_set_speed(static_cast<uint32_t>(speed));
      }
      ImGui::SameLine();
      if (ImGui::Button("1.0x")) {
        linapple_speed_reset();
      }

      bool turbo = linapple_get_turbo();
      if (ImGui::Checkbox("Turbo Mode (Uncapped Speed)", &turbo)) {
        linapple_set_turbo(turbo);
      }

      if (current_language != A2LANG_US &&
          (active_model == A2TYPE_APPLE2E ||
           active_model == A2TYPE_APPLE2EENHANCED)) {
        bool local_charset = linapple_get_rocker_switch();
        if (ImGui::Checkbox("Local Character Set (Rocker Switch)",
                            &local_charset)) {
          linapple_set_rocker_switch(local_charset);
        }
      }

      ImGui::Spacing();
      ImGui::Separator();
      const double mhz = linapple_get_clock_hz() / hz_per_mhz;
      ImGui::TextDisabled("Active Clock: %.3f MHz  |  Standard: %s", mhz,
                          system_state.video_scanner_ntsc ? "NTSC" : "PAL");

      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Video")) {
      const uint32_t cur_mode = (videotype < VT_NUM_MODES) ? videotype : 0;
      ImGui::TextUnformatted("Display Mode");
      if (ImGui::BeginCombo("##VideoModeCombo",
                            settings_video_modes.at(cur_mode))) {
        for (uint32_t m = 0; m < VT_NUM_MODES; ++m) {
          const bool selected = (cur_mode == m);
          if (ImGui::Selectable(settings_video_modes.at(m), selected)) {
            videotype = m;
            video_reinitialize();
            if (system_state.mode != app_mode_logo) {
              video_redraw_screen();
            }
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      ImGui::Spacing();
      bool budget = get_budget_video();
      if (ImGui::Checkbox("Budget Video (Fast Scanlines)", &budget)) {
        set_budget_video(budget);
      }

      bool keep_aspect = imgui_app_get_keep_aspect();
      if (ImGui::Checkbox("Preserve 4:3 Aspect Ratio", &keep_aspect)) {
        imgui_app_set_keep_aspect(keep_aspect);
      }

      bool classic_leds = show_leds;
      if (ImGui::Checkbox("Show Classic In-Frame Drive LEDs", &classic_leds)) {
        show_leds = classic_leds;
      }

      ImGui::Spacing();
      if (ImGui::Button(system_state.fullscreen ? "Exit Fullscreen (F6)"
                                                : "Enter Fullscreen (F6)")) {
        imgui_app_toggle_fullscreen();
      }

      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Audio")) {
      bool muted = imgui_app_is_audio_muted();
      if (ImGui::Checkbox("Mute All Audio", &muted)) {
        imgui_app_set_audio_muted(muted);
      }

      ImGui::Spacing();
      ImGui::Separator();

      uint32_t spkr_vol = max_volume_level;
      (void)config_load_int(cfg_sec_configuration, cfg_spkr_volume, &spkr_vol);
      int spkr_slider = static_cast<int>(spkr_vol);
      ImGui::SetNextItemWidth(speed_slider_width);
      if (ImGui::SliderInt("Speaker Volume (on restart)", &spkr_slider, 0,
                           max_volume_level)) {
        Configuration::instance().set_int(cfg_sec_configuration,
                                          cfg_spkr_volume, spkr_slider);
      }

      uint32_t mb_vol = max_volume_level;
      (void)config_load_int(cfg_sec_configuration, cfg_mb_volume, &mb_vol);
      int mb_slider = static_cast<int>(mb_vol);
      ImGui::SetNextItemWidth(speed_slider_width);
      if (ImGui::SliderInt("Mockingboard Volume (on restart)", &mb_slider, 0,
                           max_volume_level)) {
        Configuration::instance().set_int(cfg_sec_configuration, cfg_mb_volume,
                                          mb_slider);
      }

      uint32_t sc_type = 1;
      (void)config_load_int(cfg_sec_configuration, cfg_soundcard_type,
                            &sc_type);
      const size_t sc_idx =
          (sc_type < soundcard_names.size()) ? static_cast<size_t>(sc_type) : 0;
      ImGui::SetNextItemWidth(speed_slider_width);
      if (ImGui::BeginCombo("Soundcard Type", soundcard_names.at(sc_idx))) {
        for (size_t i = 0; i < soundcard_names.size(); ++i) {
          const bool sel = (i == sc_idx);
          if (ImGui::Selectable(soundcard_names.at(i), sel)) {
            Configuration::instance().set_int(
                cfg_sec_configuration, cfg_soundcard_type, static_cast<int>(i));
          }
          if (sel) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
      ImGui::TextDisabled(
          "Save configuration and restart to apply audio card "
          "and volume changes.");
      if (ImGui::Button("Save & Restart Audio")) {
        (void)imgui_app_save_configuration();
        imgui_app_request_restart();
      }

      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Input")) {
      ImGui::TextUnformatted("Keyboard");
      bool caps_on = keyboard_get_caps();
      if (ImGui::Checkbox("Apple II Uppercase (Caps Lock)", &caps_on)) {
        keyboard_set_caps_mode(caps_mode_emulated);
        keyboard_set_caps(caps_on);
        imgui_app_notify(caps_on ? "Apple II Uppercase (CAPS LOCK): ON"
                                 : "Apple II Uppercase (CAPS LOCK): OFF");
      }

      const int cur_caps_mode = keyboard_get_caps_mode();
      const size_t caps_idx = (cur_caps_mode == caps_mode_emulated) ? 1 : 0;
      ImGui::SetNextItemWidth(speed_slider_width);
      if (ImGui::BeginCombo("Caps Lock Mode", caps_mode_names.at(caps_idx))) {
        for (size_t i = 0; i < caps_mode_names.size(); ++i) {
          const bool sel = (i == caps_idx);
          if (ImGui::Selectable(caps_mode_names.at(i), sel)) {
            keyboard_set_caps_mode(static_cast<int>(i));
          }
          if (sel) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      const KeyboardMappingMode cur_map = keyboard_get_mapping_mode();
      const size_t map_idx = (cur_map == kbd_mode_positional) ? 1 : 0;
      ImGui::SetNextItemWidth(speed_slider_width);
      if (ImGui::BeginCombo("Key Mapping Mode",
                            mapping_mode_names.at(map_idx))) {
        for (size_t i = 0; i < mapping_mode_names.size(); ++i) {
          const bool sel = (i == map_idx);
          if (ImGui::Selectable(mapping_mode_names.at(i), sel)) {
            keyboard_set_mapping_mode(static_cast<KeyboardMappingMode>(i));
          }
          if (sel) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      bool hotkeys = keyboard_get_hotkeys_enabled();
      if (ImGui::Checkbox("Enable Function-Key Hotkeys (F1-F12)", &hotkeys)) {
        keyboard_set_hotkeys_enabled(hotkeys);
      }

      const QuickSaveMode cur_qs = keyboard_get_quicksave_mode();
      const size_t qs_idx =
          (static_cast<size_t>(cur_qs) < quicksave_mode_names.size())
              ? static_cast<size_t>(cur_qs)
              : 0;
      ImGui::SetNextItemWidth(speed_slider_width);
      if (ImGui::BeginCombo("Quick Save Combo",
                            quicksave_mode_names.at(qs_idx))) {
        for (size_t i = 0; i < quicksave_mode_names.size(); ++i) {
          const bool sel = (i == qs_idx);
          if (ImGui::Selectable(quicksave_mode_names.at(i), sel)) {
            keyboard_set_quicksave_mode(static_cast<QuickSaveMode>(i));
          }
          if (sel) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::TextUnformatted("Joysticks & Mouse");

      uint32_t joy0 = 0;
      (void)config_load_int(cfg_sec_configuration, cfg_joy_type1, &joy0);
      const size_t j0_idx =
          (joy0 < joystick_names.size()) ? static_cast<size_t>(joy0) : 0;
      ImGui::SetNextItemWidth(speed_slider_width);
      if (ImGui::BeginCombo("Joystick 0", joystick_names.at(j0_idx))) {
        for (size_t i = 0; i < joystick_names.size(); ++i) {
          const bool sel = (i == j0_idx);
          if (ImGui::Selectable(joystick_names.at(i), sel)) {
            Configuration::instance().set_int(
                cfg_sec_configuration, cfg_joy_type1, static_cast<int>(i));
            imgui_app_reinit_joystick();
          }
          if (sel) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      uint32_t joy1 = 0;
      (void)config_load_int(cfg_sec_configuration, cfg_joy_type2, &joy1);
      const size_t j1_idx =
          (joy1 < joystick_names.size()) ? static_cast<size_t>(joy1) : 0;
      ImGui::SetNextItemWidth(speed_slider_width);
      if (ImGui::BeginCombo("Joystick 1", joystick_names.at(j1_idx))) {
        for (size_t i = 0; i < joystick_names.size(); ++i) {
          const bool sel = (i == j1_idx);
          if (ImGui::Selectable(joystick_names.at(i), sel)) {
            Configuration::instance().set_int(
                cfg_sec_configuration, cfg_joy_type2, static_cast<int>(i));
            imgui_app_reinit_joystick();
          }
          if (sel) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      if (mouse_frontend_card_present()) {
        ImGui::TextDisabled("Apple Mouse Card: Slot %d (Capture: %s)",
                            mouse_frontend_card_slot(),
                            mouse_frontend_capture_enabled() ? "ON" : "OFF");
      } else {
        ImGui::TextDisabled("Apple Mouse Card: Not installed");
      }

      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Hardware")) {
      bool disk_turbo = imgui_app_get_disk_turbo();
      if (ImGui::Checkbox("Disk Turbo (Fast Disk Loading)", &disk_turbo)) {
        imgui_app_set_disk_turbo(disk_turbo);
      }

      uint32_t hdd_en = 0;
      (void)config_load_int(cfg_sec_configuration, cfg_hdd_enabled, &hdd_en);
      bool hdd_enabled = (hdd_en != 0);
      if (ImGui::Checkbox("Enable Hard Disk Controller (on restart)",
                          &hdd_enabled)) {
        Configuration::instance().set_int(cfg_sec_configuration,
                                          cfg_hdd_enabled, hdd_enabled ? 1 : 0);
      }

      uint32_t mouse_s4 = 0;
      (void)config_load_int(cfg_sec_configuration, cfg_mouse_in_slot4,
                            &mouse_s4);
      bool mouse_slot4 = (mouse_s4 != 0);
      if (ImGui::Checkbox("Enable Apple Mouse Card in Slot 4 (on restart)",
                          &mouse_slot4)) {
        Configuration::instance().set_int(
            cfg_sec_configuration, cfg_mouse_in_slot4, mouse_slot4 ? 1 : 0);
      }

      const Apple2Language cur_lang = linapple_get_language();
      const char* lang_label = "US (Standard)";
      for (const LanguageOption& opt : language_options) {
        if (opt.lang == cur_lang) {
          lang_label = opt.label;
          break;
        }
      }
      ImGui::SetNextItemWidth(speed_slider_width);
      if (ImGui::BeginCombo("Keyboard Language", lang_label)) {
        for (const LanguageOption& opt : language_options) {
          const bool sel = (opt.lang == cur_lang);
          if (ImGui::Selectable(opt.label, sel)) {
            linapple_set_language(opt.lang);
          }
          if (sel) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::TextUnformatted("Installed I/O Peripherals");
      const std::string printer_path = printer_frontend_output_path(1);
      ImGui::TextDisabled(
          "Parallel Printer (Slot 1): %s",
          printer_path.empty() ? "Inactive" : printer_path.c_str());

      const int ssc_slot = super_serial_frontend_primary_slot();
      if (ssc_slot > 0) {
        const std::string ssc_dev = super_serial_frontend_device_path(ssc_slot);
        ImGui::TextDisabled(
            "Super Serial Card (Slot %d): %s", ssc_slot,
            ssc_dev.empty() ? "Connected (loopback/idle)" : ssc_dev.c_str());
      } else {
        ImGui::TextDisabled("Super Serial Card: Not installed");
      }

      ImGui::Spacing();
      if (ImGui::Button("Save & Restart Hardware")) {
        (void)imgui_app_save_configuration();
        imgui_app_request_restart();
      }

      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Preferences")) {
      ImGui::Checkbox("Save State on Exit", &save_state_on_exit);

      bool chrome_visible = imgui_app_is_visible();
      if (ImGui::Checkbox("Show Menu & Status Bars (Super+M)",
                          &chrome_visible)) {
        imgui_app_set_visible(chrome_visible);
      }

      ImGui::Spacing();
      ImGui::Separator();
      const std::string& cfg_path = Configuration::instance().path;
      ImGui::TextDisabled("Config file: %s",
                          cfg_path.empty() ? "(default)" : cfg_path.c_str());

      if (ImGui::Button("Save Configuration to Disk")) {
        (void)imgui_app_save_configuration();
      }

      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }

  ImGui::End();
}

auto imgui_draw_shortcuts_panel(bool* open) -> void {
  if (open == nullptr || !(*open)) {
    return;
  }

  ImGui::SetNextWindowSize(
      ImVec2(shortcuts_window_width, shortcuts_window_height),
      ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Keyboard Shortcuts", open)) {
    ImGui::End();
    return;
  }

  constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_BordersInnerH |
                                          ImGuiTableFlags_SizingStretchProp;
  constexpr int num_columns = 2;
  if (ImGui::BeginTable("##ShortcutsTable", num_columns, table_flags)) {
    ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    for (const ShortcutRow& row : shortcut_rows) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextColored(color_accent, "%s", row.keys);
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(row.description);
    }
    ImGui::EndTable();
  }

  ImGui::End();
}

auto imgui_draw_about_panel(bool* open) -> void {
  if (open == nullptr || !(*open)) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(about_window_width, 0.0F),
                           ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("About LinApple", open,
                    ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }

  ImGui::TextColored(color_accent, "LinApple %s", VERSIONSTRING);
  ImGui::TextUnformatted("Apple ][ / ][+ / //e / Enhanced //e Emulator");
  ImGui::Separator();
  ImGui::TextWrapped(
      "Cross-platform Apple II emulator originally ported from AppleWin, "
      "featuring a decoupled hardware core and modern Dear ImGui "
      "desktop interface.");
  ImGui::Spacing();
  ImGui::TextDisabled("License: GPL-2.0-only");
  ImGui::TextDisabled("Repository: github.com/linappleii/linapple");
  ImGui::Spacing();
  if (ImGui::Button("Close")) {
    *open = false;
  }

  ImGui::End();
}
