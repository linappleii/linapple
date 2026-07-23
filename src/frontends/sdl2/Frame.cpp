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

#include "frontends/sdl2/Frame.h"

#include <SDL2/SDL.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "Debugger/Debug.h"
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "apple2/peripherals/printer/Printer.h"
#include "apple2/peripherals/super_serial_card/SuperSerial.h"
#include "core/Asset.h"
#include "core/AudioMixer.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "frontends/common/SaveStateManager.h"
#include "frontends/common/VideoStretch.h"
#include "frontends/sdl2/DiskChoose.h"
#include "frontends/sdl2/DiskUI.h"
#include "frontends/sdl2/SDL_Video.h"

#define ENABLE_MENU 0

SDL_Surface* apple_icon;
SDL_Surface* screen;
SDL_Window* g_window = nullptr;
SDL_Renderer* g_renderer = nullptr;
SDL_Texture* g_texture = nullptr;
SDL_Rect orig_rect;
SDL_Rect new_rect;

enum { BUTTONY = 0, BUTTONCX = 45, BUTTONCY = 45 };

static bool g_bAppActive = false;

static DiskStatus_t g_lastDiskStatus{};
static int g_drive0_last_reported_error = disk_err_none;
static int g_drive1_last_reported_error = disk_err_none;

int buttondown = -1;

bool g_window_resized;

bool usingcursor = false;

void draw_status_area(int drawflags);

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): button and mod are
// semantically distinct
void process_button_click(int button, int mod);

void ResetMachineState();

void SetFullScreenMode();

void SetNormalMode();

void set_using_cursor(bool);

void SetIcon();

bool g_scroll_lock_full_speed = false;

void DrawAppleContent() {
  g_video_draw_mutex.lock();
  VideoRealizePalette();

  draw_status_area(DRAW_BACKGROUND | DRAW_LEDS);

  if (g_state.mode == MODE_LOGO) {
    VideoDisplayLogo();
    g_bFrameReady = true;
  } else if (g_state.mode == MODE_DEBUG) {
    debug_display(true);
    g_bFrameReady = true;
  } else {
    VideoRedrawScreen();
  }
  g_video_draw_mutex.unlock();
}

void frame_refresh() {
  if (g_texture != nullptr && screen != nullptr) {
    SDL_UpdateTexture(g_texture, nullptr, screen->pixels, screen->pitch);
    SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);
    SDL_RenderPresent(g_renderer);
  }
}

void DrawFrameWindow() {
  if (g_bFrameReady == false) return;

  g_video_draw_mutex.lock();
  if (g_texture != nullptr && screen != nullptr) {
    uint32_t* output = VideoGetOutputBuffer();
    SDL_Rect r = {0, 0, 560, 384};

    // Fill screen from RGB32 output buffer
    if (g_state.mode != MODE_DEBUG) {
      VideoSurface vs_screen = sdl_surface_to_video_surface(screen);
      VideoSurface vs_output{};
      vs_output.pixels = reinterpret_cast<uint8_t*>(output);
      vs_output.w = 560;
      vs_output.h = 384;
      vs_output.pitch = 560 * 4;
      vs_output.bpp = 4;

      if (g_window_resized == false) {
        VideoSoftStretch(&vs_output, reinterpret_cast<VideoRect*>(&r),
                         &vs_screen, reinterpret_cast<VideoRect*>(&r));
      } else {
        VideoSoftStretch(&vs_output, reinterpret_cast<VideoRect*>(&orig_rect),
                         &vs_screen, reinterpret_cast<VideoRect*>(&new_rect));
      }
    } else {
      // Debugger draws directly to g_debug_screen (INDEX8)
      // We need to stretch/convert it to the RGB32 screen surface
      extern VideoSurface* g_debug_screen;
      if (g_debug_screen != nullptr) {
        VideoSurface vs_screen = sdl_surface_to_video_surface(screen);
        if (g_window_resized == false) {
          VideoSoftStretch(g_debug_screen, reinterpret_cast<VideoRect*>(&r),
                           &vs_screen, reinterpret_cast<VideoRect*>(&r));
        } else {
          VideoSoftStretch(g_debug_screen,
                           reinterpret_cast<VideoRect*>(&orig_rect), &vs_screen,
                           reinterpret_cast<VideoRect*>(&new_rect));
        }
      }
    }

    frame_refresh();
    g_bFrameReady = false;
  }
  g_video_draw_mutex.unlock();
}

