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

#include "core/Common.h"
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "frontends/sdl3/Frame.h"
#include "frontends/sdl3/SDL_Video.h"
VideoSurface SDLSurfaceToVideoSurface(SDL_Surface* s);
#include "frontends/sdl3/JoystickFrontend.h"
#include "apple2/Keyboard.h"
#include "core/asset.h"
#include "apple2/Structs.h"
#include "apple2/Video.h"
#include "apple2/stretch.h"
#include "core/Log.h"
#include "core/Common_Globals.h"
#include "Debugger/Debug.h"
#include "core/Registry.h"
#include "apple2/Disk.h"
#include "apple2/Harddisk.h"
#include "apple2/DiskFTP.h"
#include "apple2/SoundCore.h"
#include "apple2/MouseInterface.h"
#include "frontends/sdl3/Frontend.h"
#include "apple2/SerialComms.h"
#include "apple2/ParallelPrinter.h"
#include "apple2/Speaker.h"
#include "frontends/sdl3/DiskChoose.h"
#include "apple2/SaveState.h"
#include "apple2/Joystick.h"
#include "apple2/Mockingboard.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"

#define ENABLE_MENU 0

SDL_Surface *apple_icon;
SDL_Surface *screen;
SDL_Window *g_window = NULL;
SDL_Renderer *g_renderer = NULL;
SDL_Texture *g_texture = NULL;
SDL_Rect origRect;
SDL_Rect newRect;

#define  VIEWPORTCX  560
#if ENABLE_MENU
#define  VIEWPORTCY  400
#else
#define  VIEWPORTCY  384
#endif
#define  BUTTONX     (VIEWPORTCX+(VIEWPORTX<<1))
#define  BUTTONY     0
#define  BUTTONCX    45
#define  BUTTONCY    45
#define  FSVIEWPORTX (640-BUTTONCX-VIEWPORTX-VIEWPORTCX)
#define  FSVIEWPORTY ((480-VIEWPORTCY)>>1)
#define  FSBUTTONX   (640-BUTTONCX)
#define  FSBUTTONY   (((480-VIEWPORTCY)>>1)-1)
#define  BUTTONS     8

static bool g_bAppActive = false;

int buttondown = -1;

bool g_WindowResized;

bool usingcursor = false;

void DrawStatusArea(int drawflags);

void ProcessButtonClick(int button, int mod);

void ResetMachineState();

void SetFullScreenMode();

void SetNormalMode();

void SetUsingCursor(bool);

void SetIcon();

bool g_bScrollLock_FullSpeed = false;

