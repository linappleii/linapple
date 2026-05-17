#include <SDL3/SDL.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskFTP.h"
#include "apple2/peripherals/disk/ftpparse.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "frontends/sdl3/DiskChoose.h"
#include "frontends/sdl3/DiskUI.h"
#include "frontends/sdl3/Frame.h"

void DiskSelectImage(int drive, char* pszFilename) {
  (void)pszFilename;
  static size_t fileIndex = 0;
  static size_t backdx = 0;
  static size_t dirdx = 0;

  std::string filename;
  std::string fullPath;
  bool isdir = false;

  fileIndex = backdx;
  isdir = true;
  fullPath = g_state.sCurrentDir.data();

  while (isdir) {
    if (!ChooseAnImage(g_state.ScreenWidth, g_state.ScreenHeight, fullPath, 6,
                       filename, isdir, fileIndex)) {
      DrawFrameWindow();
      return;
    }
    if (isdir) {
      if (filename == "..") {
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
  Util_SafeStrCpy(g_state.sCurrentDir.data(), fullPath.c_str(),
                  g_state.sCurrentDir.size());
  Configuration::Instance().SetString("Preferences", REGVALUE_PREF_START_DIR,
                                      g_state.sCurrentDir.data());
  Configuration::Instance().Save();

  fullPath += "/" + filename;

  DiskInsertCmd_t cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  Util_SafeStrCpy(cmd.path, fullPath.c_str(), sizeof(cmd.path));
  cmd.write_protected = 0;
  cmd.create_if_necessary = 1;

  Peripheral_Command(disk_default_slot, disk_cmd_insert, &cmd, sizeof(cmd));

  backdx = fileIndex;
  DrawFrameWindow();
}

void DiskSelect(int drive) {
  char szSelect[] = "";
  DiskSelectImage(drive, szSelect);  // drive is 0 for D1, 1 - for D2
}

void Disk_FTP_SelectImage(int drive) {
  // FTP selection logic...
  // For now, this is a placeholder/stub to be refined in later milestones.
  (void)drive;
}
