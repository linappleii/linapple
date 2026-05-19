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

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/Common.h"
#include "core/Peripheral.h"
#include "frontends/sdl3/Frame.h"
#include "frontends/sdl3/SDL_Video.h"
auto SDLSurfaceToVideoSurface(SDL_Surface* s) -> VideoSurface;
#include "Debugger/Debug.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SaveState.h"
#include "apple2/SoundCore.h"
#include "apple2/Video.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "apple2/peripherals/printer/Printer.h"
#include "apple2/peripherals/super_serial_card/SuperSerial.h"
#include "apple2/stretch.h"
#include "core/Common_Globals.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "core/asset.h"
#include "frontends/sdl3/DiskChoose.h"
#include "frontends/sdl3/DiskUI.h"

#define ENABLE_MENU 0

SDL_Surface* apple_icon;
SDL_Surface* screen;
SDL_Window* g_window = nullptr;
SDL_Renderer* g_renderer = nullptr;
SDL_Texture* g_texture = nullptr;
SDL_Rect origRect;
SDL_Rect newRect;

#define VIEWPORTCX 560
#if ENABLE_MENU
#define VIEWPORTCY 400
#else
#define VIEWPORTCY 384
#endif
#define BUTTONX (VIEWPORTCX + (VIEWPORTX << 1))
enum { BUTTONY = 0, BUTTONCX = 45, BUTTONCY = 45 };
#define FSVIEWPORTX (640 - BUTTONCX - VIEWPORTX - VIEWPORTCX)
#define FSVIEWPORTY ((480 - VIEWPORTCY) >> 1)
#define FSBUTTONX (640 - BUTTONCX)
#define FSBUTTONY (((480 - VIEWPORTCY) >> 1) - 1)
enum { BUTTONS = 8 };

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
  if (g_texture && screen) {
    SDL_UpdateTexture(g_texture, nullptr, screen->pixels, screen->pitch);
    SDL_RenderTexture(g_renderer, g_texture, nullptr, nullptr);
    SDL_RenderPresent(g_renderer);
  }
}