void DrawAppleContent()
{
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

void DrawFrameWindow()
{
  if (!g_bFrameReady) return;

  g_video_draw_mutex.lock();
  if (g_texture && screen) {
      uint32_t* output = VideoGetOutputBuffer();
      SDL_Rect r = {0, 0, 560, 384};

      // Fill screen from RGB32 output buffer
      if (g_state.mode != MODE_DEBUG) {
          VideoSurface vs_screen = SDLSurfaceToVideoSurface(screen);
          VideoSurface vs_output;
          vs_output.pixels = (uint8_t*)output;
          vs_output.w = 560;
          vs_output.h = 384;
          vs_output.pitch = 560 * 4;
          vs_output.bpp = 4;

          if (!g_WindowResized) {
              VideoSoftStretch(&vs_output, (VideoRect*)&r, &vs_screen, (VideoRect*)&r);
          } else {
              VideoSoftStretch(&vs_output, (VideoRect*)&origRect, &vs_screen, (VideoRect*)&newRect);
          }
      }

      SDL_UpdateTexture(g_texture, NULL, screen->pixels, screen->pitch);
      SDL_RenderTexture(g_renderer, g_texture, NULL, NULL);
      SDL_RenderPresent(g_renderer);
      g_bFrameReady = false;
  }
  g_video_draw_mutex.unlock();
}

void DrawStatusArea(int drawflags)
{
  if (font_sfc == NULL) {
    if (!fonts_initialization()) {
      fprintf(stderr, "Font file was not loaded.\n");
      return;
    }
  }

  VideoRect srect;
  uint8_t mybluez = DARK_BLUE;

  if (drawflags & DRAW_BACKGROUND) {
    g_iStatusCycle = SHOW_CYCLES;
  }
  if (drawflags & DRAW_LEDS) {
    srect.x = 4;
    srect.y = 22;
    srect.w = STATUS_PANEL_W - 8;
    srect.h = STATUS_PANEL_H - 25;
    
    // Fill background of status surface
    for (int y = srect.y; y < srect.y + srect.h; ++y) {
        memset(g_hStatusSurface->pixels + y * g_hStatusSurface->pitch + srect.x, mybluez, srect.w);
    }

    char leds[2] = "\x64";
    #define LEDS  1
    int iDrive1Status = DISK_STATUS_OFF;
    int iDrive2Status = DISK_STATUS_OFF;
    int iHDDStatus = DISK_STATUS_OFF;

    DiskGetLightStatus(&iDrive1Status, &iDrive2Status);
    iHDDStatus = HD_GetStatus();

    leds[0] = LEDS + iDrive1Status;
    font_print(8, 23, leds, g_hStatusSurface, 4, 2.7);

    leds[0] = LEDS + iDrive2Status;
    font_print(40, 23, leds, g_hStatusSurface, 4, 2.7);

    leds[0] = LEDS + iHDDStatus;
    font_print(71, 23, leds, g_hStatusSurface, 4, 2.7);

    if (iDrive1Status | iDrive2Status | iHDDStatus) {
      g_iStatusCycle = SHOW_CYCLES;
    }
  }
}

void FrameShowHelpScreen(int sx, int sy)
{
  (void)sy;
  const int MAX_LINES = 25;
  const char *HelpStrings[MAX_LINES] = {"Welcome to LinApple - Apple][ emulator for Linux!",
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

  VideoSurface *tempSurface = NULL;

  if (font_sfc == NULL) {
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

  if (tempSurface == NULL) {
    // Wrap screen as fallback
    static VideoSurface vs_screen;
    vs_screen = SDLSurfaceToVideoSurface(screen);
    tempSurface = &vs_screen;
  }

  VideoSurface *my_screen_vs = VideoCreateSurface(tempSurface->w, tempSurface->h, tempSurface->bpp);
  VideoSurface vs_actual_screen = SDLSurfaceToVideoSurface(screen);

  surface_fader(my_screen_vs, 0.2F, 0.2F, 0.2F, -1, 0);
  VideoSoftStretch(tempSurface, NULL, my_screen_vs, NULL);
  VideoSoftStretch(my_screen_vs, NULL, &vs_actual_screen, NULL);

  double facx = double(g_state.ScreenWidth) / double(SCREEN_WIDTH);
  double facy = double(g_state.ScreenHeight) / double(SCREEN_HEIGHT);

  font_print_centered(sx / 2, int(5 * facy), (char *) HelpStrings[0], &vs_actual_screen, 1.5 * facx, 1.3 * facy);
  font_print_centered(sx / 2, int(20 * facy), (char *) HelpStrings[1], &vs_actual_screen, 1.3 * facx, 1.2 * facy);
  font_print_centered(sx / 2, int(30 * facy), (char *) HelpStrings[2], &vs_actual_screen, 1.2 * facx, 1.0 * facy);

  int Help_TopX = int(45 * facy);
  for (int i = 3; i < MAX_LINES; i++) {
    if (HelpStrings[i])
      font_print(4, Help_TopX + (i - 3) * 15 * facy, (char *) HelpStrings[i], &vs_actual_screen, 1.5 * facx,
               1.5 * facy);
  }

  rectangle(&vs_actual_screen, 0, Help_TopX - 5, g_state.ScreenWidth - 1, int(335 * facy), 0xFFFFFF);
  rectangle(&vs_actual_screen, 1, Help_TopX - 4, g_state.ScreenWidth, int(335 * facy), 0xFFFFFF);
  rectangle(&vs_actual_screen, 1, 1, g_state.ScreenWidth - 2, (Help_TopX - 8), 0xFFFF00);

  // Logo bit
  VideoSurface vs_icon;
  vs_icon.pixels = (uint8_t*)((SDL_Surface*)assets->icon)->pixels;
  vs_icon.w = ((SDL_Surface*)assets->icon)->w;
  vs_icon.h = ((SDL_Surface*)assets->icon)->h;
  vs_icon.pitch = ((SDL_Surface*)assets->icon)->pitch;
  vs_icon.bpp = 4; // Assuming RGB32

  VideoRect logo, scrr;
  logo.x = logo.y = 0;
  logo.w = vs_icon.w;
  logo.h = vs_icon.h;
  scrr.x = int(460 * facx);
  scrr.y = int(270 * facy);
  scrr.w = scrr.h = int(100 * facy);
  VideoSoftStretchOr(&vs_icon, &logo, &vs_actual_screen, &scrr);

  if (g_texture && screen) {
      SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
      SDL_UpdateTexture(g_texture, NULL, screen->pixels, screen->pitch);
      SDL_RenderClear(g_renderer);
      SDL_RenderTexture(g_renderer, g_texture, NULL, NULL);
      SDL_RenderPresent(g_renderer);
  }
  SDL_Delay(1000);

  VideoDestroySurface(my_screen_vs);

  SDL_Event event;

  event.type = SDL_EVENT_QUIT;
  while (event.type != SDL_EVENT_KEY_DOWN) {
    usleep(100);
    SDL_PollEvent(&event);
  }

  DrawFrameWindow();
}

void FrameQuickState(int num, int mod)
{
  // quick load or save state with number num, if Shift is pressed, state is being saved, otherwise - being loaded
  char fpath[MAX_PATH];
  snprintf(fpath, MAX_PATH, "%.*s/SaveState%d.aws", int(strlen(g_state.sSaveStateDir)), g_state.sSaveStateDir, num);
  Snapshot_SetFilename(fpath);
  if (mod & SDL_KMOD_SHIFT) {
    Snapshot_SaveState();
  } else {
    Snapshot_LoadState();
  }
}

bool IsModifierKey(SDL_Keycode sym) {
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

void Frame_OnResize(int w, int h) {
  g_video_draw_mutex.lock();
  g_state.ScreenWidth = w;
  g_state.ScreenHeight = (h / 96) * 96;
  if (g_state.ScreenHeight < 192) {
    g_state.ScreenHeight = 192;
  }

  if (screen) SDL_DestroySurface(screen);
  screen = SDL_CreateSurface(g_state.ScreenWidth, g_state.ScreenHeight, SDL_PIXELFORMAT_XRGB8888);

  if (g_texture) SDL_DestroyTexture(g_texture);
  g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, g_state.ScreenWidth, g_state.ScreenHeight);

  if (screen == NULL || g_texture == NULL) {
    g_video_draw_mutex.unlock();
    SDL_Quit();
    return;
  } else {
    g_WindowResized = (g_state.ScreenWidth != SCREEN_WIDTH) | (g_state.ScreenHeight != SCREEN_HEIGHT);
    if (g_WindowResized) {
      origRect.x = origRect.y = newRect.x = newRect.y = 0;
      origRect.w = SCREEN_WIDTH;
      origRect.h = SCREEN_HEIGHT;
      newRect.w = g_state.ScreenWidth;
      newRect.h = g_state.ScreenHeight;
      if ((g_state.mode != MODE_LOGO) && (g_state.mode != MODE_DEBUG)) {
        VideoRedrawScreen();
      }
    }
  }
  g_video_draw_mutex.unlock();
}

void Frame_OnFocus(bool gained) {
    g_bAppActive = gained;
}

void Frame_OnExpose() {
    if ((g_state.mode != MODE_LOGO) && (g_state.mode != MODE_DEBUG)) {
        VideoRedrawScreen();
    }
}

bool PSP_SaveStateSelectImage(bool saveit)
{
  static size_t fileIndex = 0;
  static int backdx = 0;
  static int dirdx = 0;

  std::string filename;
  std::string fullPath;
  bool isDirectory = true;

  fileIndex = backdx;
  fullPath = g_state.sSaveStateDir;

  while (isDirectory) {
    if (!ChooseAnImage(g_state.ScreenWidth, g_state.ScreenHeight, fullPath, saveit,
                       filename, isDirectory, fileIndex)) {
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
  strcpy(g_state.sSaveStateDir, fullPath.c_str());
  Configuration::Instance().SetString("Preferences", REGVALUE_PREF_SAVESTATE_DIR, g_state.sSaveStateDir);
  Configuration::Instance().Save();

  backdx = fileIndex;

  fullPath += "/" + filename;

  Snapshot_SetFilename(fullPath.c_str());
  Configuration::Instance().SetString("Preferences", REGVALUE_SAVESTATE_FILENAME, fullPath.c_str());
  Configuration::Instance().Save();
  DrawFrameWindow();
  return true;
}

void FrameSaveBMP(void) {
  // Save current screen as a .bmp file in current directory
  struct stat bufp;
  static int i = 1;
  char bmpName[20];

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
  snprintf(bmpName, 20, "linapple%7d.bmp", i);
  while (!stat(bmpName, &bufp)) {
    i++;
    snprintf(bmpName, 20, "linapple%7d.bmp", i);
  }
#pragma GCC diagnostic pop

  SDL_SaveBMP(screen, bmpName);
  printf("File %s saved!\n", bmpName);
  i++;
}

void ProcessButtonClick(int button, int mod)
{
  SDL_Event qe;

  SoundCore_SetFade(FADE_OUT);

  switch (button) {
    case BTN_HELP:
      FrameShowHelpScreen(screen->w, screen->h);
      break;

    case BTN_RUN:
      if ((mod & (SDL_KMOD_LCTRL)) == (SDL_KMOD_LCTRL) || (mod & (SDL_KMOD_RCTRL)) == (SDL_KMOD_RCTRL)) {
        if (g_state.mode == MODE_LOGO) {
          DiskBoot();
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
      JoyReset();
      if (mod & SDL_KMOD_CTRL) {
        if (mod & SDL_KMOD_SHIFT) {
          printf("HDD  Eject Drive #%d\n", (button - BTN_DRIVE1) + 1);
          HD_Eject(button - BTN_DRIVE1);
        } else {
          printf("Disk Eject Drive #%d\n", (button - BTN_DRIVE1) + 1);
          DiskEject(button - BTN_DRIVE1);
        }
        break;
      }

      if (mod & SDL_KMOD_SHIFT) {
        if (mod & SDL_KMOD_ALT) {
          HD_FTP_Select(button - BTN_DRIVE1);
        } else {
          HD_Select(button - BTN_DRIVE1);
        }
      } else {
        if (mod & SDL_KMOD_ALT) {
          Disk_FTP_SelectImage(button - BTN_DRIVE1);
        } else {
          DiskSelect(button - BTN_DRIVE1);
        }
      }
      break;

    case BTN_DRIVESWAP:
      DiskDriveSwap();
      break;

    case BTN_FULLSCR:
      if (mod & SDL_KMOD_SHIFT) {
         // only IIe and enhanced have a keyboard rocker switch (and only non-US keyboards)
         if ((g_KeyboardLanguage != English_US)&&
             ((g_Apple2Type == A2TYPE_APPLE2E)||(g_Apple2Type == A2TYPE_APPLE2EENHANCED)))
         {
           g_KeyboardRockerSwitch = !g_KeyboardRockerSwitch;
           printf("Toggling keyboard rocker switch. Selected character set: %s...\n", (g_KeyboardRockerSwitch) ? "local" : "standard/US");
         }
      }
      else
      {
        if (g_state.fullscreen) {
          g_state.fullscreen = false;
          SetNormalMode();
        } else {
          g_state.fullscreen = true;
          SetFullScreenMode();
        }
        JoyReset();
      }
      break;

    case BTN_DEBUG:
      if (g_state.mode != MODE_DEBUG)
      {
        DebugBegin();
        SetUsingCursor(false);
      }
      else
      if (g_state.mode == MODE_DEBUG)
      {
        g_state.mode = MODE_RUNNING;
      }
      break;

    case BTN_SETUP:
      if (mod & SDL_KMOD_SHIFT) {
        Configuration::Instance().SetInt("Configuration", "Video Emulation", g_videotype);
        Configuration::Instance().SetInt("Configuration", "Emulation Speed", g_state.dwSpeed);
        Configuration::Instance().SetInt("Configuration", "Fullscreen", g_state.fullscreen ? 1 : 0);
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
        if (g_state.mode != MODE_LOGO)
        {
          if (g_state.mode == MODE_DEBUG)
          {
            unsigned int debugVideoMode;
            if (DebugGetVideoMode(&debugVideoMode)) {
              VideoRefreshScreen();
            }
          }
          else
          {
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

        DiskReset();
        KeybReset();
        if (!IS_APPLE2()) {
          VideoResetState();
        }
        MB_Reset();
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
  DiskReset();
  g_bFullSpeed = false;  // Might've hit reset in middle of InternalCpuExecute() - so beep may get (partially) muted

  MemReset();
  DiskBoot();
  VideoResetState();
  SSC_Reset(&sg_SSC);
  PrintReset();
  JoyReset();
  MB_Reset();
  SpkrReset();
}

static bool bIamFullScreened;

void SetFullScreenMode() {
  if (!bIamFullScreened) {
    bIamFullScreened = true;
    SDL_SetWindowFullscreen(g_window, true);
    if (g_state.mode != MODE_DEBUG)
      SDL_HideCursor();
  }
}

void SetNormalMode()
{
  if (bIamFullScreened) {
    bIamFullScreened = false;
    SDL_SetWindowFullscreen(g_window, false);
    if (!usingcursor) {
      SDL_ShowCursor();
    }
  }
  else
  if (g_state.mode == MODE_DEBUG)
  {
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
    if ((!bIamFullScreened)||(g_state.mode == MODE_DEBUG)) {
      SDL_ShowCursor();
    }
    SDL_SetWindowMouseGrab(g_window, false);
  }
}

extern void SDL_Asset_LoadIcon();
extern void SDL_Asset_FreeIcon();

int FrameCreateWindow()
{
  SDL_Asset_LoadIcon();
  bIamFullScreened = false;

  Uint32 flags = 0;
  if (g_state.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;

  g_window = SDL_CreateWindow(g_pAppTitle, g_state.ScreenWidth, g_state.ScreenHeight, flags);
  if (!g_window) {
    fprintf(stderr, "Could not create SDL window: %s\n", SDL_GetError());
    return 1;
  }

  g_renderer = SDL_CreateRenderer(g_window, NULL);
  if (!g_renderer) {
    fprintf(stderr, "Could not create SDL renderer: %s\n", SDL_GetError());
    return 1;
  }

  screen = SDL_CreateSurface(g_state.ScreenWidth, g_state.ScreenHeight, SDL_PIXELFORMAT_XRGB8888);
  if (screen == NULL) {
    fprintf(stderr, "Could not create SDL surface: %s\n", SDL_GetError());
    return 1;
  }

  g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 560, 384);

  SDL_SetRenderLogicalPresentation(g_renderer, 560, 384, SDL_LOGICAL_PRESENTATION_LETTERBOX);
  SDL_ShowWindow(g_window);
  SetIcon();

  g_WindowResized = (g_state.ScreenWidth != SCREEN_WIDTH) | (g_state.ScreenHeight != SCREEN_HEIGHT);
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

void SetIcon()
{
  /* Black is the transparency colour.
     Part of the logo seems to use it !? */
  Uint32 colorkey = SDL_MapRGB(SDL_GetPixelFormatDetails(((SDL_Surface*)assets->icon)->format), SDL_GetSurfacePalette((SDL_Surface*)assets->icon), 0, 0, 0);
  SDL_SetSurfaceColorKey((SDL_Surface*)assets->icon, true, colorkey);

  /* No need to pass a mask given the above. */
  SDL_SetWindowIcon(g_window, (SDL_Surface*)assets->icon);
}

int InitSDL()
{
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK)) {
    fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
    return 1;
  }

  // SDL ref: Icon should be set *before* the first call to SDL_SetVideoMode.
  return 0;
}

void FrameRefreshStatus(int drawflags)
{
  DrawStatusArea(drawflags);
}
