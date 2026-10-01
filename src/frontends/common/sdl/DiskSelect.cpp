// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <cstring>
#include <string>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "frontends/common/AppController.h"
#include "frontends/common/sdl/DiskChoose_Decl.h"

#if ENABLE_FTP
#include "core/services/ftp/FtpClient.h"
#include "core/services/ftp/FtpTypes.h"
#include "frontends/common/FtpDialog.h"
#endif

auto disk_select(int drive) -> void {
  if (drive < 0 || drive > 1) {
    return;
  }

  static size_t file_index = 0;
  static size_t back_idx = 0;
  static size_t dir_idx = 0;

  std::string filename;
  std::string full_path = system_state.current_dir.data();
  bool is_dir = true;

  file_index = back_idx;

  while (is_dir) {
    if (!choose_an_image(system_state.screen_width, system_state.screen_height,
                         full_path, disk_default_slot, filename, is_dir,
                         file_index)) {
      draw_frame_window();
      return;
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
    } else {
      if (full_path != "/") {
        full_path += "/" + filename;
      } else {
        full_path = "/" + filename;
      }
      dir_idx = file_index;
      file_index = 0;
    }
  }

  util_safe_strcpy(system_state.current_dir.data(), full_path.c_str(),
                   system_state.current_dir.size());
  Configuration_t::instance().set_string("Preferences", REGVALUE_PREF_START_DIR,
                                         system_state.current_dir.data());
  Configuration_t::instance().save();

  full_path += "/" + filename;

  DiskInsertCmd_t cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  util_safe_strcpy(cmd.path, full_path.c_str(), sizeof(cmd.path));
  cmd.write_protected = 0;

  if (peripheral_command(disk_default_slot, disk_cmd_insert, &cmd,
                         sizeof(cmd)) == peripheral_ok) {
    app_controller_save_disk_config(drive);
  }

  back_idx = file_index;
  draw_frame_window();
}

auto disk_ftp_select_image(int drive) -> void {
  if (drive < 0 || drive > 1) {
    return;
  }

#if ENABLE_FTP
  static size_t file_index = 0;
  static size_t back_idx = 0;
  static size_t dir_idx = 0;

  std::string filename;
  std::string full_path = system_state.ftp_server.data();
  bool is_directory = true;

  file_index = back_idx;
  if (full_path.empty()) {
    full_path = "ftp://ftp.apple.asimov.net/pub/apple_II/images/games/";
  }

  while (is_directory) {
    if (!choose_an_image_ftp(
            system_state.screen_width, system_state.screen_height, full_path,
            disk_default_slot, filename, is_directory, file_index)) {
      draw_frame_window();
      return;
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
    } else {
      if (full_path != "/") {
        full_path += filename + "/";
      } else {
        full_path = "/" + filename + "/";
      }
      dir_idx = file_index;
      file_index = 0;
    }
  }

  util_safe_strcpy(system_state.ftp_server.data(), full_path.c_str(),
                   system_state.ftp_server.size());
  Configuration_t::instance().set_string("Preferences", REGVALUE_FTP_DIR,
                                         system_state.ftp_server.data());
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

  if (status == FtpStatus_t::ok) {
    const std::string local_path =
        std::string(system_state.ftp_local_dir.data()) + "/" + safe_filename;
    DiskInsertCmd_t cmd{};
    cmd.drive = static_cast<uint8_t>(drive);
    util_safe_strcpy(cmd.path, local_path.c_str(), sizeof(cmd.path));
    cmd.write_protected = 0;

    if (peripheral_command(disk_default_slot, disk_cmd_insert, &cmd,
                           sizeof(cmd)) == peripheral_ok) {
      app_controller_save_disk_config(drive);
    }
  } else {
    Logger::error("FTP: Failed downloading floppy image from %s (status %u)\n",
                  full_path.c_str(), static_cast<unsigned>(status));
  }

  back_idx = file_index;
  draw_frame_window();
#else
  Logger::error("FTP: FTP support is disabled in this build\n");
#endif
}
