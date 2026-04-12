#include "core/Common.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <sys/stat.h>

#include "apple2/Harddisk.h"
#include "apple2/Disk.h"
#include "apple2/DiskFTP.h"
#include "apple2/ftpparse.h"
#include "core/Common_Globals.h"
#include "core/Registry.h"
#include "frontends/sdl3/DiskChoose.h"
#include "frontends/sdl3/Frame.h"

// Note: Core hardware emulation logic moved to src/apple2/Harddisk.cpp
// This file only contains frontend UI functions.

void HD_FTP_Select(int nDrive)
{
  // Selects HDrive from FTP directory
  static size_t fileIndex = 0; // file index will be remembered for current dir
  static size_t backdx = 0;  //reserve
  static size_t dirdx = 0;  // reserve for dirs

  std::string filename;      // given filename
  std::string fullPath;  // full path for it
  bool isDirectory = true;      // if given filename is a directory?

  fileIndex = backdx;
  fullPath = g_state.sFTPServerHDD;  // global var for FTP path for HDD

  while (isDirectory) {
    if (!ChooseAnImageFTP(g_state.ScreenWidth, g_state.ScreenHeight, fullPath, 7,
                          filename, isDirectory, fileIndex)) {
      DrawFrameWindow();
      return;
    }
    // --
    if (isDirectory) {
      if (filename == "..") {
        // go to the upper directory
        auto r = fullPath.find_last_of(FTP_SEPARATOR);
        if (r == fullPath.size()-1) {
          r = fullPath.find_last_of(FTP_SEPARATOR, r-1);
        }
        if (r != std::string::npos) {
          fullPath = fullPath.substr(0, 1+r);
        }
        if (fullPath == "") {
          fullPath = "/";  //we don't want fullPath to be empty
        }
        fileIndex = dirdx;  // restore
      } else {
        if (fullPath != "/") {
          fullPath += filename + "/";
        } else {
          fullPath = "/" + filename + "/";
        }
        dirdx = fileIndex; // store it
        fileIndex = 0;  // start with beginning of dir
      }
    }
  }
  // we chose some file
  strcpy(g_state.sFTPServerHDD, fullPath.c_str());
  Configuration::Instance().SetString("Preferences", REGVALUE_FTP_HDD_DIR, g_state.sFTPServerHDD); Configuration::Instance().Save();// save it

  fullPath += "/" + filename;

  std::string localPath = std::string(g_state.sFTPLocalDir) + "/" + filename; // local path for file

  int error = ftp_get(fullPath.c_str(), localPath.c_str());
  if (!error) {
    if (HD_InsertDisk2(nDrive, localPath.c_str())) {
      // save file names for HDD disk 1 or 2
      if (nDrive) {
        Configuration::Instance().SetString("Preferences", REGVALUE_HDD_IMAGE2, localPath.c_str()); Configuration::Instance().Save();
      } else {
        Configuration::Instance().SetString("Preferences", REGVALUE_HDD_IMAGE1, localPath.c_str()); Configuration::Instance().Save();
      }
    }
  }
  backdx = fileIndex;  //store cursor position
  DrawFrameWindow();
}

void HD_Select(int nDrive)
{
  // Selects HDrive from file list
  static size_t fileIndex = 0; // file index will be remembered for current dir
  static size_t backdx = 0;  //reserve
  static size_t dirdx = 0;  // reserve for dirs

  std::string filename;      // given filename
  std::string fullPath;  // full path for it
  bool isDirectory;      // if given filename is a directory?

  fileIndex = backdx;
  isDirectory = true;
  fullPath = g_state.sHDDDir;  // global var for disk selecting directory

  while (isDirectory) {
    if (!ChooseAnImage(g_state.ScreenWidth, g_state.ScreenHeight, fullPath, 7,
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
          fullPath = "/";  //we don't want fullPath to be empty
        }
        fileIndex = dirdx;  // restore
      } else {
        if (fullPath != "/") {
          fullPath += "/" + filename;
        } else {
          fullPath = "/" + filename;
        }
        dirdx = fileIndex; // store it
        fileIndex = 0;  // start with beginning of dir
      }
    }
  }
  // we chose some file
  strcpy(g_state.sHDDDir, fullPath.c_str());
  Configuration::Instance().SetString("Preferences", REGVALUE_PREF_HDD_START_DIR, g_state.sHDDDir); Configuration::Instance().Save(); // Save it

  fullPath += "/" + filename;

  // in future: save file name in registry for future fetching
  // for one drive will be one reg parameter
  if (HD_InsertDisk2(nDrive, fullPath.c_str())) {
    // save file names for HDD disk 1 or 2
    if (nDrive) {
      Configuration::Instance().SetString("Preferences", REGVALUE_HDD_IMAGE2, fullPath.c_str()); Configuration::Instance().Save();
    } else {
      Configuration::Instance().SetString("Preferences", REGVALUE_HDD_IMAGE1, fullPath.c_str()); Configuration::Instance().Save();
    }
    printf("HDD disk image %s inserted\n", fullPath.c_str());
  }
  backdx = fileIndex; // Store cursor position
  DrawFrameWindow();
}
