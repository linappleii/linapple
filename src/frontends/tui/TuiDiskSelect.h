// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>

struct FileList_t;

auto tui_disk_select_open(int slot, int drive) -> void;
auto tui_disk_select_close() -> void;
[[nodiscard]] auto tui_disk_select_is_active() -> bool;
[[nodiscard]] auto tui_disk_select_get_slot() -> int;
[[nodiscard]] auto tui_disk_select_get_drive() -> int;
[[nodiscard]] auto tui_disk_select_get_current_dir() -> const char*;
[[nodiscard]] auto tui_disk_select_get_file_list() -> const FileList_t*;
[[nodiscard]] auto tui_disk_select_get_selected_index() -> size_t;
[[nodiscard]] auto tui_disk_select_get_first_visible_index() -> size_t;

auto tui_disk_select_move(int delta, size_t page_size) -> void;
auto tui_disk_select_page(int direction, size_t page_size) -> void;
auto tui_disk_select_home() -> void;
auto tui_disk_select_end(size_t page_size) -> void;
auto tui_disk_select_jump_char(char ch, size_t page_size) -> void;
[[nodiscard]] auto tui_disk_select_confirm() -> bool;
