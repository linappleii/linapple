#include "core/Common_Globals.h"
/*
linapple : An Apple //e emulator for Linux

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <SDL/SDL.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "Debugger/Debug.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SaveState.h"
#include "apple2/SoundCore.h"
#include "apple2/Video.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "apple2/peripherals/printer/Printer.h"
#include "apple2/peripherals/super_serial_card/SuperSerial.h"
#include "apple2/stretch.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "core/asset.h"
#include "frontends/sdl1/DiskChoose.h"
#include "frontends/sdl1/DiskUI.h"
#include "frontends/sdl1/Frame.h"
#include "frontends/sdl1/SDL_Video.h"

SDL_Surface* apple_icon;
SDL_Surface* g_screen = nullptr;
SDL_Surface* g_texture = nullptr;
SDL_Rect origRect;
SDL_Rect newRect;

enum { BUTTONY = 0, BUTTONCX = 45, BUTTONCY = 45 };

static bool g_bAppActive = false;

static DiskStatus_t g_lastDiskStatus{};
static int g_drive0_last_reported_error = disk_err_none;
static int g_drive1_last_reported_error = disk_err_none;

int buttondown = -1;

bool g_WindowResized;

bool usingcursor = false;

void DrawStatusArea(int drawflags);

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): button and mod are
// semantically distinct
void ProcessButtonClick(int button, int mod);

void ResetMachineState();

void SetFullScreenMode();

void SetNormalMode();

void SetUsingCursor(bool);

void SetIcon();

bool g_bScrollLock_FullSpeed = false;

void DrawAppleContent() {
  g_video_draw_mutex.lock();
  VideoRealizePalette();

  DrawStatusArea(DRAW_BACKGROUND | DRAW_LEDS);

  if (g_state.mode == MODE_LOGO) {
    VideoDisplayLogo();
    g_bFrameReady = true;
  } else if (g_state.mode == MODE_DEBUG) {
    DebugDisplay(true);
    g_bFrameReady = true;
  } else {
    VideoRedrawScreen();
  }
  g_video_draw_mutex.unlock();
}

void FrameRefresh() {
  if (g_texture != nullptr && g_screen != nullptr) {
    SDL_BlitSurface(g_texture, nullptr, g_screen, nullptr);
    SDL_Flip(g_screen);
  }
}

static inline auto ToVideoRect(const SDL_Rect& r) -> VideoRect {
  return {static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.w),
          static_cast<int>(r.h)};
}

void DrawFrameWindow() {
  if (g_bFrameReady == false) return;

  g_video_draw_mutex.lock();
  if (g_texture != nullptr && g_screen != nullptr) {
    uint32_t* output = VideoGetOutputBuffer();
    SDL_Rect r = {0, 0, 560, 384};

    // Fill g_screen from RGB32 output buffer
    if (g_state.mode != MODE_DEBUG) {
      VideoSurface vs_texture = SDLSurfaceToVideoSurface(g_texture);
      VideoSurface vs_output{};
      vs_output.pixels = reinterpret_cast<uint8_t*>(output);
      vs_output.w = 560;
      vs_output.h = 384;
      vs_output.pitch = 560 * 4;
      vs_output.bpp = 4;

      if (g_WindowResized == false) {
        VideoRect vr = ToVideoRect(r);
        VideoSoftStretch(&vs_output, &vr, &vs_texture, &vr);
      } else {
        VideoRect vor = ToVideoRect(origRect);
        VideoRect vnr = ToVideoRect(newRect);
        VideoSoftStretch(&vs_output, &vor, &vs_texture, &vnr);
      }
    } else {
      // Debugger draws directly to g_hDebugScreen (INDEX8)
      // We need to stretch/convert it to the RGB32 g_screen surface
      extern VideoSurface* g_hDebugScreen;
      if (g_hDebugScreen != nullptr) {
        VideoSurface vs_texture = SDLSurfaceToVideoSurface(g_texture);
        if (g_WindowResized == false) {
          VideoRect vr = ToVideoRect(r);
          VideoSoftStretch(g_hDebugScreen, &vr, &vs_texture, &vr);
        } else {
          VideoRect vor = ToVideoRect(origRect);
          VideoRect vnr = ToVideoRect(newRect);
          VideoSoftStretch(g_hDebugScreen, &vor, &vs_texture, &vnr);
        }
      }
    }

    FrameRefresh();
    g_bFrameReady = false;
  }
  g_video_draw_mutex.unlock();
}

void DrawStatusArea(int drawflags) {
  if (font_sfc == nullptr) {
    if (fonts_initialization() == false) {
      fprintf(stderr, "Font file was not loaded.\n");
      return;
    }
  }

  VideoRect srect{};
  uint8_t mybluez = DARK_BLUE;

  if ((drawflags & DRAW_BACKGROUND) != 0) {
    g_iStatusCycle = SHOW_CYCLES;
  }
  if ((drawflags & DRAW_LEDS) != 0) {
    srect.x = 4;
    srect.y = 22;
    srect.w = static_cast<int16_t>(STATUS_PANEL_W - 8);
    srect.h = static_cast<int16_t>(STATUS_PANEL_H - 25);

    for (int y = srect.y; y < srect.y + srect.h; ++y) {
      memset(g_hStatusSurface->pixels +
                 static_cast<ptrdiff_t>(y * g_hStatusSurface->pitch) + srect.x,
             mybluez, static_cast<size_t>(srect.w));
    }

    std::array<char, 2> leds = {{"\x64"}};
    constexpr int LED_CHAR_BASE = 1;
    int iDrive1Status = disk_status_off;
    int iDrive2Status = disk_status_off;
    int iHDDStatus = disk_status_off;

    if (g_lastDiskStatus.drive0_spinning != 0) {
      iDrive1Status = (g_lastDiskStatus.drive0_writing != 0) ? disk_status_write
                                                             : disk_status_read;
    } else if (g_lastDiskStatus.drive0_loaded != 0 &&
               g_lastDiskStatus.drive0_write_protected != 0) {
      iDrive1Status = disk_status_prot;
    }

    if (g_lastDiskStatus.drive1_spinning != 0) {
      iDrive2Status = (g_lastDiskStatus.drive1_writing != 0) ? disk_status_write
                                                             : disk_status_read;
    } else if (g_lastDiskStatus.drive1_loaded != 0 &&
               g_lastDiskStatus.drive1_write_protected != 0) {
      iDrive2Status = disk_status_prot;
    }

    HarddiskStatus_t hstatus{};
    size_t hsize = sizeof(hstatus);
    if (Peripheral_Query(7, harddisk_cmd_get_status, &hstatus, &hsize) ==
        PERIPHERAL_OK) {
      iHDDStatus = hstatus.activity_status;
    }

    leds.at(0) = static_cast<char>(LED_CHAR_BASE + iDrive1Status);
    font_print(8, 23, leds.data(), g_hStatusSurface, 4.0f, 2.7f);

    leds.at(0) = static_cast<char>(LED_CHAR_BASE + iDrive2Status);
    font_print(40, 23, leds.data(), g_hStatusSurface, 4.0f, 2.7f);

    leds.at(0) = static_cast<char>(LED_CHAR_BASE + iHDDStatus);
    font_print(71, 23, leds.data(), g_hStatusSurface, 4.0f, 2.7f);

    if ((iDrive1Status | iDrive2Status | iHDDStatus) != 0) {
      g_iStatusCycle = SHOW_CYCLES;
    }
  }
}

void FrameShowHelpScreen(int sx, int sy) {
  (void)sy;
  constexpr int MAX_LINES = 25;
  static const std::array<const char*, MAX_LINES> HelpStrings = {
      {"Welcome to LinApple - Apple][ emulator for Linux!",
       "Conf file is linapple.conf in current directory by default",
       "Hugest archive of Apple][ stuff you can find at ftp.apple.asimov.net",
       "       F1 - Show help g_screen",
       "  Ctrl+F2 - Cold reboot (Power off and back on)",
       " Shift+F2 - Reload configuration file and cold reboot",
       " Ctrl+F10 - Hot Reset (Control+Reset)",
       "      F12 - Quit",
       "",
       "    F3/F4 - Load floppy disk 1/2 (Slot 6, Drive 1/2)",
       "       F5 - Swap floppy disks",
       " Shift+F3/F4 - Attach hard drive 1/2 (Slot 7, Drive 1/2)",
       "",
       "       F6 - Toggle g_state.fullscreen mode",
       " Shift+F6 - Toggle character set (keyboard rocker switch)",
       "       F7 - Toggle debugging view",
       "       F8 - Take g_screenshot",
       " Shift+F8 - Save runtime changes to configuration file",
       "       F9 - Cycle through various video modes",
       " Shift+F9 - Budget video, for smoother music/audio",
       "  F10/F11 - Load/save snapshot file",
       "",
       "       Pause - Pause/resume emulator",
       " Scroll Lock - Toggle full speed",
       "  Numpad +/-/* - Increase/Decrease/Normal speed"}};

  VideoSurface* tempSurface = nullptr;

  if (font_sfc == nullptr) {
    if (fonts_initialization() == false) {
      fprintf(stderr, "Font file was not loaded.\n");
      return;
    }
  }
  if (g_WindowResized == false) {
    if (g_state.mode == MODE_LOGO) {
      tempSurface = g_hLogoBitmap;
    } else {
      tempSurface = g_hDeviceBitmap;
    }
  } else {
    tempSurface = g_origscreen;
  }

  if (tempSurface == nullptr) {
    // Wrap g_screen as fallback
    static VideoSurface vs_g_screen;
    vs_g_screen = SDLSurfaceToVideoSurface(g_screen);
    tempSurface = &vs_g_screen;
  }

  VideoSurface vs_actual_g_screen = SDLSurfaceToVideoSurface(g_screen);

  // Capture original g_screen
  VideoSoftStretch(tempSurface, nullptr, &vs_actual_g_screen, nullptr);

  // Blur the background by downscaling and upscaling
  // We use a small temporary surface (1/16 size) to create a pixelated blur
  // effect
  SDL_Surface* blur_temp =
      SDL_CreateRGBSurface(0, g_screen->w / 16, g_screen->h / 16, 32,
                           0x00FF0000, 0x0000FF00, 0x000000FF, 0);
  if (blur_temp != nullptr) {
    VideoSurface vs_blur = SDLSurfaceToVideoSurface(blur_temp);
    VideoSoftStretch(&vs_actual_g_screen, nullptr, &vs_blur,
                     nullptr);  // Downscale
    VideoSoftStretch(&vs_blur, nullptr, &vs_actual_g_screen,
                     nullptr);  // Upscale back
    SDL_FreeSurface(blur_temp);
  }

  // Dim the background using SDL blending for better text readability
  SDL_Surface* dim_surface = SDL_CreateRGBSurface(
      0, g_screen->w, g_screen->h, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);
  if (dim_surface != nullptr) {
    Uint32 dim_color = SDL_MapRGBA(dim_surface->format, 0, 0, 0, 200);
    SDL_FillRect(dim_surface, nullptr, dim_color);
    SDL_SetAlpha(dim_surface, SDL_SRCALPHA, 200);
    SDL_BlitSurface(dim_surface, nullptr, g_screen, nullptr);
    SDL_FreeSurface(dim_surface);
  }

  const float facx_f = static_cast<float>(g_state.ScreenWidth) /
                       static_cast<float>(SCREEN_WIDTH);
  const float facy_f = static_cast<float>(g_state.ScreenHeight) /
                       static_cast<float>(SCREEN_HEIGHT);
  const double facy = static_cast<double>(facy_f);

  font_print_centered(sx / 2, static_cast<int>(5.0 * facy),
                      const_cast<char*>(HelpStrings.at(0)), &vs_actual_g_screen,
                      1.5f * facx_f, 1.3f * facy_f);
  font_print_centered(sx / 2, static_cast<int>(20.0 * facy),
                      const_cast<char*>(HelpStrings.at(1)), &vs_actual_g_screen,
                      1.3f * facx_f, 1.2f * facy_f);
  font_print_centered(sx / 2, static_cast<int>(30.0 * facy),
                      const_cast<char*>(HelpStrings.at(2)), &vs_actual_g_screen,
                      1.2f * facx_f, 1.0f * facy_f);

  int Help_TopX = static_cast<int>(45.0 * facy);
  for (int i = 3; i < MAX_LINES; i++) {
    if (HelpStrings.at(i) != nullptr) {
      font_print(4,
                 static_cast<int>(static_cast<double>(Help_TopX) +
                                  static_cast<double>(i - 3) * 15.0 * facy),
                 const_cast<char*>(HelpStrings.at(i)), &vs_actual_g_screen,
                 1.5f * facx_f, 1.5f * facy_f);
    }
  }

  rectangle(&vs_actual_g_screen, 0, Help_TopX - 5,
            static_cast<int>(g_state.ScreenWidth - 1),
            static_cast<int>(335.0 * facy), 0xFFFFFF);
  rectangle(&vs_actual_g_screen, 1, Help_TopX - 4,
            static_cast<int>(g_state.ScreenWidth),
            static_cast<int>(335.0 * facy), 0xFFFFFF);
  rectangle(&vs_actual_g_screen, 1, 1,
            static_cast<int>(g_state.ScreenWidth - 2), (Help_TopX - 8),
            0xFFFF00);

  // Logo bit
  VideoSurface vs_icon{};
  vs_icon.pixels =
      static_cast<uint8_t*>((static_cast<SDL_Surface*>(assets->icon))->pixels);
  vs_icon.w =
      static_cast<uint16_t>((static_cast<SDL_Surface*>(assets->icon))->w);
  vs_icon.h =
      static_cast<uint16_t>((static_cast<SDL_Surface*>(assets->icon))->h);
  vs_icon.pitch =
      static_cast<uint16_t>((static_cast<SDL_Surface*>(assets->icon))->pitch);
  vs_icon.bpp = 4;  // Assuming RGB32

  VideoRect logo{};
  VideoRect scrr{};
  logo.x = logo.y = 0;
  logo.w = vs_icon.w;
  logo.h = vs_icon.h;
  scrr.x = static_cast<int16_t>(460.0f * facx_f);
  scrr.y = static_cast<int16_t>(270.0f * facy_f);
  scrr.w = static_cast<int16_t>(100.0f * facy_f);
  scrr.h = static_cast<int16_t>(100.0f * facy_f);
  VideoSoftStretchOr(&vs_icon, &logo, &vs_actual_g_screen, &scrr);

  FrameRefresh();
  SDL_Delay(1000);

  SDL_Event event;

  event.type = SDL_QUIT;
  while (event.type != SDL_KEYDOWN) {
    usleep(100);
    SDL_PollEvent(&event);
  }

  DrawFrameWindow();
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): num and mod are
// semantically distinct
void FrameQuickState(int num, int mod) {
  // quick load or save state with number num, if Shift is pressed, state is
  // being saved, otherwise - being loaded
  std::array<char, path_max_len> fpath;
  snprintf(fpath.data(), fpath.size(), "%.*s/SaveState%d.aws",
           static_cast<int>(strlen(g_state.sSaveStateDir.data())),
           g_state.sSaveStateDir.data(), num);
  Snapshot_SetFilename(fpath.data());
  if ((mod & KMOD_SHIFT) != 0) {
    Snapshot_SaveState();
  } else {
    Snapshot_LoadState();
  }
}

auto IsModifierKey(SDLKey sym) -> bool {
  switch (sym) {
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
    case SDLK_LCTRL:
    case SDLK_RCTRL:
    case SDLK_LALT:
    case SDLK_RALT:
    case SDLK_LSUPER:
    case SDLK_RSUPER:
    case SDLK_CAPSLOCK:
      return true;
    default:
      return false;
  }
}

void Frame_OnResize(int width, int height) {
  g_video_draw_mutex.lock();
  g_state.ScreenWidth = static_cast<uint32_t>(width);
  g_state.ScreenHeight = static_cast<uint32_t>((height / 96) * 96);
  if (g_state.ScreenHeight < 192) {
    g_state.ScreenHeight = 192;
  }

  Uint32 flags = SDL_SWSURFACE | SDL_RESIZABLE;
  if (g_state.fullscreen) flags |= SDL_FULLSCREEN;

  g_screen =
      SDL_SetVideoMode(static_cast<int>(g_state.ScreenWidth),
                       static_cast<int>(g_state.ScreenHeight), 32, flags);

  if (g_texture != nullptr) SDL_FreeSurface(g_texture);
  g_texture = SDL_CreateRGBSurface(0, g_state.ScreenWidth, g_state.ScreenHeight,
                                   32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);

  if (g_screen == nullptr || g_texture == nullptr) {
    g_video_draw_mutex.unlock();
    SDL_Quit();
    return;
  }
  g_WindowResized = (g_state.ScreenWidth != SCREEN_WIDTH) |
                    (g_state.ScreenHeight != SCREEN_HEIGHT);
  if (g_WindowResized) {
    origRect.x = origRect.y = newRect.x = newRect.y = 0;
    origRect.w = static_cast<int16_t>(SCREEN_WIDTH);
    origRect.h = static_cast<int16_t>(SCREEN_HEIGHT);
    newRect.w = static_cast<int16_t>(g_state.ScreenWidth);
    newRect.h = static_cast<int16_t>(g_state.ScreenHeight);
    if ((g_state.mode != MODE_LOGO) && (g_state.mode != MODE_DEBUG)) {
      VideoRedrawScreen();
    }
  }
  g_video_draw_mutex.unlock();
}

void Frame_OnFocus(bool gained) {
  g_bAppActive = gained;
  if (g_bAppActive) {
    // Re-sync Caps Lock state upon regaining focus
    SDLMod mod = SDL_GetModState();
    uint8_t caps = ((mod & KMOD_CAPS) != 0) ? 1 : 0;
    Peripheral_Command(0, keyboard_cmd_set_caps, &caps, 1);
  }
}

void Frame_OnExpose() {
  if ((g_state.mode != MODE_LOGO) && (g_state.mode != MODE_DEBUG)) {
    VideoRedrawScreen();
  }
}

auto PSP_SaveStateSelectImage(bool saveit) -> bool {
  static size_t fileIndex = 0;
  static int backdx = 0;
  static int dirdx = 0;

  std::string filename;  // given filename
  std::string fullPath;  // full path for it
  bool isDirectory = true;

  fileIndex = static_cast<size_t>(backdx);
  fullPath = g_state.sSaveStateDir.data();

  while (isDirectory) {
    if (ChooseAnImage(g_state.ScreenWidth, g_state.ScreenHeight, fullPath,
                      saveit ? 1 : 0, filename, isDirectory,
                      fileIndex) == false) {
      DrawFrameWindow();
      return false;
    }
    if (isDirectory) {
      if (filename == "..") {
        const auto last_sep_pos = fullPath.find_last_of(FILE_SEPARATOR);
        if (last_sep_pos == std::string::npos) {
          fullPath = fullPath.substr(0, last_sep_pos);
        }
        if (fullPath.empty()) {
          fullPath = "/";
        }
        fileIndex = static_cast<size_t>(dirdx);
      } else {
        if (fullPath != "/") {
          fullPath += "/" + filename;
        } else {
          fullPath = "/" + filename;
        }
        dirdx = static_cast<int>(fileIndex);
        fileIndex = 0;
      }
    }
  }
  Util_SafeStrCpy(g_state.sSaveStateDir.data(), fullPath.c_str(),
                  g_state.sSaveStateDir.size());
  Configuration::Instance().SetString("Preferences", "Save State Directory",
                                      g_state.sSaveStateDir.data());
  Configuration::Instance().Save();

  backdx = static_cast<int>(fileIndex);

  fullPath += "/" + filename;

  Snapshot_SetFilename(fullPath.c_str());
  Configuration::Instance().SetString(
      "Preferences", REGVALUE_SAVESTATE_FILENAME, fullPath.c_str());
  Configuration::Instance().Save();
  DrawFrameWindow();
  return true;
}

void FrameSaveBMP() {
  // Save current g_screen as a .bmp file in current directory
  struct stat bufp{};
  static int i = 1;
  std::array<char, 20> bmpName;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
  snprintf(bmpName.data(), bmpName.size(), "linapple%7d.bmp", i);
  while (stat(bmpName.data(), &bufp) == 0) {
    i++;
    snprintf(bmpName.data(), bmpName.size(), "linapple%7d.bmp", i);
  }
#pragma GCC diagnostic pop

  SDL_SaveBMP(g_screen, bmpName.data());
  printf("File %s saved!\n", bmpName.data());
  i++;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): button and mod are
// semantically distinct
void ProcessButtonClick(int button, int mod) {
  SDL_Event qe;

  SoundCore_SetFade(FADE_OUT);

  switch (button) {
    case BTN_HELP:
      FrameShowHelpScreen(g_screen->w, g_screen->h);
      break;

    case BTN_RUN:
      if ((mod & (KMOD_LCTRL)) == (KMOD_LCTRL) ||
          (mod & (KMOD_RCTRL)) == (KMOD_RCTRL)) {
        if (g_state.mode == MODE_LOGO) {
          Peripheral_Command(disk_default_slot, disk_cmd_boot, nullptr, 0);
        } else if (g_state.mode == MODE_RUNNING) {
          ResetMachineState();
        }
        if ((g_state.mode == MODE_DEBUG) || (g_state.mode == MODE_STEPPING)) {
          DebugEnd();
        }
        g_state.mode = MODE_RUNNING;
        DrawStatusArea(DRAW_TITLE);
        VideoRedrawScreen();
        g_state.bResetTiming = true;
      } else if ((mod & KMOD_SHIFT) != 0) {
        g_state.restart = true;
        qe.type = SDL_QUIT;
        SDL_PushEvent(&qe);
      }
      break;

    case BTN_DRIVE1:
    case BTN_DRIVE2:
      Peripheral_Command(0, JOY_CMD_RESET, nullptr, 0);
      if ((mod & KMOD_CTRL) != 0) {
        if ((mod & KMOD_SHIFT) != 0) {
          printf("HDD  Eject Drive #%d\n", (button - BTN_DRIVE1) + 1);
          HarddiskEjectCmd_t ecmd = {static_cast<uint8_t>(button - BTN_DRIVE1)};
          Peripheral_Command(7, harddisk_cmd_eject, &ecmd, sizeof(ecmd));
        } else {
          printf("Disk Eject Drive #%d\n", (button - BTN_DRIVE1) + 1);
          DiskEjectCmd_t ecmd{};
          ecmd.drive = static_cast<uint8_t>(button - BTN_DRIVE1);
          Peripheral_Command(disk_default_slot, disk_cmd_eject, &ecmd,
                             sizeof(ecmd));
        }
        break;
      }

      if ((mod & KMOD_SHIFT) != 0) {
        if ((mod & KMOD_ALT) != 0) {
          HarddiskUI_FTPSelect(button - BTN_DRIVE1);
        } else {
          HarddiskUI_Select(button - BTN_DRIVE1);
        }
      } else {
        extern void DiskSelect(int drive);
        extern void Disk_FTP_SelectImage(int drive);
        if ((mod & KMOD_ALT) != 0) {
          Disk_FTP_SelectImage(button - BTN_DRIVE1);
        } else {
          DiskSelect(button - BTN_DRIVE1);
        }
      }
      break;

    case BTN_DRIVESWAP:
      Peripheral_Command(disk_default_slot, disk_cmd_swap_drives, nullptr, 0);
      break;

    case BTN_FULLSCR:
      if ((mod & KMOD_SHIFT) != 0) {
        // only IIe and enhanced have a keyboard rocker switch (and only non-US
        // keyboards)
        if ((g_Language != A2LANG_US) &&
            ((g_Apple2Type == A2TYPE_APPLE2E) ||
             (g_Apple2Type == A2TYPE_APPLE2EENHANCED))) {
          uint8_t cur_rocker = 0;
          size_t rocker_sz = sizeof(cur_rocker);
          Peripheral_Query(0, keyboard_query_rocker, &cur_rocker, &rocker_sz);
          uint8_t new_rocker = (cur_rocker != 0) ? 0 : 1;
          Peripheral_Command(0, keyboard_cmd_set_rocker, &new_rocker, 1);
          printf(
              "Toggling keyboard rocker switch. Selected character set: "
              "%s...\n",
              (new_rocker != 0) ? "local" : "standard/US");
        }
      } else {
        if (g_state.fullscreen) {
          g_state.fullscreen = false;
          SetNormalMode();
        } else {
          g_state.fullscreen = true;
          SetFullScreenMode();
        }
        Peripheral_Command(0, JOY_CMD_RESET, nullptr, 0);
      }
      break;

    case BTN_DEBUG:
      if (g_state.mode != MODE_DEBUG) {
        DebugBegin();
        SetUsingCursor(false);
      } else if (g_state.mode == MODE_DEBUG) {
        g_state.mode = MODE_RUNNING;
      }
      break;

    case BTN_SETUP:
      if ((mod & KMOD_SHIFT) != 0) {
        Configuration::Instance().SetInt("Configuration", "Video Emulation",
                                         static_cast<int>(g_videotype));
        Configuration::Instance().SetInt("Configuration", "Emulation Speed",
                                         static_cast<int>(g_state.dwSpeed));
        Configuration::Instance().SetInt("Configuration", "Fullg_screen",
                                         g_state.fullscreen ? 1 : 0);
        Configuration::Instance().Save();

      } else {
        FrameSaveBMP();
      }
      break;

    case BTN_CYCLE:
      if ((mod & KMOD_SHIFT) != 0) {
        SetBudgetVideo(!GetBudgetVideo());
      } else {
        g_videotype++;
        if (g_videotype >= VT_NUM_MODES) {
          g_videotype = 0;
        }
        VideoReinitialize();
        if (g_state.mode != MODE_LOGO) {
          if (g_state.mode == MODE_DEBUG) {
            uint32_t debugVideoMode = 0;
            if (DebugGetVideoMode(&debugVideoMode)) {
              VideoRefreshScreen();
            }
          } else {
            VideoRefreshScreen();
          }
        }
      }
      break;
    case BTN_QUIT:
      qe.type = SDL_QUIT;
      SDL_PushEvent(&qe);
      break;
    case BTN_SAVEST:
      if ((mod & KMOD_ALT) != 0) {
        Snapshot_SaveState();
      } else if (PSP_SaveStateSelectImage(true)) {
        Snapshot_SaveState();
      }
      break;
    case BTN_LOADST:
      if ((mod & KMOD_CTRL) != 0) {
        if (IS_APPLE2() == false) {
          MemResetPaging();
        }

        Peripheral_Manager_Reset();
        if (IS_APPLE2() == false) {
          VideoResetState();
        }
        CpuReset();
      } else if ((mod & KMOD_ALT) != 0) {
        Snapshot_LoadState();
      } else if (PSP_SaveStateSelectImage(false)) {
        Snapshot_LoadState();
      }
      break;
  }

  if ((g_state.mode != MODE_DEBUG) && (g_state.mode != MODE_PAUSED)) {
    SoundCore_SetFade(FADE_IN);
  }
}

void ResetMachineState() {
  g_bFullSpeed = false;  // Might've hit reset in middle of InternalCpuExecute()
                         // - so beep may get (partially) muted

  MemReset();
  Peripheral_Manager_Reset();
  Peripheral_Command(disk_default_slot, disk_cmd_boot, nullptr, 0);
  VideoResetState();
  Peripheral_Command(0, JOY_CMD_RESET, nullptr, 0);
}

static bool bIamFullScreened;

void SetFullScreenMode() {
  if (bIamFullScreened == false) {
    bIamFullScreened = true;
    SDL_WM_ToggleFullScreen(g_screen);
    if (g_state.mode != MODE_DEBUG) {
      SDL_ShowCursor(SDL_DISABLE);
    }
  }
}

void SetNormalMode() {
  if (bIamFullScreened) {
    bIamFullScreened = false;
    SDL_WM_ToggleFullScreen(g_screen);
    if (usingcursor == false) {
      SDL_ShowCursor(SDL_ENABLE);
    }
  } else if (g_state.mode == MODE_DEBUG) {
    SDL_ShowCursor(SDL_ENABLE);
    SDL_WM_GrabInput(SDL_GRAB_OFF);
  }
}

void SetUsingCursor(bool newvalue) {
  usingcursor = newvalue;
  if (usingcursor) {
    SDL_ShowCursor(SDL_DISABLE);
    SDL_WM_GrabInput(SDL_GRAB_ON);
  } else {
    if ((bIamFullScreened == false) || (g_state.mode == MODE_DEBUG)) {
      SDL_ShowCursor(SDL_ENABLE);
    }
    SDL_WM_GrabInput(SDL_GRAB_OFF);
  }
}

extern void SDL_Asset_LoadIcon();
extern void SDL_Asset_FreeIcon();

auto FrameCreateWindow() -> int {
  SDL_Asset_LoadIcon();
  bIamFullScreened = false;

  Uint32 flags = SDL_SWSURFACE;
  if (g_state.fullscreen) flags |= SDL_FULLSCREEN;

  g_screen =
      SDL_SetVideoMode(static_cast<int>(g_state.ScreenWidth),
                       static_cast<int>(g_state.ScreenHeight), 32, flags);
  if (g_screen == nullptr) {
    fprintf(stderr, "Could not set video mode: %s\n", SDL_GetError());
    return 1;
  }

  g_texture = SDL_CreateRGBSurface(0, g_state.ScreenWidth, g_state.ScreenHeight,
                                   32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);

  SDL_WM_SetCaption(g_pAppTitle, g_pAppTitle);
  SetIcon();

  g_WindowResized = (g_state.ScreenWidth != SCREEN_WIDTH) |
                    (g_state.ScreenHeight != SCREEN_HEIGHT);
  printf("Screen size is %ux%u\n", g_state.ScreenWidth, g_state.ScreenHeight);
  if (g_WindowResized) {
    origRect.x = origRect.y = newRect.x = newRect.y = 0;
    origRect.w = static_cast<int16_t>(SCREEN_WIDTH);
    origRect.h = static_cast<int16_t>(SCREEN_HEIGHT);
    newRect.w = static_cast<int16_t>(g_state.ScreenWidth);
    newRect.h = static_cast<int16_t>(g_state.ScreenHeight);
  }
  return 0;
}

void SetIcon() {
  /* Black is the transparency colour.
     Part of the logo seems to use it !? */
  auto* icon = static_cast<SDL_Surface*>(assets->icon);
  if (icon == nullptr) {
    return;
  }
  Uint32 colorkey = SDL_MapRGB(icon->format, 0, 0, 0);
  SDL_SetColorKey(icon, SDL_SRCCOLORKEY, colorkey);

  /* No need to pass a mask given the above. */
  SDL_WM_SetIcon(icon, nullptr);
}

