#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "apple2/peripherals/disk/DiskFTP.h"
#include "apple2/peripherals/disk/ftpparse.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "frontends/sdl3/DiskChoose.h"
#include "frontends/sdl3/Frame.h"

// Note: Core hardware emulation logic moved to src/apple2/Harddisk.cpp
// This file only contains frontend UI functions.

void HarddiskUI_FTPSelect(int nDrive) {
  // Selects HDrive from FTP directory
  static size_t fileIndex = 0;  // file index will be remembered for current dir
  static size_t backdx = 0;     // reserve
  static size_t dirdx = 0;      // reserve for dirs

  std::string filename;     // given filename
  std::string fullPath;     // full path for it
  bool isDirectory = true;  // if given filename is a directory?

  fileIndex = backdx;
  fullPath = g_state.sFTPServerHDD.data();  // global var for FTP path for HDD

  while (isDirectory) {
    if (!choose_an_image_ftp(g_state.ScreenWidth, g_state.ScreenHeight, fullPath,
                          7, filename, isDirectory, fileIndex)) {
      DrawFrameWindow();
      return;
    }
    // --
    if (isDirectory) {
      if (filename == "..") {
        // go to the upper directory
        auto r = fullPath.find_last_of(FTP_SEPARATOR);
        if (r == fullPath.size() - 1) {
          r = fullPath.find_last_of(FTP_SEPARATOR, r - 1);
        }
        if (r != std::string::npos) {
          fullPath = fullPath.substr(0, 1 + r);
        }
        if (fullPath == "") {
          fullPath = "/";  // we don't want fullPath to be empty
        }
        fileIndex = dirdx;  // restore
      } else {
        if (fullPath != "/") {
          fullPath += filename + "/";
        } else {
          fullPath = "/" + filename + "/";
        }
        dirdx = fileIndex;  // store it
        fileIndex = 0;      // start with beginning of dir
      }
    }
  }
  // we chose some file
  Util_SafeStrCpy(g_state.sFTPServerHDD.data(), fullPath.c_str(),
                  g_state.sFTPServerHDD.size());
  Configuration::Instance().SetString("Preferences", REGVALUE_FTP_HDD_DIR,
                                      g_state.sFTPServerHDD.data());
  Configuration::Instance().Save();  // save it

  fullPath += "/" + filename;

  std::string localPath = std::string(g_state.sFTPLocalDir.data()) + "/" +
                          filename;  // local path for file

  int error = ftp_get(fullPath.c_str(), localPath.c_str());
  if (!error) {
    HarddiskInsertCmd_t cmd{};
    cmd.drive = static_cast<uint8_t>(nDrive);
    strncpy(cmd.path, localPath.c_str(), sizeof(cmd.path) - 1);

    if (Peripheral_Command(7, harddisk_cmd_insert, &cmd, sizeof(cmd)) ==
        PERIPHERAL_OK) {
      // save file names for HDD disk 1 or 2
      if (nDrive) {
        Configuration::Instance().SetString("Preferences", REGVALUE_HDD_IMAGE2,
                                            localPath.c_str());
        Configuration::Instance().Save();
      } else {
        Configuration::Instance().SetString("Preferences", REGVALUE_HDD_IMAGE1,
                                            localPath.c_str());
        Configuration::Instance().Save();
      }
    }
  }
  backdx = fileIndex;  // store cursor position
  DrawFrameWindow();
}

void HarddiskUI_Select(int nDrive) {
  // Selects HDrive from file list
  static size_t fileIndex = 0;  // file index will be remembered for current dir
  static size_t backdx = 0;     // reserve
  static size_t dirdx = 0;      // reserve for dirs

  std::string filename;      // given filename
  std::string fullPath;      // full path for it
  bool isDirectory = false;  // if given filename is a directory?

  fileIndex = backdx;
  isDirectory = true;
  fullPath = g_state.sHDDDir.data();  // global var for disk selecting directory

  while (isDirectory) {
    if (!choose_an_image(g_state.ScreenWidth, g_state.ScreenHeight, fullPath, 7,
                       filename, isDirectory, fileIndex)) {
      DrawFrameWindow();
      return;  // if ESC was pressed, just leave
    }
    if (isDirectory) {
      if (filename == "..") {
        const auto last_sep_pos = fullPath.find_last_of(FILE_SEPARATOR);

        if (last_sep_pos != std::string::npos) {
          fullPath = fullPath.substr(0, last_sep_pos);
        }
        if (fullPath == "") {
          fullPath = "/";  // we don't want fullPath to be empty
        }
        fileIndex = dirdx;  // restore
      } else {
        if (fullPath != "/") {
          fullPath += "/" + filename;
        } else {
          fullPath = "/" + filename;
        }
        dirdx = fileIndex;  // store it
        fileIndex = 0;      // start with beginning of dir
      }
    }
  }
  // we chose some file
  Util_SafeStrCpy(g_state.sHDDDir.data(), fullPath.c_str(),
                  g_state.sHDDDir.size());
  Configuration::Instance().SetString(
      "Preferences", REGVALUE_PREF_HDD_START_DIR, g_state.sHDDDir.data());
  Configuration::Instance().Save();  // Save it

  fullPath += "/" + filename;

  // in future: save file name in registry for future fetching
  // for one drive will be one reg parameter
  HarddiskInsertCmd_t cmd{};
  cmd.drive = static_cast<uint8_t>(nDrive);
  strncpy(cmd.path, fullPath.c_str(), sizeof(cmd.path) - 1);

  if (Peripheral_Command(7, harddisk_cmd_insert, &cmd, sizeof(cmd)) ==
      PERIPHERAL_OK) {
    // save file names for HDD disk 1 or 2
    if (nDrive) {
      Configuration::Instance().SetString("Preferences", REGVALUE_HDD_IMAGE2,
                                          fullPath.c_str());
      Configuration::Instance().Save();
    } else {
      Configuration::Instance().SetString("Preferences", REGVALUE_HDD_IMAGE1,
                                          fullPath.c_str());
      Configuration::Instance().Save();
    }
    printf("HDD disk image %s inserted\n", fullPath.c_str());
  }
  backdx = fileIndex;  // Store cursor position
  DrawFrameWindow();
}
