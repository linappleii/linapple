#include "core/Common.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include "apple2/Disk.h"
#include "frontends/sdl3/DiskChoose.h"
#include "apple2/DiskFTP.h"
#include "apple2/ftpparse.h"
#include "frontends/sdl3/Frame.h"
#include "core/Common_Globals.h"
#include "core/Registry.h"
#include "core/Util_Path.h"

// Note: These were previously in Disk.cpp but are now in the frontend
// because they drive the UI (DiskChoose) and use frontend-specific logic.

void DiskSelectImage(int drive, char* pszFilename)
{
  (void)pszFilename;
  // Omit pszFilename??? for some reason or not!
  static size_t fileIndex = 0; // file index will be remembered for current dir
  static size_t backdx = 0;    //reserve
  static size_t dirdx = 0;     // reserve for dirs

  std::string filename;
  std::string fullPath;
  bool isdir;

  fileIndex = backdx;
  isdir = true;
  fullPath = g_state.sCurrentDir;

  while (isdir) {
    if (!ChooseAnImage(g_state.ScreenWidth, g_state.ScreenHeight, fullPath, 6,
                       filename, isdir, fileIndex)) {
      DrawFrameWindow();
      return;
    }
    if (isdir) {
      if (filename == "..")
      {
        const auto last_sep_pos = fullPath.find_last_of(FILE_SEPARATOR);
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
  strcpy(g_state.sCurrentDir, fullPath.c_str());
  Configuration::Instance().SetString("Preferences", REGVALUE_PREF_START_DIR, g_state.sCurrentDir);
  Configuration::Instance().Save();

  fullPath += "/" + filename;

  int error = DiskInsert(drive, fullPath.c_str(), false, true);
  if (!error) {
    // for one drive will be one reg parameter
    //  RegSaveString("Preferences",REGVALUE_PREF_START_DIR, 1,filename);
    if (drive == 0) {
      Configuration::Instance().SetString("Preferences", REGVALUE_DISK_IMAGE1, fullPath.c_str());
    }
    else {
      Configuration::Instance().SetString("Preferences", REGVALUE_DISK_IMAGE2, fullPath.c_str());
    }
    Configuration::Instance().Save();
  } else {
    DiskNotifyInvalidImage(filename.c_str(), error); // show error on the screen (or in console for our case)
  }
  backdx = fileIndex;  //store cursor position
  DrawFrameWindow();
}

void DiskSelect(int drive)
{
  char szSelect[] = "";
  DiskSelectImage(drive, szSelect);  // drive is 0 for D1, 1 - for D2
}


void Disk_FTP_SelectImage(int drive)  // select a disk image using FTP
{
  //  omit pszFilename??? for some reason or not!
  static size_t fileIndex = 0; // file index will be remembered for current dir
  static size_t backdx = 0;  //reserve
  static size_t dirdx = 0;  // reserve for dirs

  std::string filename;      // given filename
  std::string fullPath;  // full path for it
  bool isdir;      // if given filename is a directory?

  struct stat info;

  fileIndex = backdx;
  isdir = true;
  fullPath = g_state.sFTPServer;  // global var for FTP path

  while (isdir) {
    if (!ChooseAnImageFTP(g_state.ScreenWidth, g_state.ScreenHeight, fullPath, 6,
                          filename, isdir, fileIndex)) {
      DrawFrameWindow();
      return;  // if ESC was pressed, just leave
    }
    if (isdir) {
      if (filename == "..") {
        // go to the upper directory
        auto r = fullPath.find_last_of(FTP_SEPARATOR);
        if (r == fullPath.size() - 1) { // found: look for 2nd last
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
    }/* if isdir */
  } /* while isdir */
  // we chose some file
  strcpy(g_state.sFTPServer, fullPath.c_str());
  Configuration::Instance().SetString("Preferences", REGVALUE_FTP_DIR, g_state.sFTPServer);
  Configuration::Instance().Save();

  fullPath += "/" + filename;

  std::string localPath = std::string(g_state.sFTPLocalDir) + "/" +  filename; // local path for file

  int error;

  // One moment - if we have some file with the same name
  // we do not want to download it again
  if (stat(localPath.c_str(), &info) == 0) {
    error = 0; // use this file
  } else {
    error = ftp_get(fullPath.c_str(), localPath.c_str()); // 0 on success
  }

  if (!error) {
    error = DiskInsert(drive, localPath.c_str(), false, true);// try to insert downloaded file as a disk image
    if (!error) {
    } else {
      DiskNotifyInvalidImage(filename.c_str(), error); // show error on the screen (or in console for our case)
    }
  } else
    printf("Error downloading file %s\n", localPath.c_str());

  backdx = fileIndex;  //store cursor position
  DrawFrameWindow();
}