auto InitSDL() -> int {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
    fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
    return 1;
  }

  // SDL ref: Icon should be set *before* the first call to SDL_SetVideoMode.
  return 0;
}

void FrameRefreshStatus(int drawflags) {
  if ((drawflags & DRAW_LEDS) != 0) {
    size_t size = sizeof(g_lastDiskStatus);
    if (Peripheral_Query(disk_default_slot, disk_cmd_get_status,
                         &g_lastDiskStatus, &size) == PERIPHERAL_OK) {
      if (g_lastDiskStatus.drive0_last_error != disk_err_none &&
          g_lastDiskStatus.drive0_last_error != g_drive0_last_reported_error) {
        fprintf(stderr, "Disk 1 Error: %s\n",
                DiskUI_GetErrorMessage(g_lastDiskStatus.drive0_last_error));
        g_drive0_last_reported_error = g_lastDiskStatus.drive0_last_error;
      } else if (g_lastDiskStatus.drive0_last_error == disk_err_none) {
        g_drive0_last_reported_error = disk_err_none;
      }

      if (g_lastDiskStatus.drive1_last_error != disk_err_none &&
          g_lastDiskStatus.drive1_last_error != g_drive1_last_reported_error) {
        fprintf(stderr, "Disk 2 Error: %s\n",
                DiskUI_GetErrorMessage(g_lastDiskStatus.drive1_last_error));
        g_drive1_last_reported_error = g_lastDiskStatus.drive1_last_error;
      } else if (g_lastDiskStatus.drive1_last_error == disk_err_none) {
        g_drive1_last_reported_error = disk_err_none;
      }

      std::array<char, 512> s_title = {};
      if (g_lastDiskStatus.drive0_loaded != 0) {
        snprintf(s_title.data(), s_title.size(), "%s - %s", g_pAppTitle,
                 g_lastDiskStatus.drive0_name);
      } else {
        snprintf(s_title.data(), s_title.size(), "%s", g_pAppTitle);
      }
      Linapple_UpdateTitle(s_title.data());
    }
  }
  DrawStatusArea(drawflags);
}
