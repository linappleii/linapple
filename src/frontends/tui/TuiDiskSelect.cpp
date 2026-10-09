// SPDX-License-Identifier: GPL-2.0-only
#include "TuiDiskSelect.h"

#include <cstddef>

#include "frontends/common/FileBrowser.h"

namespace {
DiskBrowser s_browser{};
}  // namespace

auto tui_disk_select_open(int slot, int drive) -> void {
  // A machine without the card asked for has no slot to browse for.
  if (slot < 0) {
    return;
  }
  if (s_browser.is_active) {
    disk_browser_close(&s_browser);
  }
  disk_browser_open(&s_browser, slot, drive, nullptr);
}

auto tui_disk_select_close() -> void { disk_browser_close(&s_browser); }

auto tui_disk_select_is_active() -> bool { return s_browser.is_active; }

auto tui_disk_select_get_slot() -> int { return s_browser.slot; }

auto tui_disk_select_get_drive() -> int { return s_browser.drive; }

auto tui_disk_select_get_current_dir() -> const char* {
  return s_browser.current_dir;
}

auto tui_disk_select_get_file_list() -> const FileList* {
  return s_browser.list_handle;
}

auto tui_disk_select_get_selected_index() -> size_t {
  return s_browser.selected_index;
}

auto tui_disk_select_get_first_visible_index() -> size_t {
  return s_browser.first_visible_index;
}

auto tui_disk_select_move(int delta, size_t page_size) -> void {
  disk_browser_move(&s_browser, delta, page_size);
}

auto tui_disk_select_page(int direction, size_t page_size) -> void {
  disk_browser_page(&s_browser, direction, page_size);
}

auto tui_disk_select_home() -> void { disk_browser_home(&s_browser); }

auto tui_disk_select_end(size_t page_size) -> void {
  disk_browser_end(&s_browser, page_size);
}

auto tui_disk_select_jump_char(char ch, size_t page_size) -> void {
  disk_browser_jump_char(&s_browser, ch, page_size);
}

auto tui_disk_select_confirm() -> bool {
  return disk_browser_confirm(&s_browser);
}