void draw_status_area(int drawflags) {
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
    constexpr int led_char_base = 1;
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
    if (peripheral_query(7, harddisk_cmd_get_status, &hstatus, &hsize) ==
        peripheral_ok) {
      iHDDStatus = hstatus.activity_status;
    }

    leds.at(0) = static_cast<char>(led_char_base + iDrive1Status);
    font_print(8, 23, leds.data(), g_hStatusSurface, 4.0f, 2.7f);

    leds.at(0) = static_cast<char>(led_char_base + iDrive2Status);
    font_print(40, 23, leds.data(), g_hStatusSurface, 4.0f, 2.7f);

    leds.at(0) = static_cast<char>(led_char_base + iHDDStatus);
    font_print(71, 23, leds.data(), g_hStatusSurface, 4.0f, 2.7f);

    if ((iDrive1Status | iDrive2Status | iHDDStatus) != 0) {
      g_iStatusCycle = SHOW_CYCLES;
    }
  }
}

void FrameShowHelpScreen(int sx, int sy) {
  (void)sy;
  constexpr int max_lines = 25;
  static const std::array<const char*, max_lines> HelpStrings = {
      {"Welcome to LinApple - Apple][ emulator for Linux!",
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
       "  Numpad +/-/* - Increase/Decrease/Normal speed"}};

  VideoSurface* tempSurface = nullptr;

  if (font_sfc == nullptr) {
    if (fonts_initialization() == false) {
      fprintf(stderr, "Font file was not loaded.\n");
      return;
    }
  }
  if (g_window_resized == false) {
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
    vs_screen = sdl_surface_to_video_surface(screen);
    tempSurface = &vs_screen;
  }

  VideoSurface vs_actual_screen = sdl_surface_to_video_surface(screen);

  // Capture original screen
  VideoSoftStretch(tempSurface, nullptr, &vs_actual_screen, nullptr);

  // Blur the background by downscaling and upscaling
  // We use a small temporary surface (1/16 size) to create a pixelated blur
  // effect
  SDL_Surface* blur_temp = SDL_CreateRGBSurfaceWithFormat(
      0, screen->w / 16, screen->h / 16, 32, SDL_PIXELFORMAT_ARGB8888);
  if (blur_temp != nullptr) {
    VideoSurface vs_blur = sdl_surface_to_video_surface(blur_temp);
    VideoSoftStretch(&vs_actual_screen, nullptr, &vs_blur,
                     nullptr);  // Downscale
    VideoSoftStretch(&vs_blur, nullptr, &vs_actual_screen,
                     nullptr);  // Upscale back
    SDL_FreeSurface(blur_temp);
  }

  // Dim the background using SDL blending for better text readability
  SDL_Surface* dim_surface = SDL_CreateRGBSurfaceWithFormat(
      0, screen->w, screen->h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (dim_surface != nullptr) {
    Uint32 dim_color = SDL_MapRGBA(dim_surface->format, 0, 0, 0, 200);
    SDL_FillRect(dim_surface, nullptr, dim_color);
    SDL_SetSurfaceBlendMode(dim_surface, SDL_BLENDMODE_BLEND);
    SDL_BlitSurface(dim_surface, nullptr, screen, nullptr);
    SDL_FreeSurface(dim_surface);
  }

  const float facx_f = static_cast<float>(g_state.ScreenWidth) /
                       static_cast<float>(SCREEN_WIDTH);
  const float facy_f = static_cast<float>(g_state.ScreenHeight) /
                       static_cast<float>(SCREEN_HEIGHT);
  const double facy = static_cast<double>(facy_f);

  font_print_centered(sx / 2, static_cast<int>(5.0 * facy),
                      const_cast<char*>(HelpStrings.at(0)), &vs_actual_screen,
                      1.5f * facx_f, 1.3f * facy_f);
  font_print_centered(sx / 2, static_cast<int>(20.0 * facy),
                      const_cast<char*>(HelpStrings.at(1)), &vs_actual_screen,
                      1.3f * facx_f, 1.2f * facy_f);
  font_print_centered(sx / 2, static_cast<int>(30.0 * facy),
                      const_cast<char*>(HelpStrings.at(2)), &vs_actual_screen,
                      1.2f * facx_f, 1.0f * facy_f);

  int Help_TopX = static_cast<int>(45.0 * facy);
  for (int i = 3; i < max_lines; i++) {
    if (HelpStrings.at(i) != nullptr) {
      font_print(4,
                 static_cast<int>(static_cast<double>(Help_TopX) +
                                  static_cast<double>(i - 3) * 15.0 * facy),
                 const_cast<char*>(HelpStrings.at(i)), &vs_actual_screen,
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

  VideoRect logo{};
  VideoRect scrr{};
  logo.x = logo.y = 0;
  logo.w = vs_icon.w;
  logo.h = vs_icon.h;
  scrr.x = static_cast<int16_t>(460.0f * facx_f);
  scrr.y = static_cast<int16_t>(270.0f * facy_f);
  scrr.w = static_cast<int16_t>(100.0f * facy_f);
  scrr.h = static_cast<int16_t>(100.0f * facy_f);
  VideoSoftStretchOr(&vs_icon, &logo, &vs_actual_screen, &scrr);

  frame_refresh();
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
void frame_quick_state(int num, int mod) {
  // quick load or save state with number num, if Shift is pressed, state is
  // being saved, otherwise - being loaded
  std::array<char, path_max_len> fpath;
  snprintf(fpath.data(), fpath.size(), "%.*s/SaveState%d.aws",
           static_cast<int>(strlen(g_state.sSaveStateDir.data())),
           g_state.sSaveStateDir.data(), num);
  save_state_set_filename(fpath.data());
  if ((mod & KMOD_SHIFT) != 0) {
    save_state_save();
  } else {
    save_state_load();
  }
}

auto is_modifier_key(SDL_Keycode sym) -> bool {
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
  g_state.ScreenWidth = static_cast<uint32_t>(width);
  g_state.ScreenHeight = static_cast<uint32_t>((height / 96) * 96);
  if (g_state.ScreenHeight < 192) {
    g_state.ScreenHeight = 192;
  }

  if (screen != nullptr) SDL_FreeSurface(screen);
  screen = SDL_CreateRGBSurfaceWithFormat(0, g_state.ScreenWidth,
                                          g_state.ScreenHeight, 32,
                                          SDL_PIXELFORMAT_ARGB8888);

  if (g_texture != nullptr) SDL_DestroyTexture(g_texture);
  g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                g_state.ScreenWidth, g_state.ScreenHeight);

  if (screen == nullptr || g_texture == nullptr) {
    g_video_draw_mutex.unlock();
    SDL_Quit();
    return;
  }
  g_window_resized = (g_state.ScreenWidth != SCREEN_WIDTH) |
                    (g_state.ScreenHeight != SCREEN_HEIGHT);
  if (g_window_resized) {
    orig_rect.x = orig_rect.y = new_rect.x = new_rect.y = 0;
    orig_rect.w = static_cast<int16_t>(SCREEN_WIDTH);
    orig_rect.h = static_cast<int16_t>(SCREEN_HEIGHT);
    new_rect.w = static_cast<int16_t>(g_state.ScreenWidth);
    new_rect.h = static_cast<int16_t>(g_state.ScreenHeight);
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
    SDL_Keymod mod = SDL_GetModState();
    uint8_t caps = ((mod & KMOD_CAPS) != 0) ? 1 : 0;
    peripheral_command(0, keyboard_cmd_set_caps, &caps, 1);
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
    if (choose_an_image(g_state.ScreenWidth, g_state.ScreenHeight, fullPath,
                      saveit ? 1 : 0, filename, isDirectory,
                      fileIndex) == false) {
      DrawFrameWindow();
      return false;
    }
    if (isDirectory) {
      if (filename == "..") {
        const auto last_sep_pos = fullPath.find_last_of(file_separator);
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
  Configuration_t::instance().set_string("Preferences", "Save State Directory",
                                      g_state.sSaveStateDir.data());
  Configuration_t::instance().save();

  backdx = static_cast<int>(fileIndex);

  fullPath += "/" + filename;

  save_state_set_filename(fullPath.c_str());
  Configuration_t::instance().set_string(
      "Preferences", REGVALUE_SAVESTATE_FILENAME, fullPath.c_str());
  Configuration_t::instance().save();
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
  while (stat(bmpName.data(), &bufp) == 0) {
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
void process_button_click(int button, int mod) {
  SDL_Event qe;

  audio_mixer_set_fade(fade_out);

  switch (button) {
    case btn_help:
      FrameShowHelpScreen(screen->w, screen->h);
      break;

    case btn_run:
      if ((mod & (KMOD_LCTRL)) == (KMOD_LCTRL) ||
          (mod & (KMOD_RCTRL)) == (KMOD_RCTRL)) {
        if (g_state.mode == MODE_LOGO) {
          peripheral_command(disk_default_slot, disk_cmd_boot, nullptr, 0);
        } else if (g_state.mode == MODE_RUNNING) {
          ResetMachineState();
        }
        if ((g_state.mode == MODE_DEBUG) || (g_state.mode == MODE_STEPPING)) {
          debug_end();
        }
        g_state.mode = MODE_RUNNING;
        draw_status_area(DRAW_TITLE);
        VideoRedrawScreen();
        g_state.bResetTiming = true;
      } else if ((mod & KMOD_SHIFT) != 0) {
        g_state.restart = true;
        qe.type = SDL_QUIT;
        SDL_PushEvent(&qe);
      }
      break;

    case btn_drive1:
    case btn_drive2:
      peripheral_command(0, JOY_CMD_RESET, nullptr, 0);
      if ((mod & KMOD_CTRL) != 0) {
        if ((mod & KMOD_SHIFT) != 0) {
          printf("HDD  Eject Drive #%d\n", (button - btn_drive1) + 1);
          HarddiskEjectCmd_t ecmd = {static_cast<uint8_t>(button - btn_drive1)};
          peripheral_command(7, harddisk_cmd_eject, &ecmd, sizeof(ecmd));
        } else {
          printf("Disk Eject Drive #%d\n", (button - btn_drive1) + 1);
          DiskEjectCmd_t ecmd{};
          ecmd.drive = static_cast<uint8_t>(button - btn_drive1);
          peripheral_command(disk_default_slot, disk_cmd_eject, &ecmd,
                             sizeof(ecmd));
        }
        break;
      }

      if ((mod & KMOD_SHIFT) != 0) {
        if ((mod & KMOD_ALT) != 0) {
          HarddiskUI_FTPSelect(button - btn_drive1);
        } else {
          HarddiskUI_Select(button - btn_drive1);
        }
      } else {
        extern void DiskSelect(int drive);
        extern void Disk_FTP_SelectImage(int drive);
        if ((mod & KMOD_ALT) != 0) {
          Disk_FTP_SelectImage(button - btn_drive1);
        } else {
          DiskSelect(button - btn_drive1);
        }
      }
      break;

    case btn_driveswap:
      peripheral_command(disk_default_slot, disk_cmd_swap_drives, nullptr, 0);
      break;

    case btn_fullscr:
      if ((mod & KMOD_SHIFT) != 0) {
        // only IIe and enhanced have a keyboard rocker switch (and only non-US
        // keyboards)
        if ((g_Language != A2LANG_US) &&
            ((g_Apple2Type == A2TYPE_APPLE2E) ||
             (g_Apple2Type == A2TYPE_APPLE2EENHANCED))) {
          uint8_t cur_rocker = 0;
          size_t rocker_sz = sizeof(cur_rocker);
          peripheral_query(0, keyboard_query_rocker, &cur_rocker, &rocker_sz);
          uint8_t new_rocker = (cur_rocker != 0) ? 0 : 1;
          peripheral_command(0, keyboard_cmd_set_rocker, &new_rocker, 1);
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
        peripheral_command(0, JOY_CMD_RESET, nullptr, 0);
      }
      break;

    case btn_debug:
#if ENABLE_DEBUGGER
      if (!g_state.bDisableDebugger) {
        if (g_state.mode != MODE_DEBUG) {
          debug_begin();
          set_using_cursor(false);
        } else if (g_state.mode == MODE_DEBUG) {
          g_state.mode = MODE_RUNNING;
        }
      }
#endif
      break;

    case btn_setup:
      if ((mod & KMOD_SHIFT) != 0) {
        Configuration_t::instance().set_int("Configuration", "Video Emulation",
                                         static_cast<int>(g_videotype));
        Configuration_t::instance().set_int("Configuration", "Emulation Speed",
                                         static_cast<int>(g_state.dwSpeed));
        Configuration_t::instance().set_int("Configuration", "Fullscreen",
                                         g_state.fullscreen ? 1 : 0);
        Configuration_t::instance().save();

      } else {
        FrameSaveBMP();
      }
      break;

    case btn_cycle:
      if ((mod & KMOD_SHIFT) != 0) {
        set_budget_video(!get_budget_video());
      } else {
        g_videotype++;
        if (g_videotype >= VT_NUM_MODES) {
          g_videotype = 0;
        }
        VideoReinitialize();
        if (g_state.mode != MODE_LOGO) {
          if (g_state.mode == MODE_DEBUG) {
            uint32_t debugVideoMode = 0;
            if (debug_get_video_mode(&debugVideoMode)) {
              VideoRefreshScreen();
            }
          } else {
            VideoRefreshScreen();
          }
        }
      }
      break;
    case btn_quit:
      qe.type = SDL_QUIT;
      SDL_PushEvent(&qe);
      break;
    case btn_savest:
      if ((mod & KMOD_ALT) != 0) {
        save_state_save();
      } else if (PSP_SaveStateSelectImage(true)) {
        save_state_save();
      }
      break;
    case btn_loadst:
      if ((mod & KMOD_CTRL) != 0) {
        if (IS_APPLE2() == false) {
          MemResetPaging();
        }

        peripheral_manager_reset();
        if (IS_APPLE2() == false) {
          VideoResetState();
        }
        CpuReset();
      } else if ((mod & KMOD_ALT) != 0) {
        save_state_load();
      } else if (PSP_SaveStateSelectImage(false)) {
        save_state_load();
      }
      break;
  }

  if ((g_state.mode != MODE_DEBUG) && (g_state.mode != MODE_PAUSED)) {
    audio_mixer_set_fade(fade_in);
  }
}

void ResetMachineState() {
  g_bFullSpeed = false;  // Might've hit reset in middle of InternalCpuExecute()
                         // - so beep may get (partially) muted

  MemReset();
  peripheral_manager_reset();
  peripheral_command(disk_default_slot, disk_cmd_boot, nullptr, 0);
  VideoResetState();
  peripheral_command(0, JOY_CMD_RESET, nullptr, 0);
}

static bool bIamFullScreened;

void SetFullScreenMode() {
  if (bIamFullScreened == false) {
    bIamFullScreened = true;
    SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN);
    if (g_state.mode != MODE_DEBUG) {
      SDL_ShowCursor(SDL_DISABLE);
    }
  }
}

void SetNormalMode() {
  if (bIamFullScreened) {
    bIamFullScreened = false;
    SDL_SetWindowFullscreen(g_window, 0);
    if (usingcursor == false) {
      SDL_ShowCursor(SDL_ENABLE);
    }
  } else if (g_state.mode == MODE_DEBUG) {
    SDL_ShowCursor(SDL_ENABLE);
    SDL_SetWindowGrab(g_window, SDL_FALSE);
  }
}

void set_using_cursor(bool newvalue) {
  usingcursor = newvalue;
  if (usingcursor) {
    SDL_ShowCursor(SDL_DISABLE);
    SDL_SetWindowGrab(g_window, SDL_TRUE);
  } else {
    if ((bIamFullScreened == false) || (g_state.mode == MODE_DEBUG)) {
      SDL_ShowCursor(SDL_ENABLE);
    }
    SDL_SetWindowGrab(g_window, SDL_FALSE);
  }
}

extern void SDL_Asset_LoadIcon();
extern void SDL_Asset_FreeIcon();

auto frame_create_window() -> int {
  SDL_Asset_LoadIcon();
  bIamFullScreened = false;

  Uint32 flags = 0;
  if (g_state.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;

  g_window = SDL_CreateWindow(g_pAppTitle, SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              static_cast<int>(g_state.ScreenWidth),
                              static_cast<int>(g_state.ScreenHeight), flags);
  if (g_window == nullptr) {
    fprintf(stderr, "Could not create SDL window: %s\n", SDL_GetError());
    return 1;
  }

  g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
  if (g_renderer == nullptr) {
    fprintf(stderr, "Could not create SDL renderer: %s\n", SDL_GetError());
    return 1;
  }

  screen = SDL_CreateRGBSurfaceWithFormat(0, g_state.ScreenWidth,
                                          g_state.ScreenHeight, 32,
                                          SDL_PIXELFORMAT_ARGB8888);
  if (screen == nullptr) {
    fprintf(stderr, "Could not create SDL surface: %s\n", SDL_GetError());
    return 1;
  }

  g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, 560, 384);

  SDL_RenderSetLogicalSize(g_renderer, 560, 384);
  SDL_ShowWindow(g_window);
  SetIcon();

  g_window_resized = (g_state.ScreenWidth != SCREEN_WIDTH) |
                    (g_state.ScreenHeight != SCREEN_HEIGHT);
  printf("Screen size is %ux%u\n", g_state.ScreenWidth, g_state.ScreenHeight);
  if (g_window_resized) {
    orig_rect.x = orig_rect.y = new_rect.x = new_rect.y = 0;
    orig_rect.w = static_cast<int16_t>(SCREEN_WIDTH);
    orig_rect.h = static_cast<int16_t>(SCREEN_HEIGHT);
    new_rect.w = static_cast<int16_t>(g_state.ScreenWidth);
    new_rect.h = static_cast<int16_t>(g_state.ScreenHeight);
  }
  return 0;
}

void SetIcon() {
  /* Black is the transparency colour.
     Part of the logo seems to use it !? */
  auto* icon = static_cast<SDL_Surface*>(assets->icon);
  Uint32 colorkey = SDL_MapRGB(icon->format, 0, 0, 0);
  SDL_SetColorKey(icon, SDL_TRUE, colorkey);

  /* No need to pass a mask given the above. */
  SDL_SetWindowIcon(g_window, icon);
}

auto init_sdl() -> int {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
    fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
    return 1;
  }

  // SDL ref: Icon should be set *before* the first call to SDL_SetVideoMode.
  return 0;
}

void frame_refresh_status(int drawflags) {
  if ((drawflags & DRAW_LEDS) != 0) {
    size_t size = sizeof(g_lastDiskStatus);
    if (peripheral_query(disk_default_slot, disk_cmd_get_status,
                         &g_lastDiskStatus, &size) == peripheral_ok) {
      if (g_lastDiskStatus.drive0_last_error != disk_err_none &&
          g_lastDiskStatus.drive0_last_error != g_drive0_last_reported_error) {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "Disk 1 error",
            disk_ui_get_error_message(g_lastDiskStatus.drive0_last_error),
            g_window);
        g_drive0_last_reported_error = g_lastDiskStatus.drive0_last_error;
      } else if (g_lastDiskStatus.drive0_last_error == disk_err_none) {
        g_drive0_last_reported_error = disk_err_none;
      }

      if (g_lastDiskStatus.drive1_last_error != disk_err_none &&
          g_lastDiskStatus.drive1_last_error != g_drive1_last_reported_error) {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "Disk 2 error",
            disk_ui_get_error_message(g_lastDiskStatus.drive1_last_error),
            g_window);
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
      linapple_update_title(s_title.data());
    }
  }
  draw_status_area(drawflags);
}
