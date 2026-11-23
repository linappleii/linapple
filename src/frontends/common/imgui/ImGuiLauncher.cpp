// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/imgui/ImGuiLauncher.h"

#include <dirent.h>
#include <imgui.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "frontends/common/FileBrowser.h"
#include "frontends/common/imgui/ImGuiApp.h"
#include "frontends/common/imgui/ImGuiBackend.h"

namespace {

using DirPtr = std::unique_ptr<DIR, int (*)(DIR*)>;

struct LauncherDiskEntry {
  std::string display_name;
  std::string full_path;
  bool is_builtin;
};

struct LauncherState {
  bool catalog_dirty{true};
  std::string scanned_dir;
  std::string builtin_master_path;
  std::vector<LauncherDiskEntry> disks;
  int selected_index{-1};
  std::array<char, 128> filter_buf{};
};

LauncherState launcher_state{};

constexpr float launcher_max_width = 720.0F;
constexpr float launcher_max_height = 500.0F;
constexpr float launcher_margin = 24.0F;
constexpr float half_scale = 0.5F;
constexpr float button_height_primary = 32.0F;
constexpr float list_min_height = 120.0F;
constexpr float footer_reserved_height = 108.0F;
constexpr float combo_width_compact = 175.0F;
constexpr float filter_input_width = 180.0F;

// NOLINTBEGIN(readability-magic-numbers)
// Justification: Visual styling RGBA colors for the launcher overlay.
constexpr ImVec4 color_accent(0.95F, 0.68F, 0.22F, 1.00F);
constexpr ImVec4 color_primary_btn(0.82F, 0.54F, 0.14F, 1.00F);
constexpr ImVec4 color_primary_btn_hover(0.92F, 0.64F, 0.20F, 1.00F);
constexpr ImVec4 color_primary_btn_active(0.72F, 0.46F, 0.10F, 1.00F);
constexpr ImVec4 color_primary_btn_text(0.06F, 0.08F, 0.10F, 1.00F);
constexpr ImVec4 color_backdrop(0.04F, 0.05F, 0.07F, 0.84F);
// NOLINTEND(readability-magic-numbers)

constexpr std::array<const char*, VT_NUM_MODES> launcher_video_modes = {
    "Mono (Custom)",    "Color (Standard)", "Color (Text)", "Color (TV Emu)",
    "Color (Half-Dim)", "Mono (Amber)",     "Mono (Green)", "Mono (White)",
};

auto contains_case_insensitive(const std::string& haystack, const char* needle)
    -> bool {
  if (needle == nullptr || needle[0] == '\0') {
    return true;
  }
  const size_t nlen = std::strlen(needle);
  if (nlen > haystack.size()) {
    return false;
  }
  for (size_t i = 0; i + nlen <= haystack.size(); ++i) {
    bool match = true;
    for (size_t j = 0; j < nlen; ++j) {
      const auto a = static_cast<unsigned char>(
          std::tolower(static_cast<unsigned char>(haystack[i + j])));
      const auto b = static_cast<unsigned char>(
          std::tolower(static_cast<unsigned char>(needle[j])));
      if (a != b) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

auto scan_disk_catalog() -> void {
  launcher_state.disks.clear();
  launcher_state.selected_index = -1;
  launcher_state.scanned_dir = system_state.current_dir.data();
  launcher_state.builtin_master_path = Path::find_data_file("Master.dsk");

  constexpr size_t ext_buf_len = 256;
  std::array<char, ext_buf_len> slot6_exts{};
  (void)linapple_get_supported_disk_extensions(
      disk_default_slot, slot6_exts.data(), slot6_exts.size());

  bool master_listed = false;
  if (!launcher_state.scanned_dir.empty() && slot6_exts.at(0) != '\0') {
    const DirPtr dir(opendir(launcher_state.scanned_dir.c_str()), closedir);
    if (dir != nullptr) {
      for (const struct dirent* ent = readdir(dir.get()); ent != nullptr;
           ent = readdir(dir.get())) {
        if (ent->d_name[0] == '.') {
          continue;
        }
        const std::string full =
            Path::join(launcher_state.scanned_dir, ent->d_name);
        struct stat st{};
        if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
          continue;
        }
        if (!file_browser_is_extension_supported(ent->d_name,
                                                 slot6_exts.data())) {
          continue;
        }
        const bool is_master = (!launcher_state.builtin_master_path.empty() &&
                                full == launcher_state.builtin_master_path);
        if (is_master) {
          master_listed = true;
        }
        launcher_state.disks.push_back(
            LauncherDiskEntry{ent->d_name, full, is_master});
      }
    }
  }

  std::sort(launcher_state.disks.begin(), launcher_state.disks.end(),
            [](const LauncherDiskEntry& a, const LauncherDiskEntry& b) -> bool {
              return a.display_name < b.display_name;
            });

  if (!master_listed && !launcher_state.builtin_master_path.empty()) {
    const LauncherDiskEntry master_entry{
        "Master.dsk",
        launcher_state.builtin_master_path,
        true,
    };
    launcher_state.disks.insert(launcher_state.disks.begin(), master_entry);
  }

  if (!launcher_state.disks.empty()) {
    launcher_state.selected_index = 0;
  }
  launcher_state.catalog_dirty = false;
}

auto draw_primary_button(const char* label, ImVec2 size) -> bool {
  ImGui::PushStyleColor(ImGuiCol_Button, color_primary_btn);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color_primary_btn_hover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, color_primary_btn_active);
  ImGui::PushStyleColor(ImGuiCol_Text, color_primary_btn_text);
  const bool clicked = ImGui::Button(label, size);
  constexpr int primary_style_colors = 4;
  ImGui::PopStyleColor(primary_style_colors);
  return clicked;
}

}  // namespace

auto imgui_launcher_mark_dirty() noexcept -> void {
  launcher_state.catalog_dirty = true;
}

auto imgui_draw_launcher() -> void {
  if (system_state.mode != app_mode_logo) {
    return;
  }

  if (launcher_state.catalog_dirty ||
      launcher_state.scanned_dir != system_state.current_dir.data()) {
    scan_disk_catalog();
  }

  const ImGuiViewport* vp = ImGui::GetMainViewport();

  ImDrawList* bg = ImGui::GetBackgroundDrawList();
  bg->AddRectFilled(vp->Pos,
                    ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                    ImGui::ColorConvertFloat4ToU32(color_backdrop));

  const float max_w = std::max(1.0F, vp->Size.x - (launcher_margin * 2.0F));
  const float max_h = std::max(1.0F, vp->Size.y - (launcher_margin * 2.0F));
  const float win_w = std::min(launcher_max_width, max_w);
  const float win_h = std::min(launcher_max_height, max_h);

  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + (vp->Size.x * half_scale),
                                 vp->Pos.y + (vp->Size.y * half_scale)),
                          ImGuiCond_Always, ImVec2(half_scale, half_scale));
  ImGui::SetNextWindowSize(ImVec2(win_w, win_h), ImGuiCond_Always);

  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

  if (!ImGui::Begin("LinApple Launcher", nullptr, flags)) {
    ImGui::End();
    return;
  }

  ImGui::TextColored(color_accent, "Welcome to LinApple");
  ImGui::SameLine();
  ImGui::TextDisabled(
      "— Select a disk to boot or start directly without a disk");
  ImGui::Separator();
  ImGui::Spacing();

  if (draw_primary_button("Boot ROM BASIC (No Disk)",
                          ImVec2(0.0F, button_height_primary))) {
    imgui_app_boot_rom_basic();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Eject Drive 1 for this session and boot into ROM BASIC / Monitor");
  }

  if (!launcher_state.builtin_master_path.empty()) {
    ImGui::SameLine();
    if (ImGui::Button("Boot Master Disk",
                      ImVec2(0.0F, button_height_primary)) &&
        imgui_app_mount_disk(0, launcher_state.builtin_master_path.c_str(),
                             false)) {
      imgui_app_boot_machine();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Mount built-in Master.dsk in Drive 1 and boot");
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Choose Disk & Boot...",
                    ImVec2(0.0F, button_height_primary))) {
    imgui_app_open_dialog(imgui_dialog_open_disk1_and_boot);
  }

  ImGui::Spacing();
  ImGui::Separator();

  ImGui::TextUnformatted("Disk Library");
  ImGui::SameLine();
  ImGui::TextDisabled("(%zu found)", launcher_state.disks.size());
  ImGui::SameLine();
  if (ImGui::SmallButton("Folder...")) {
    imgui_app_open_dialog(imgui_dialog_choose_disk_dir);
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Refresh")) {
    launcher_state.catalog_dirty = true;
  }
  constexpr float min_inline_filter_width = 140.0F;
  if (ImGui::GetContentRegionAvail().x >= min_inline_filter_width) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(
        std::min(filter_input_width, ImGui::GetContentRegionAvail().x));
  } else {
    ImGui::SetNextItemWidth(-1.0F);
  }
  ImGui::InputTextWithHint("##DiskFilter", "Filter disks...",
                           launcher_state.filter_buf.data(),
                           launcher_state.filter_buf.size());

  const float list_h =
      std::max(list_min_height,
               ImGui::GetContentRegionAvail().y - footer_reserved_height);
  if (ImGui::BeginChild("##LauncherDiskList", ImVec2(0.0F, list_h),
                        ImGuiChildFlags_Borders)) {
    if (launcher_state.disks.empty()) {
      ImGui::TextDisabled("No Disk II images found in: %s",
                          launcher_state.scanned_dir.c_str());
      ImGui::TextDisabled(
          "Use 'Choose Disk & Boot...', 'Folder...', or drag & drop a disk "
          "here.");
    } else {
      for (size_t i = 0; i < launcher_state.disks.size(); ++i) {
        const LauncherDiskEntry& entry = launcher_state.disks.at(i);
        if (!contains_case_insensitive(entry.display_name,
                                       launcher_state.filter_buf.data())) {
          continue;
        }
        constexpr size_t row_label_len = 320;
        std::array<char, row_label_len> row_label{};
        if (entry.is_builtin) {
          std::snprintf(row_label.data(), row_label.size(), "%s  [Built-in]",
                        entry.display_name.c_str());
        } else {
          std::snprintf(row_label.data(), row_label.size(), "%s",
                        entry.display_name.c_str());
        }
        const bool selected =
            (launcher_state.selected_index == static_cast<int>(i));
        if (ImGui::Selectable(row_label.data(), selected,
                              ImGuiSelectableFlags_AllowDoubleClick)) {
          launcher_state.selected_index = static_cast<int>(i);
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
              imgui_app_mount_disk(0, entry.full_path.c_str(), false)) {
            imgui_app_boot_machine();
          }
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("%s\nDouble-click to mount in Drive 1 and boot",
                            entry.full_path.c_str());
        }
      }
    }
  }
  ImGui::EndChild();

  const bool has_selection =
      (launcher_state.selected_index >= 0 &&
       static_cast<size_t>(launcher_state.selected_index) <
           launcher_state.disks.size());
  ImGui::BeginDisabled(!has_selection);
  if (ImGui::Button("Mount in Drive 1 & Boot")) {
    const LauncherDiskEntry& entry = launcher_state.disks.at(
        static_cast<size_t>(launcher_state.selected_index));
    if (imgui_app_mount_disk(0, entry.full_path.c_str(), false)) {
      imgui_app_boot_machine();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Mount in Drive 2")) {
    const LauncherDiskEntry& entry = launcher_state.disks.at(
        static_cast<size_t>(launcher_state.selected_index));
    imgui_app_mount_disk(1, entry.full_path.c_str(), false);
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button("Boot Current Drives")) {
    imgui_app_boot_machine();
  }

  ImGui::Spacing();
  ImGui::Separator();

  size_t model_count = 0;
  const ImGuiModelOption* models = imgui_app_model_options(&model_count);
  const Apple2Type cur_model = linapple_get_apple2_type();
  const char* cur_model_label = "Enhanced Apple //e";
  for (size_t i = 0; i < model_count; ++i) {
    if (models[i].type == cur_model) {
      cur_model_label = models[i].label;
      break;
    }
  }
  const float combo_avail = ImGui::GetContentRegionAvail().x;
  constexpr float combo_labels_overhead = 120.0F;
  constexpr float combo_min_width = 120.0F;
  const float combo_w =
      std::min(combo_width_compact,
               std::max(combo_min_width,
                        (combo_avail - combo_labels_overhead) * half_scale));

  ImGui::TextDisabled("Model:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(combo_w);
  if (ImGui::BeginCombo("##LauncherModel", cur_model_label)) {
    for (size_t i = 0; i < model_count; ++i) {
      const bool sel = (models[i].type == cur_model);
      if (ImGui::Selectable(models[i].label, sel) &&
          models[i].type != cur_model) {
        imgui_app_request_model_change(models[i].type);
      }
      if (sel) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  ImGui::SameLine();
  ImGui::TextDisabled("Video:");
  ImGui::SameLine();
  const uint32_t cur_vmode = (videotype < VT_NUM_MODES) ? videotype : 0;
  ImGui::SetNextItemWidth(combo_w);
  if (ImGui::BeginCombo("##LauncherVideo",
                        launcher_video_modes.at(cur_vmode))) {
    for (uint32_t m = 0; m < VT_NUM_MODES; ++m) {
      const bool sel = (m == cur_vmode);
      if (ImGui::Selectable(launcher_video_modes.at(m), sel)) {
        videotype = m;
        video_reinitialize();
      }
      if (sel) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  DiskStatus dstatus{};
  size_t dsize = sizeof(dstatus);
  if (peripheral_query(disk_default_slot, disk_query_status, &dstatus,
                       &dsize) == peripheral_ok) {
    ImGui::TextDisabled(
        "D1: %s   |   D2: %s",
        (dstatus.drive0_loaded != 0 && dstatus.drive0_name[0] != '\0')
            ? dstatus.drive0_name
            : "Empty",
        (dstatus.drive1_loaded != 0 && dstatus.drive1_name[0] != '\0')
            ? dstatus.drive1_name
            : "Empty");
  }

  ImGui::End();
}