void DrawFrameWindow() {
  if (!g_bFrameReady) return;

  g_video_draw_mutex.lock();
  if (g_texture && screen) {
    uint32_t* output = VideoGetOutputBuffer();
    SDL_Rect r = {0, 0, 560, 384};

    // Fill screen from RGB32 output buffer
    if (g_state.mode != MODE_DEBUG) {
      VideoSurface vs_screen = SDLSurfaceToVideoSurface(screen);
      VideoSurface vs_output{};
      vs_output.pixels = reinterpret_cast<uint8_t*>(output);
      vs_output.w = 560;
      vs_output.h = 384;
      vs_output.pitch = 560 * 4;
      vs_output.bpp = 4;

      if (!g_WindowResized) {
        VideoSoftStretch(&vs_output, reinterpret_cast<VideoRect*>(&r),
                         &vs_screen, reinterpret_cast<VideoRect*>(&r));
      } else {
        VideoSoftStretch(&vs_output, reinterpret_cast<VideoRect*>(&origRect),
                         &vs_screen, reinterpret_cast<VideoRect*>(&newRect));
      }
    } else {
      // Debugger draws directly to g_hDebugScreen (INDEX8)
      // We need to stretch/convert it to the RGB32 screen surface
      extern VideoSurface* g_hDebugScreen;
      if (g_hDebugScreen) {
        VideoSurface vs_screen = SDLSurfaceToVideoSurface(screen);
        if (!g_WindowResized) {
          VideoSoftStretch(g_hDebugScreen, reinterpret_cast<VideoRect*>(&r),
                           &vs_screen, reinterpret_cast<VideoRect*>(&r));
        } else {
          VideoSoftStretch(g_hDebugScreen,
                           reinterpret_cast<VideoRect*>(&origRect), &vs_screen,
                           reinterpret_cast<VideoRect*>(&newRect));
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
    if (!fonts_initialization()) {
      fprintf(stderr, "Font file was not loaded.\n");
      return;
    }
  }

  VideoRect srect{};
  uint8_t mybluez = DARK_BLUE;

  if (drawflags & DRAW_BACKGROUND) {
    g_iStatusCycle = SHOW_CYCLES;
  }
  if (drawflags & DRAW_LEDS) {
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
#define LEDS 1
    int iDrive1Status = disk_status_off;
    int iDrive2Status = disk_status_off;
    int iHDDStatus = disk_status_off;

    if (g_lastDiskStatus.drive0_spinning) {
      iDrive1Status = g_lastDiskStatus.drive0_writing ? disk_status_write
                                                      : disk_status_read;
    } else if (g_lastDiskStatus.drive0_loaded &&
               g_lastDiskStatus.drive0_write_protected) {
      iDrive1Status = disk_status_prot;
    }

    if (g_lastDiskStatus.drive1_spinning) {
      iDrive2Status = g_lastDiskStatus.drive1_writing ? disk_status_write
                                                      : disk_status_read;
    } else if (g_lastDiskStatus.drive1_loaded &&
               g_lastDiskStatus.drive1_write_protected) {
      iDrive2Status = disk_status_prot;
    }

    HarddiskStatus_t hstatus{};
    size_t hsize = sizeof(hstatus);
    if (Peripheral_Query(7, HARDDISK_CMD_GET_STATUS, &hstatus, &hsize) ==
        PERIPHERAL_OK) {
      iHDDStatus = hstatus.activity_status;
    }

    leds[0] = static_cast<char>(LEDS + iDrive1Status);
    font_print(8, 23, leds.data(), g_hStatusSurface, 4.0f, 2.7f);

    leds[0] = static_cast<char>(LEDS + iDrive2Status);
    font_print(40, 23, leds.data(), g_hStatusSurface, 4.0f, 2.7f);

    leds[0] = static_cast<char>(LEDS + iHDDStatus);
    font_print(71, 23, leds.data(), g_hStatusSurface, 4.0f, 2.7f);

    if (iDrive1Status | iDrive2Status | iHDDStatus) {
      g_iStatusCycle = SHOW_CYCLES;
    }
  }
}

void FrameShowHelpScreen(int sx, int sy) {
  (void)sy;
  const int MAX_LINES = 25;
  const char* HelpStrings[MAX_LINES] = {
      "Welcome to LinApple - Apple][ emulator for Linux!",
      "Conf file is linapple.conf in current directory by default",
      "Hugest archive of Apple][ stuff you can find at ftp.apple.asimov.net",
      "       F1 - Show help screen",
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
      "       F8 - Take screenshot",
      " Shift+F8 - Save runtime changes to configuration file",
      "       F9 - Cycle through various video modes",
      " Shift+F9 - Budget video, for smoother music/audio",
      "  F10/F11 - Load/save snapshot file",
      "",
      "       Pause - Pause/resume emulator",
      " Scroll Lock - Toggle full speed",
      "  Numpad +/-/* - Increase/Decrease/Normal speed"};

  VideoSurface* tempSurface = nullptr;

  if (font_sfc == nullptr) {
    if (!fonts_initialization()) {
      fprintf(stderr, "Font file was not loaded.\n");
      return;
    }
  }
  if (!g_WindowResized) {
    if (g_state.mode == MODE_LOGO) {
      tempSurface = g_hLogoBitmap;
    } else {
      tempSurface = g_hDeviceBitmap;
    }
  } else {
    tempSurface = g_origscreen;
  }

  if (tempSurface == nullptr) {
    // Wrap screen as fallback
    static VideoSurface vs_screen;
    vs_screen = SDLSurfaceToVideoSurface(screen);
    tempSurface = &vs_screen;
  }

  VideoSurface vs_actual_screen = SDLSurfaceToVideoSurface(screen);

  // Capture original screen
  VideoSoftStretch(tempSurface, nullptr, &vs_actual_screen, nullptr);

  // Blur the background by downscaling and upscaling
  // We use a small temporary surface (1/16 size) to create a pixelated blur
  // effect
  SDL_Surface* blur_temp = SDL_CreateSurface(screen->w / 16, screen->h / 16,
                                             SDL_PIXELFORMAT_ARGB8888);
  if (blur_temp) {
    VideoSurface vs_blur = SDLSurfaceToVideoSurface(blur_temp);
    VideoSoftStretch(&vs_actual_screen, nullptr, &vs_blur,
                     nullptr);  // Downscale
    VideoSoftStretch(&vs_blur, nullptr, &vs_actual_screen,
                     nullptr);  // Upscale back
    SDL_DestroySurface(blur_temp);
  }

  // Dim the background using SDL blending for better text readability
  SDL_Surface* dim_surface =
      SDL_CreateSurface(screen->w, screen->h, SDL_PIXELFORMAT_ARGB8888);
  if (dim_surface) {
    Uint32 dim_color =
        SDL_MapRGBA(SDL_GetPixelFormatDetails(dim_surface->format),
                    SDL_GetSurfacePalette(dim_surface), 0, 0, 0, 200);
    SDL_FillSurfaceRect(dim_surface, nullptr, dim_color);
    SDL_SetSurfaceBlendMode(dim_surface, SDL_BLENDMODE_BLEND);
    SDL_BlitSurface(dim_surface, nullptr, screen, nullptr);
    SDL_DestroySurface(dim_surface);
  }

  const float facx_f = static_cast<float>(g_state.ScreenWidth) /
                       static_cast<float>(SCREEN_WIDTH);
  const float facy_f = static_cast<float>(g_state.ScreenHeight) /
                       static_cast<float>(SCREEN_HEIGHT);
  const double facy = static_cast<double>(facy_f);

  font_print_centered(sx / 2, static_cast<int>(5.0 * facy),
                      const_cast<char*>(HelpStrings[0]), &vs_actual_screen,
                      1.5f * facx_f, 1.3f * facy_f);
  font_print_centered(sx / 2, static_cast<int>(20.0 * facy),
                      const_cast<char*>(HelpStrings[1]), &vs_actual_screen,
                      1.3f * facx_f, 1.2f * facy_f);
  font_print_centered(sx / 2, static_cast<int>(30.0 * facy),
                      const_cast<char*>(HelpStrings[2]), &vs_actual_screen,
                      1.2f * facx_f, 1.0f * facy_f);

  int Help_TopX = static_cast<int>(45.0 * facy);
  for (int i = 3; i < MAX_LINES; i++) {
    if (HelpStrings[i]) {
      font_print(4,
                 static_cast<int>(static_cast<double>(Help_TopX) +
                                  static_cast<double>(i - 3) * 15.0 * facy),
                 const_cast<char*>(HelpStrings[i]), &vs_actual_screen,
                 1.5f * facx_f, 1.5f * facy_f);
    }
  }

  rectangle(&vs_actual_screen, 0, Help_TopX - 5,
            static_cast<int>(g_state.ScreenWidth - 1),
            static_cast<int>(335.0 * facy), 0xFFFFFF);
  rectangle(&vs_actual_screen, 1, Help_TopX - 4,
            static_cast<int>(g_state.ScreenWidth),
            static_cast<int>(335.0 * facy), 0xFFFFFF);
  rectangle(&vs_actual_screen, 1, 1, static_cast<int>(g_state.ScreenWidth - 2),
            (Help_TopX - 8), 0xFFFF00);

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

  VideoRect logo{}, scrr{};
  logo.x = logo.y = 0;
  logo.w = vs_icon.w;
  logo.h = vs_icon.h;
  scrr.x = static_cast<int16_t>(460.0f * facx_f);
  scrr.y = static_cast<int16_t>(270.0f * facy_f);
  scrr.w = static_cast<int16_t>(100.0f * facy_f);
  scrr.h = static_cast<int16_t>(100.0f * facy_f);
  VideoSoftStretchOr(&vs_icon, &logo, &vs_actual_screen, &scrr);

  FrameRefresh();
  SDL_Delay(1000);

  SDL_Event event;

  event.type = SDL_EVENT_QUIT;
  while (event.type != SDL_EVENT_KEY_DOWN) {
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
  if (mod & SDL_KMOD_SHIFT) {
    Snapshot_SaveState();
  } else {
    Snapshot_LoadState();
  }
}

auto IsModifierKey(SDL_Keycode sym) -> bool {
  switch (sym) {
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
    case SDLK_LCTRL:
    case SDLK_RCTRL:
    case SDLK_LALT:
    case SDLK_RALT:
    case SDLK_LGUI:
    case SDLK_RGUI:
    case SDLK_CAPSLOCK:
      return true;
    default:
      return false;
  }
}

void Frame_OnResize(int width, int height) {
  g_video_draw_mutex.lock();
  g_state.ScreenWidth = width;
  g_state.ScreenHeight = (height / 96) * 96;
  if (g_state.ScreenHeight < 192) {
    g_state.ScreenHeight = 192;
  }

  if (screen) SDL_DestroySurface(screen);
  screen = SDL_CreateSurface(g_state.ScreenWidth, g_state.ScreenHeight,
                             SDL_PIXELFORMAT_XRGB8888);

  if (g_texture) SDL_DestroyTexture(g_texture);
  g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_XRGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                g_state.ScreenWidth, g_state.ScreenHeight);

  if (screen == nullptr || g_texture == nullptr) {
    g_video_draw_mutex.unlock();
    SDL_Quit();
    return;
  } else {
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
  }
  g_video_draw_mutex.unlock();
}

void Frame_OnFocus(bool gained) {
  g_bAppActive = gained;
  if (g_bAppActive) {
    // Re-sync Caps Lock state upon regaining focus
    SDL_Keymod mod = SDL_GetModState();
    uint8_t caps = (mod & SDL_KMOD_CAPS) ? 1 : 0;
    Peripheral_Command(0, KEYB_CMD_SET_CAPS, &caps, 1);
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

  fileIndex = backdx;
  fullPath = g_state.sSaveStateDir.data();

  while (isDirectory) {
    if (!ChooseAnImage(g_state.ScreenWidth, g_state.ScreenHeight, fullPath,
                       saveit, filename, isDirectory, fileIndex)) {
      DrawFrameWindow();
      return false;
    }
    if (isDirectory) {
      if (filename == "..") {
        const auto last_sep_pos = fullPath.find_last_of(FILE_SEPARATOR);
        if (last_sep_pos == std::string::npos) {
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
  Util_SafeStrCpy(g_state.sSaveStateDir.data(), fullPath.c_str(),
                  g_state.sSaveStateDir.size());
  Configuration::Instance().SetString("Preferences", "Save State Directory",
                                      g_state.sSaveStateDir.data());
  Configuration::Instance().Save();

  backdx = fileIndex;

  fullPath += "/" + filename;

  Snapshot_SetFilename(fullPath.c_str());
  Configuration::Instance().SetString(
      "Preferences", REGVALUE_SAVESTATE_FILENAME, fullPath.c_str());
  Configuration::Instance().Save();
  DrawFrameWindow();
  return true;
}

void FrameSaveBMP() {
  // Save current screen as a .bmp file in current directory
  struct stat bufp{};
  static int i = 1;
  std::array<char, 20> bmpName;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
  snprintf(bmpName.data(), bmpName.size(), "linapple%7d.bmp", i);
  while (!stat(bmpName.data(), &bufp)) {
    i++;
    snprintf(bmpName.data(), bmpName.size(), "linapple%7d.bmp", i);
  }
#pragma GCC diagnostic pop

  SDL_SaveBMP(screen, bmpName.data());
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
      FrameShowHelpScreen(screen->w, screen->h);
      break;

    case BTN_RUN:
      if ((mod & (SDL_KMOD_LCTRL)) == (SDL_KMOD_LCTRL) ||
          (mod & (SDL_KMOD_RCTRL)) == (SDL_KMOD_RCTRL)) {
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
      } else if (mod & SDL_KMOD_SHIFT) {
        g_state.restart = true;
        qe.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&qe);
      }
      break;

    case BTN_DRIVE1:
    case BTN_DRIVE2:
      Peripheral_Command(0, JOY_CMD_RESET, nullptr, 0);
      if (mod & SDL_KMOD_CTRL) {
        if (mod & SDL_KMOD_SHIFT) {
          printf("HDD  Eject Drive #%d\n", (button - BTN_DRIVE1) + 1);
          HarddiskEjectCmd_t ecmd = {static_cast<uint8_t>(button - BTN_DRIVE1)};
          Peripheral_Command(7, HARDDISK_CMD_EJECT, &ecmd, sizeof(ecmd));
        } else {
          printf("Disk Eject Drive #%d\n", (button - BTN_DRIVE1) + 1);
          DiskEjectCmd_t ecmd{};
          ecmd.drive = static_cast<uint8_t>(button - BTN_DRIVE1);
          Peripheral_Command(disk_default_slot, disk_cmd_eject, &ecmd,
                             sizeof(ecmd));
        }
        break;
      }

      if (mod & SDL_KMOD_SHIFT) {
        if (mod & SDL_KMOD_ALT) {
          HarddiskUI_FTPSelect(button - BTN_DRIVE1);
        } else {
          HarddiskUI_Select(button - BTN_DRIVE1);
        }
      } else {
        extern void DiskSelect(int drive);
        extern void Disk_FTP_SelectImage(int drive);
        if (mod & SDL_KMOD_ALT) {
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
      if (mod & SDL_KMOD_SHIFT) {
        // only IIe and enhanced have a keyboard rocker switch (and only non-US
        // keyboards)
        if ((g_Language != A2LANG_US) &&
            ((g_Apple2Type == A2TYPE_APPLE2E) ||
             (g_Apple2Type == A2TYPE_APPLE2EENHANCED))) {
          uint8_t cur_rocker = 0;
          size_t rocker_sz = sizeof(cur_rocker);
          Peripheral_Query(0, KEYB_QUERY_ROCKER, &cur_rocker, &rocker_sz);
          uint8_t new_rocker = cur_rocker ? 0 : 1;
          Peripheral_Command(0, KEYB_CMD_SET_ROCKER, &new_rocker, 1);
          printf(
              "Toggling keyboard rocker switch. Selected character set: "
              "%s...\n",
              new_rocker ? "local" : "standard/US");
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
      if (mod & SDL_KMOD_SHIFT) {
        Configuration::Instance().SetInt("Configuration", "Video Emulation",
                                         g_videotype);
        Configuration::Instance().SetInt("Configuration", "Emulation Speed",
                                         g_state.dwSpeed);
        Configuration::Instance().SetInt("Configuration", "Fullscreen",
                                         g_state.fullscreen ? 1 : 0);
        Configuration::Instance().Save();

      } else {
        FrameSaveBMP();
      }
      break;

    case BTN_CYCLE:
      if (mod & SDL_KMOD_SHIFT) {
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
      qe.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&qe);
      break;
    case BTN_SAVEST:
      if (mod & SDL_KMOD_ALT) {
        Snapshot_SaveState();
      } else if (PSP_SaveStateSelectImage(true)) {
        Snapshot_SaveState();
      }
      break;
    case BTN_LOADST:
      if (mod & SDL_KMOD_CTRL) {
        if (!IS_APPLE2()) {
          MemResetPaging();
        }

        Peripheral_Manager_Reset();
        if (!IS_APPLE2()) {
          VideoResetState();
        }
        CpuReset();
      } else if (mod & SDL_KMOD_ALT) {
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
  if (!bIamFullScreened) {
    bIamFullScreened = true;
    SDL_SetWindowFullscreen(g_window, true);
    if (g_state.mode != MODE_DEBUG) {
      SDL_HideCursor();
    }
  }
}

void SetNormalMode() {
  if (bIamFullScreened) {
    bIamFullScreened = false;
    SDL_SetWindowFullscreen(g_window, false);
    if (!usingcursor) {
      SDL_ShowCursor();
    }
  } else if (g_state.mode == MODE_DEBUG) {
    SDL_ShowCursor();
    SDL_SetWindowMouseGrab(g_window, false);
  }
}

void SetUsingCursor(bool newvalue) {
  usingcursor = newvalue;
  if (usingcursor) {
    SDL_HideCursor();
    SDL_SetWindowMouseGrab(g_window, true);
  } else {
    if ((!bIamFullScreened) || (g_state.mode == MODE_DEBUG)) {
      SDL_ShowCursor();
    }
    SDL_SetWindowMouseGrab(g_window, false);
  }
}

extern void SDL_Asset_LoadIcon();
extern void SDL_Asset_FreeIcon();

auto FrameCreateWindow() -> int {
  SDL_Asset_LoadIcon();
  bIamFullScreened = false;

  Uint32 flags = 0;
  if (g_state.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;

  g_window = SDL_CreateWindow(g_pAppTitle, g_state.ScreenWidth,
                              g_state.ScreenHeight, flags);
  if (!g_window) {
    fprintf(stderr, "Could not create SDL window: %s\n", SDL_GetError());
    return 1;
  }

  g_renderer = SDL_CreateRenderer(g_window, nullptr);
  if (!g_renderer) {
    fprintf(stderr, "Could not create SDL renderer: %s\n", SDL_GetError());
    return 1;
  }

  screen = SDL_CreateSurface(g_state.ScreenWidth, g_state.ScreenHeight,
                             SDL_PIXELFORMAT_XRGB8888);
  if (screen == nullptr) {
    fprintf(stderr, "Could not create SDL surface: %s\n", SDL_GetError());
    return 1;
  }

  g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_XRGB8888,
                                SDL_TEXTUREACCESS_STREAMING, 560, 384);

  SDL_SetRenderLogicalPresentation(g_renderer, 560, 384,
                                   SDL_LOGICAL_PRESENTATION_LETTERBOX);
  SDL_ShowWindow(g_window);
  SetIcon();

  g_WindowResized = (g_state.ScreenWidth != SCREEN_WIDTH) |
                    (g_state.ScreenHeight != SCREEN_HEIGHT);
  printf("Screen size is %dx%d\n", g_state.ScreenWidth, g_state.ScreenHeight);
  if (g_WindowResized) {
    origRect.x = origRect.y = newRect.x = newRect.y = 0;
    origRect.w = SCREEN_WIDTH;
    origRect.h = SCREEN_HEIGHT;
    newRect.w = g_state.ScreenWidth;
    newRect.h = g_state.ScreenHeight;
  }
  return 0;
}

void SetIcon() {
  /* Black is the transparency colour.
     Part of the logo seems to use it !? */
  Uint32 colorkey = SDL_MapRGB(
      SDL_GetPixelFormatDetails(
          (static_cast<SDL_Surface*>(assets->icon))->format),
      SDL_GetSurfacePalette(static_cast<SDL_Surface*>(assets->icon)), 0, 0, 0);
  SDL_SetSurfaceColorKey(static_cast<SDL_Surface*>(assets->icon), true,
                         colorkey);

  /* No need to pass a mask given the above. */
  SDL_SetWindowIcon(g_window, static_cast<SDL_Surface*>(assets->icon));
}

auto InitSDL() -> int {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK)) {
    fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
    return 1;
  }

  // SDL ref: Icon should be set *before* the first call to SDL_SetVideoMode.
  return 0;
}

void FrameRefreshStatus(int drawflags) {
  if (drawflags & DRAW_LEDS) {
    size_t size = sizeof(g_lastDiskStatus);
    if (Peripheral_Query(disk_default_slot, disk_cmd_get_status,
                         &g_lastDiskStatus, &size) == PERIPHERAL_OK) {
      if (g_lastDiskStatus.drive0_last_error != disk_err_none &&
          g_lastDiskStatus.drive0_last_error != g_drive0_last_reported_error) {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "Disk 1 Error",
            DiskUI_GetErrorMessage(g_lastDiskStatus.drive0_last_error),
            g_window);
        g_drive0_last_reported_error = g_lastDiskStatus.drive0_last_error;
      } else if (g_lastDiskStatus.drive0_last_error == disk_err_none) {
        g_drive0_last_reported_error = disk_err_none;
      }

      if (g_lastDiskStatus.drive1_last_error != disk_err_none &&
          g_lastDiskStatus.drive1_last_error != g_drive1_last_reported_error) {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "Disk 2 Error",
            DiskUI_GetErrorMessage(g_lastDiskStatus.drive1_last_error),
            g_window);
        g_drive1_last_reported_error = g_lastDiskStatus.drive1_last_error;
      } else if (g_lastDiskStatus.drive1_last_error == disk_err_none) {
        g_drive1_last_reported_error = disk_err_none;
      }

      char s_title[512];
      if (g_lastDiskStatus.drive0_loaded) {
        snprintf(s_title, sizeof(s_title), "%s - %s", g_pAppTitle,
                 g_lastDiskStatus.drive0_name);
      } else {
        snprintf(s_title, sizeof(s_title), "%s", g_pAppTitle);
      }
      Linapple_UpdateTitle(s_title);
    }
  }
  DrawStatusArea(drawflags);
}
