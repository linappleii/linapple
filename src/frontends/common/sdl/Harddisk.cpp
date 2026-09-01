// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "apple2/peripherals/disk/DiskFTP.h"
#include "apple2/peripherals/disk/ftpparse.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "frontends/common/sdl/DiskChoose_Decl.h"

// Note: Core hardware emulation logic moved to src/apple2/Harddisk.cpp
// This file only contains frontend UI functions.

constexpr uint8_t HARDDISK_SLOT = 7;

void HarddiskUI_FTPSelect(int drive) {
  static size_t fileIndex = 0;
  static size_t backdx = 0;
  static size_t dirdx = 0;

  std::string filename;
  std::string fullPath;
  bool isDirectory = true;

  fileIndex = backdx;
  fullPath = g_state.ftp_server_hdd.data();

  while (isDirectory) {
    if (!choose_an_image_ftp(g_state.screen_width, g_state.screen_height,
                             fullPath, HARDDISK_SLOT, filename, isDirectory,
                             fileIndex)) {
      DrawFrameWindow();
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
        if (fullPath == "") {
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

  Util_SafeStrCpy(g_state.ftp_server_hdd.data(), fullPath.c_str(),
                  g_state.ftp_server_hdd.size());
  Configuration_t::instance().set_string("Preferences", REGVALUE_FTP_HDD_DIR,
                                         g_state.ftp_server_hdd.data());
  Configuration_t::instance().save();

  std::string safe_filename = Path::sanitize_filename(filename);
  if (safe_filename.empty()) {
    Logger::error("FTP: Rejected unsafe filename\n");
    backdx = fileIndex;
    DrawFrameWindow();
    return;
  }

  fullPath += "/" + safe_filename;

  std::string localPath =
      std::string(g_state.ftp_local_dir.data()) + "/" + safe_filename;

  int error = ftp_get(fullPath.c_str(), localPath.c_str());
  if (!error) {
    HarddiskInsertCmd_t cmd{};
    cmd.drive = static_cast<uint8_t>(drive);
    strncpy(cmd.path, localPath.c_str(), sizeof(cmd.path) - 1);

    if (peripheral_command(HARDDISK_SLOT, harddisk_cmd_insert, &cmd,
                           sizeof(cmd)) == peripheral_ok) {
      if (drive) {
        Configuration_t::instance().set_string(
            "Preferences", REGVALUE_HDD_IMAGE2, localPath.c_str());
        Configuration_t::instance().save();
      } else {
        Configuration_t::instance().set_string(
            "Preferences", REGVALUE_HDD_IMAGE1, localPath.c_str());
        Configuration_t::instance().save();
      }
    }
  }
  backdx = fileIndex;
  DrawFrameWindow();
}

void HarddiskUI_Select(int drive) {
  static size_t fileIndex = 0;
  static size_t backdx = 0;
  static size_t dirdx = 0;

  std::string filename;
  std::string fullPath;
  bool isDirectory = false;

  fileIndex = backdx;
  isDirectory = true;
  fullPath = g_state.hdd_dir.data();

  while (isDirectory) {
    if (!choose_an_image(g_state.screen_width, g_state.screen_height, fullPath,
                         HARDDISK_SLOT, filename, isDirectory, fileIndex)) {
      DrawFrameWindow();
      return;
    }
    if (isDirectory) {
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

  Util_SafeStrCpy(g_state.hdd_dir.data(), fullPath.c_str(),
                  g_state.hdd_dir.size());
  Configuration_t::instance().set_string(
      "Preferences", REGVALUE_PREF_HDD_START_DIR, g_state.hdd_dir.data());
  Configuration_t::instance().save();

  fullPath += "/" + filename;

  HarddiskInsertCmd_t cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  strncpy(cmd.path, fullPath.c_str(), sizeof(cmd.path) - 1);

  if (peripheral_command(HARDDISK_SLOT, harddisk_cmd_insert, &cmd,
                         sizeof(cmd)) == peripheral_ok) {
    if (drive) {
      Configuration_t::instance().set_string("Preferences", REGVALUE_HDD_IMAGE2,
                                             fullPath.c_str());
      Configuration_t::instance().save();
    } else {
      Configuration_t::instance().set_string("Preferences", REGVALUE_HDD_IMAGE1,
                                             fullPath.c_str());
      Configuration_t::instance().save();
    }
    printf("HDD disk image %s inserted\n", fullPath.c_str());
  }
  backdx = fileIndex;
  DrawFrameWindow();
}
