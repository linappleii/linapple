// SPDX-License-Identifier: GPL-2.0-only
#include <cstring>
#include <string>

#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "frontends/common/HarddiskFrontend.h"
#include "frontends/common/sdl/DiskChoose_Decl.h"

#if ENABLE_FTP
#include "core/services/ftp/FtpClient.h"
#include "core/services/ftp/FtpTypes.h"
#include "frontends/common/FtpDialog.h"
#endif

namespace {

// The chooser is a modal loop with nowhere to put its choice when the machine
// has no hard disk, so it does not open.
auto harddisk_slot_or_refuse(int drive) -> int {
  const int slot = harddisk_frontend_slot();
  if (slot == harddisk_frontend_no_card) {
    Logger::error("hard disk drive %d: no hard disk is installed\n", drive + 1);
  }
  return slot;
}

}  // namespace

auto harddisk_ui_ftp_select(int drive) -> void {
  if (drive < 0 || drive > 1) {
    return;
  }
  const int slot = harddisk_slot_or_refuse(drive);
  if (slot == harddisk_frontend_no_card) {
    return;
  }

#if ENABLE_FTP
  static size_t file_index = 0;
  static size_t back_idx = 0;
  static size_t dir_idx = 0;

  std::string filename;
  std::string full_path = system_state.ftp_server_hdd.data();
  bool is_directory = true;

  file_index = back_idx;
  if (full_path.empty()) {
    full_path = "ftp://ftp.apple.asimov.net/pub/apple_II/images/";
  }

  while (is_directory) {
    if (!choose_an_image_ftp(system_state.screen_width,
                             system_state.screen_height, full_path, slot,
                             filename, is_directory, file_index)) {
      draw_frame_window();
      return;
    }
    if (!is_directory) {
      break;
    }
    if (filename == "..") {
      auto r = full_path.find_last_of(ftp_separator);
      if (r == full_path.size() - 1) {
        r = full_path.find_last_of(ftp_separator, r - 1);
      }
      if (r != std::string::npos) {
        full_path = full_path.substr(0, 1 + r);
      }
      if (full_path.empty()) {
        full_path = "/";
      }
      file_index = dir_idx;
      continue;
    }

    if (full_path != "/") {
      full_path += filename + "/";
    } else {
      full_path = "/" + filename + "/";
    }
    dir_idx = file_index;
    file_index = 0;
  }

  util_safe_strcpy(system_state.ftp_server_hdd.data(), full_path.c_str(),
                   system_state.ftp_server_hdd.size());
  Configuration_t::instance().set_string("Preferences", REGVALUE_FTP_HDD_DIR,
                                         system_state.ftp_server_hdd.data());
  Configuration_t::instance().save();

  std::string safe_filename = Path::sanitize_filename(filename);
  if (safe_filename.empty()) {
    Logger::error("FTP: Rejected unsafe filename\n");
    back_idx = file_index;
    draw_frame_window();
    return;
  }

  if (!full_path.empty() && full_path.back() == '/') {
    full_path += safe_filename;
  } else {
    full_path += "/" + safe_filename;
  }

  FtpClient_t client;
  const FtpStatus_t status =
      client.download_file(full_path, system_state.ftp_local_dir.data(),
                           safe_filename, system_state.ftp_user_pass.data());
  if (status != FtpStatus_t::ok) {
    Logger::error(
        "FTP: Failed downloading harddisk image from %s (status %u)\n",
        full_path.c_str(), static_cast<unsigned>(status));
    back_idx = file_index;
    draw_frame_window();
    return;
  }

  const std::string local_path =
      std::string(system_state.ftp_local_dir.data()) + "/" + safe_filename;
  harddisk_frontend_insert(drive, local_path.c_str(), false);
  back_idx = file_index;
  draw_frame_window();
#else
  Logger::error("FTP: FTP support is disabled in this build\n");
#endif
}

auto harddisk_ui_select(int drive) -> void {
  if (drive < 0 || drive > 1) {
    return;
  }
  const int slot = harddisk_slot_or_refuse(drive);
  if (slot == harddisk_frontend_no_card) {
    return;
  }

  static size_t file_index = 0;
  static size_t back_idx = 0;
  static size_t dir_idx = 0;

  std::string filename;
  std::string full_path = system_state.hdd_dir.data();
  bool is_directory = true;

  file_index = back_idx;

  while (is_directory) {
    if (!choose_an_image(system_state.screen_width, system_state.screen_height,
                         full_path, slot, filename, is_directory, file_index)) {
      draw_frame_window();
      return;
    }
    if (!is_directory) {
      break;
    }
    if (filename == "..") {
      const auto last_sep_pos = full_path.find_last_of(file_separator);
      if (last_sep_pos != std::string::npos) {
        full_path = full_path.substr(0, last_sep_pos);
      }
      if (full_path.empty()) {
        full_path = "/";
      }
      file_index = dir_idx;
      continue;
    }

    full_path =
        (full_path == "/") ? ("/" + filename) : (full_path + "/" + filename);
    dir_idx = file_index;
    file_index = 0;
  }

  util_safe_strcpy(system_state.hdd_dir.data(), full_path.c_str(),
                   system_state.hdd_dir.size());
  Configuration_t::instance().set_string(
      "Preferences", REGVALUE_PREF_HDD_START_DIR, system_state.hdd_dir.data());
  Configuration_t::instance().save();

  const std::string file_path =
      (full_path == "/") ? ("/" + filename) : (full_path + "/" + filename);

  if (harddisk_frontend_insert(drive, file_path.c_str(), false) == 0) {
    Logger::info("HDD disk image %s inserted\n", file_path.c_str());
  }
  back_idx = file_index;
  draw_frame_window();
}
