// SPDX-License-Identifier: GPL-2.0-only
#include "TuiDiskSelect.h"

#include <cstddef>

#include "frontends/common/FileBrowser.h"

namespace {
DiskBrowser browser{};
}  // namespace

auto tui_disk_select_open(int slot, int drive) -> void {
  // A machine without the card asked for has no slot to browse for.
  if (slot < 0) {
    return;
  }
  if (browser.is_active) {
    disk_browser_close(&browser);
  }
  disk_browser_open(&browser, slot, drive, nullptr);
}

auto tui_disk_select_close() -> void { disk_browser_close(&browser); }

auto tui_disk_select_is_active() -> bool { return browser.is_active; }

auto tui_disk_select_get_slot() -> int { return browser.slot; }

auto tui_disk_select_get_drive() -> int { return browser.drive; }

auto tui_disk_select_get_current_dir() -> const char* {
  return browser.current_dir;
}

auto tui_disk_select_get_file_list() -> const FileList* {
  return browser.list_handle;
}

auto tui_disk_select_get_selected_index() -> size_t {
  return browser.selected_index;
}

auto tui_disk_select_get_first_visible_index() -> size_t {
  return browser.first_visible_index;
}

auto tui_disk_select_move(int delta, size_t page_size) -> void {
  disk_browser_move(&browser, delta, page_size);
}

auto tui_disk_select_page(int direction, size_t page_size) -> void {
  disk_browser_page(&browser, direction, page_size);
}

auto tui_disk_select_home() -> void { disk_browser_home(&browser); }

auto tui_disk_select_end(size_t page_size) -> void {
  disk_browser_end(&browser, page_size);
}

auto tui_disk_select_jump_char(char ch, size_t page_size) -> void {
  disk_browser_jump_char(&browser, ch, page_size);
}

auto tui_disk_select_confirm() -> bool {
  return disk_browser_confirm(&browser);
}
