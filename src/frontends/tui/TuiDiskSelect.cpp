// SPDX-License-Identifier: GPL-2.0-only
#include "TuiDiskSelect.h"

#include <cstddef>

#include "frontends/common/FileBrowser.h"

namespace {
static DiskBrowser_t s_browser{};
}

auto tui_disk_select_open(int slot, int drive) -> void {
  disk_browser_open(&s_browser, slot, drive, nullptr);
}

auto tui_disk_select_close() -> void { disk_browser_close(&s_browser); }

auto tui_disk_select_is_active() -> bool { return s_browser.is_active; }

auto tui_disk_select_get_slot() -> int { return s_browser.slot; }

auto tui_disk_select_get_drive() -> int { return s_browser.drive; }

auto tui_disk_select_get_current_dir() -> const char* {
  return s_browser.current_dir;
}

auto tui_disk_select_get_file_list() -> const FileList_t* {
  return s_browser.list_handle;
}

auto tui_disk_select_get_selected_index() -> size_t {
  return s_browser.selected_index;
}

auto tui_disk_select_get_first_visible_index() -> size_t {
  return s_browser.first_visible_index;
}

auto tui_disk_select_set_first_visible_index(size_t index) -> void {
  s_browser.first_visible_index = index;
}

auto tui_disk_select_move(int delta, int page_size) -> void {
  disk_browser_move(&s_browser, delta, static_cast<size_t>(page_size));
}

auto tui_disk_select_page(int direction, int page_size) -> void {
  disk_browser_page(&s_browser, direction, static_cast<size_t>(page_size));
}

auto tui_disk_select_home() -> void { disk_browser_home(&s_browser); }

auto tui_disk_select_end(int page_size) -> void {
  disk_browser_end(&s_browser, static_cast<size_t>(page_size));
}

auto tui_disk_select_jump_char(char ch, int page_size) -> void {
  disk_browser_jump_char(&s_browser, ch, static_cast<size_t>(page_size));
}

auto tui_disk_select_confirm() -> void {
  (void)disk_browser_confirm(&s_browser);
}
