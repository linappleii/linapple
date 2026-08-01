#include <SDL/SDL.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskFTP.h"
#include "apple2/peripherals/disk/ftpparse.h"
#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "frontends/sdl1/DiskChoose.h"
#include "frontends/sdl1/DiskUI.h"
#include "frontends/sdl1/Frame.h"

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
  fullPath = g_state.current_dir.data();

  while (isdir) {
    constexpr int disk_choose_slot = 6;
    if (!choose_an_image(static_cast<int>(g_state.ScreenWidth),
                       static_cast<int>(g_state.ScreenHeight), fullPath,
                       disk_choose_slot, filename, isdir, fileIndex)) {
      DrawFrameWindow();
      return;
    }
    if (isdir) {
      if (filename == "..") {
        const auto last_sep_pos = fullPath.find_last_of(file_separator);
        if (last_sep_pos != std::string::npos) {
          fullPath = fullPath.substr(0, last_sep_pos);
        }
        if (fullPath.empty()) {
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
  Util_SafeStrCpy(g_state.current_dir.data(), fullPath.c_str(),
                  g_state.current_dir.size());
  Configuration_t::instance().set_string("Preferences", REGVALUE_PREF_START_DIR,
                                      g_state.current_dir.data());
  Configuration_t::instance().save();

  fullPath += "/" + filename;

  DiskInsertCmd_t cmd{};
  cmd.drive = static_cast<uint8_t>(drive);
  Util_SafeStrCpy(cmd.path, fullPath.c_str(), sizeof(cmd.path));
  cmd.write_protected = 0;
  cmd.create_if_necessary = 1;

  peripheral_command(disk_default_slot, disk_cmd_insert, &cmd, sizeof(cmd));

  backdx = fileIndex;
  DrawFrameWindow();
}

void DiskSelect(int drive) {
  std::array<char, 1> szSelect = {{'\0'}};
  DiskSelectImage(drive, szSelect.data());  // drive is 0 for D1, 1 - for D2
}

void Disk_FTP_SelectImage(int drive) {
  // FTP selection logic...
  // For now, this is a placeholder/stub to be refined in later milestones.
  (void)drive;
}
