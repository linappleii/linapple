// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <cstdio>
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
#include "core/services/ftp/FtpClient.h"
#include "core/services/ftp/FtpTypes.h"
#include "frontends/common/AppController.h"
#include "frontends/common/FtpDialog.h"
#include "frontends/common/sdl/DiskChoose_Decl.h"

void disk_select_image(int drive, char* pszFilename) {
  (void)pszFilename;
  static size_t fileIndex = 0;
  static size_t backdx = 0;
  static size_t dirdx = 0;

  std::string filename;
  std::string fullPath;
  bool isdir = false;

  fileIndex = backdx;
  isdir = true;
  fullPath = g_state.current_dir.data();

  while (isdir) {
    if (!choose_an_image(g_state.screen_width, g_state.screen_height, fullPath,
                         disk_default_slot, filename, isdir, fileIndex)) {
      draw_frame_window();
      return;
    }
    if (isdir) {
      if (filename == "..") {
        const auto last_sep_pos = fullPath.find_last_of(file_separator);
        if (last_sep_pos != std::string::npos) {
          fullPath = fullPath.substr(0, last_sep_pos);
        }
        if (fullPath == "") {
          fullPath = "/";
        }
        fileIndex = dirdx;

      } else {
        if (fullPath != "/") {
          fullPath += "/" + filename;
        } else {
          fullPath = "/" + filename;
        }
        dirdx = fileIndex;
        fileIndex = 0;
      }
    }
  }
  util_safe_strcpy(g_state.current_dir.data(), fullPath.c_str(),
                   g_state.current_dir.size());
  Configuration_t::instance().set_string("Preferences", REGVALUE_PREF_START_DIR,
                                         g_state.current_dir.data());
  Configuration_t::instance().save();

  fullPath += "/" + filename;

  DiskInsertCmd_t cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  util_safe_strcpy(cmd.path, fullPath.c_str(), sizeof(cmd.path));
  cmd.write_protected = 0;
  cmd.create_if_necessary = 1;

  if (peripheral_command(disk_default_slot, disk_cmd_insert, &cmd,
                         sizeof(cmd)) == peripheral_ok) {
    app_controller_save_disk_config(drive);
  }

  backdx = fileIndex;
  draw_frame_window();
}

void disk_select(int drive) {
  char select[] = "";
  disk_select_image(drive, select);  // drive is 0 for D1, 1 - for D2
}

void disk_ftp_select_image(int drive) {
#if ENABLE_FTP
  static size_t fileIndex = 0;
  static size_t backdx = 0;
  static size_t dirdx = 0;

  std::string filename;
  std::string fullPath;
  bool isDirectory = true;

  fileIndex = backdx;
  fullPath = g_state.ftp_server.data();
  if (fullPath.empty()) {
    fullPath = "ftp://ftp.apple.asimov.net/pub/apple_II/images/games/";
  }

  while (isDirectory) {
    if (!choose_an_image_ftp(g_state.screen_width, g_state.screen_height,
                             fullPath, disk_default_slot, filename, isDirectory,
                             fileIndex)) {
      draw_frame_window();
      return;
    }
    if (isDirectory) {
      if (filename == "..") {
        auto r = fullPath.find_last_of(ftp_separator);
        if (r == fullPath.size() - 1) {
          r = fullPath.find_last_of(ftp_separator, r - 1);
        }
        if (r != std::string::npos) {
          fullPath = fullPath.substr(0, 1 + r);
        }
        if (fullPath.empty()) {
          fullPath = "/";
        }
        fileIndex = dirdx;
      } else {
        if (fullPath != "/") {
          fullPath += filename + "/";
        } else {
          fullPath = "/" + filename + "/";
        }
        dirdx = fileIndex;
        fileIndex = 0;
      }
    }
  }

  util_safe_strcpy(g_state.ftp_server.data(), fullPath.c_str(),
                   g_state.ftp_server.size());
  Configuration_t::instance().set_string("Preferences", REGVALUE_FTP_DIR,
                                         g_state.ftp_server.data());
  Configuration_t::instance().save();

  std::string safe_filename = Path::sanitize_filename(filename);
  if (safe_filename.empty()) {
    Logger::error("FTP: Rejected unsafe filename\n");
    backdx = fileIndex;
    draw_frame_window();
    return;
  }

  if (!fullPath.empty() && fullPath.back() == '/') {
    fullPath += safe_filename;
  } else {
    fullPath += "/" + safe_filename;
  }

  FtpClient_t client;
  const FtpStatus_t status =
      client.download_file(fullPath, g_state.ftp_local_dir.data(),
                           safe_filename, g_state.ftp_user_pass.data());

  if (status == FtpStatus_t::ok) {
    const std::string localPath =
        std::string(g_state.ftp_local_dir.data()) + "/" + safe_filename;
    DiskInsertCmd_t cmd{};
    cmd.drive = static_cast<uint8_t>(drive);
    util_safe_strcpy(cmd.path, localPath.c_str(), sizeof(cmd.path));
    cmd.write_protected = 0;
    cmd.create_if_necessary = 1;

    if (peripheral_command(disk_default_slot, disk_cmd_insert, &cmd,
                           sizeof(cmd)) == peripheral_ok) {
      app_controller_save_disk_config(drive);
    }
  } else {
    Logger::error("FTP: Failed downloading floppy image from %s (status %u)\n",
                  fullPath.c_str(), static_cast<unsigned>(status));
  }

  backdx = fileIndex;
  draw_frame_window();
#else
  (void)drive;
  Logger::error("FTP: FTP support is disabled in this build\n");
#endif
}
