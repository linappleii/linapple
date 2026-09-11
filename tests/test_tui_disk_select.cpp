// SPDX-License-Identifier: GPL-2.0-only
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <vector>

#include "core/Registry.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/FileBrowser.h"
#include "frontends/tui/TuiDiskSelect.h"
#include "frontends/tui/TuiVideo.h"
#include "test_fixtures.h"

namespace {

struct ScopedCwd_t {
  std::string original_cwd;

  explicit ScopedCwd_t(const std::string& new_dir) {
    std::array<char, 1024> buf{};
    if (getcwd(buf.data(), buf.size()) != nullptr) {
      original_cwd = buf.data();
    }
    chdir(new_dir.c_str());
  }

  ~ScopedCwd_t() {
    if (!original_cwd.empty()) {
      chdir(original_cwd.c_str());
    }
  }

  ScopedCwd_t(const ScopedCwd_t&) = delete;
  auto operator=(const ScopedCwd_t&) -> ScopedCwd_t& = delete;
  ScopedCwd_t(ScopedCwd_t&&) = delete;
  auto operator=(ScopedCwd_t&&) -> ScopedCwd_t& = delete;
};

struct ScopedConfigPath_t {
  std::string original_path{Configuration_t::instance().get_path()};

  ~ScopedConfigPath_t() { Configuration_t::instance().set_path(original_path); }

  ScopedConfigPath_t() = default;
  ScopedConfigPath_t(const ScopedConfigPath_t&) = delete;
  auto operator=(const ScopedConfigPath_t&) -> ScopedConfigPath_t& = delete;
  ScopedConfigPath_t(ScopedConfigPath_t&&) = delete;
  auto operator=(ScopedConfigPath_t&&) -> ScopedConfigPath_t& = delete;
};

auto create_dummy_disk(const std::string& path) -> void {
  std::ofstream out(path, std::ios::binary);
  std::vector<char> dummy(1024, 0);
  out.write(dummy.data(), static_cast<std::streamsize>(dummy.size()));
}

}  // namespace

TEST_CASE("TuiDiskSelect: Open and Close Lifecycle") {
  TestFixtures::ScopedTempDir_t temp_dir;
  ScopedCwd_t cwd_guard(temp_dir.path());

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
  TestFixtures::ScopedTempDir_t temp_dir;
  create_dummy_disk(temp_dir.path() + "/disk1.dsk");
  create_dummy_disk(temp_dir.path() + "/disk2.dsk");
  create_dummy_disk(temp_dir.path() + "/disk3.dsk");
  create_dummy_disk(temp_dir.path() + "/disk4.dsk");
  create_dummy_disk(temp_dir.path() + "/disk5.dsk");

  ScopedCwd_t cwd_guard(temp_dir.path());

  tui_disk_select_open(6, 0);
  REQUIRE(tui_disk_select_is_active());

  const FileList_t* list = tui_disk_select_get_file_list();
  REQUIRE(list != nullptr);

  size_t count = file_browser_get_count(list);
  REQUIRE(count == 6);  // 1 parent UP entry ("..") + 5 disk files

  CHECK(tui_disk_select_get_selected_index() == 0);

  tui_disk_select_move(1, 14);
  CHECK(tui_disk_select_get_selected_index() == 1);

  tui_disk_select_move(-1, 14);
  CHECK(tui_disk_select_get_selected_index() == 0);

  tui_disk_select_end(14);
  CHECK(tui_disk_select_get_selected_index() == count - 1);

  tui_disk_select_home();
  CHECK(tui_disk_select_get_selected_index() == 0);

  tui_disk_select_close();
}

TEST_CASE("TuiVideo: Screenshot Generation") {
  TestFixtures::ScopedTempDir_t temp_dir;
  ScopedCwd_t cwd_guard(temp_dir.path());

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

    // Verify ANSI escape sequences exist in the generated ANSI art file
    std::ifstream ans_file("linapple0000001.ans", std::ios::binary);
    std::string ans_content((std::istreambuf_iterator<char>(ans_file)),
                            std::istreambuf_iterator<char>());
    CHECK(ans_content.find("\x1b[") != std::string::npos);

    unlink("linapple0000001.ans");
  }
  if (txt_exists) {
    CHECK(st_txt.st_size > 0);
    unlink("linapple0000001.txt");
  }
}

TEST_CASE("Configuration: Save and Load Runtime Settings") {
  TestFixtures::ScopedTempDir_t temp_dir;
  ScopedConfigPath_t config_path_guard;

  std::string test_conf = temp_dir.path() + "/test_runtime_save.conf";
  Configuration_t::instance().set_path(test_conf);
  Configuration_t::instance().set_int("Configuration", "Fullscreen", 1);
  Configuration_t::instance().set_int("Configuration", "Emulation Speed", 20);

  bool saved = Configuration_t::instance().save();
  CHECK(saved);

  struct stat st{};
  CHECK(stat(test_conf.c_str(), &st) == 0);
  CHECK(st.st_size > 0);

  // Read file back to verify contents
  std::ifstream in(test_conf);
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  CHECK(content.find("Fullscreen = 1") != std::string::npos);
  CHECK(content.find("Emulation Speed = 20") != std::string::npos);
}

TEST_CASE("TuiVideo: Render Mode Toggle") {
  tui_video_set_render_mode(TUI_RENDER_SMART);
  CHECK(tui_video_get_render_mode() == TUI_RENDER_SMART);

  tui_video_toggle_render_mode();
  CHECK(tui_video_get_render_mode() == TUI_RENDER_BLOCK);

  tui_video_toggle_render_mode();
  CHECK(tui_video_get_render_mode() == TUI_RENDER_SMART);
}
