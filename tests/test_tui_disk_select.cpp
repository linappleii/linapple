#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "doctest.h"
#include "frontends/common/FileBrowser.h"
#include "frontends/tui/TuiDiskSelect.h"

TEST_CASE("TuiDiskSelect: Open and Close Lifecycle") {
  CHECK_FALSE(tui_disk_select_is_active());

  tui_disk_select_open(0);
  CHECK(tui_disk_select_is_active());
  CHECK(tui_disk_select_get_drive() == 0);
  CHECK(tui_disk_select_get_current_dir() != nullptr);

  tui_disk_select_close();
  CHECK_FALSE(tui_disk_select_is_active());

  tui_disk_select_open(1);
  CHECK(tui_disk_select_is_active());
  CHECK(tui_disk_select_get_drive() == 1);

  tui_disk_select_close();
  CHECK_FALSE(tui_disk_select_is_active());
}

TEST_CASE("TuiDiskSelect: Navigation and Paging") {
  tui_disk_select_open(0);
  REQUIRE(tui_disk_select_is_active());

  const FileList_t* list = tui_disk_select_get_file_list();
  if (list != nullptr && file_browser_get_count(list) > 1) {
    size_t count = file_browser_get_count(list);
    CHECK(tui_disk_select_get_selected_index() == 0);

    tui_disk_select_move(1, 14);
    CHECK(tui_disk_select_get_selected_index() == 1);

    tui_disk_select_move(-1, 14);
    CHECK(tui_disk_select_get_selected_index() == 0);

    tui_disk_select_end(14);
    CHECK(tui_disk_select_get_selected_index() == count - 1);

    tui_disk_select_home();
    CHECK(tui_disk_select_get_selected_index() == 0);
  }

  tui_disk_select_close();
}
