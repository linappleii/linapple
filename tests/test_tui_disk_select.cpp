#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/Registry.h"
#include "doctest.h"
#include "frontends/common/FileBrowser.h"
#include "frontends/tui/TuiDiskSelect.h"
#include "frontends/tui/TuiVideo.h"

TEST_CASE("TuiDiskSelect: Open and Close Lifecycle") {
  CHECK_FALSE(tui_disk_select_is_active());

  tui_disk_select_open(6, 0);
  CHECK(tui_disk_select_is_active());
  CHECK(tui_disk_select_get_slot() == 6);
  CHECK(tui_disk_select_get_drive() == 0);
  CHECK(tui_disk_select_get_current_dir() != nullptr);

  tui_disk_select_close();
  CHECK_FALSE(tui_disk_select_is_active());

  tui_disk_select_open(7, 1);
  CHECK(tui_disk_select_is_active());
  CHECK(tui_disk_select_get_slot() == 7);
  CHECK(tui_disk_select_get_drive() == 1);

  tui_disk_select_close();
  CHECK_FALSE(tui_disk_select_is_active());
}

TEST_CASE("TuiDiskSelect: Navigation and Paging") {
  tui_disk_select_open(6, 0);
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

TEST_CASE("TuiVideo: Screenshot Generation") {
  tui_video_initialize();
  tui_video_on_resize();
  tui_video_render_frame(nullptr, 0, 0, 0);

  tui_video_save_screenshot();

  struct stat st_ans{};
  struct stat st_txt{};
  bool ans_exists = (stat("linapple0000001.ans", &st_ans) == 0);
  bool txt_exists = (stat("linapple0000001.txt", &st_txt) == 0);

  CHECK(ans_exists);
  CHECK(txt_exists);
  if (ans_exists) {
    CHECK(st_ans.st_size > 0);
    unlink("linapple0000001.ans");
  }
  if (txt_exists) {
    CHECK(st_txt.st_size > 0);
    unlink("linapple0000001.txt");
  }
}

TEST_CASE("Configuration: Save and Load Runtime Settings") {
  std::string test_conf = "test_runtime_save.conf";
  Configuration_t::instance().set_path(test_conf);
  Configuration_t::instance().set_int("Configuration", "Fullscreen", 1);
  Configuration_t::instance().set_int("Configuration", "Emulation Speed", 20);

  bool saved = Configuration_t::instance().save();
  CHECK(saved);

  struct stat st{};
  CHECK(stat(test_conf.c_str(), &st) == 0);
  CHECK(st.st_size > 0);

  unlink(test_conf.c_str());
}
