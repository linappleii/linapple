// SPDX-License-Identifier: GPL-2.0-only
#pragma once

auto imgui_panels_invalidate_caches() -> void;
auto imgui_panels_refresh_save_slots() -> void;
auto imgui_panels_slot_exists(int slot) -> bool;

auto imgui_draw_drives_panel(bool* open) -> void;
auto imgui_draw_save_states_panel(bool* open) -> void;
auto imgui_draw_settings_panel(bool* open) -> void;
auto imgui_draw_shortcuts_panel(bool* open) -> void;
auto imgui_draw_about_panel(bool* open) -> void;
