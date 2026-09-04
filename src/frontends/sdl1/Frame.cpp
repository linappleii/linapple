// SPDX-License-Identifier: GPL-2.0-only
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

#include "frontends/sdl1/Frame.h"

#include <SDL/SDL.h>
#include <sys/stat.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "Debugger/Debug.h"
#include "SDL_error.h"
#include "SDL_events.h"
#include "SDL_keyboard.h"
#include "SDL_keysym.h"
#include "SDL_mouse.h"
#include "SDL_stdinc.h"
#include "SDL_timer.h"
#include "SDL_video.h"
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/joystick/JoystickCommands.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/Asset.h"
#include "core/AudioMixer.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/HelpText.h"
#include "frontends/common/SaveStateManager.h"
#include "frontends/common/VideoStretch.h"
#include "frontends/common/VideoSurface.h"
#include "frontends/common/sdl/DiskUI.h"
#include "frontends/sdl1/DiskChoose.h"
#include "frontends/sdl1/SDL_Video.h"

SDL_Surface* g_apple_icon;
SDL_Surface* g_screen = nullptr;
SDL_Surface* g_texture = nullptr;
SDL_Rect g_orig_rect;
SDL_Rect g_new_rect;

enum { BUTTONY = 0, BUTTONCX = 45, BUTTONCY = 45 };

static bool g_app_active = false;

static DiskStatus_t g_last_disk_status{};
static int g_drive0_last_reported_error = disk_err_none;
static int g_drive1_last_reported_error = disk_err_none;

int g_buttondown = -1;

bool g_window_resized;

bool g_usingcursor = false;

void draw_status_area(int drawflags);

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): button and mod are
// semantically distinct
void process_button_click(int button, int mod);

void reset_machine_state();

void set_fullscreen_mode();

void set_normal_mode();

void set_using_cursor(bool);

void set_icon();

bool g_scroll_lock_full_speed = false;

void draw_apple_content() {
  g_video_draw_mutex.lock();
  video_realize_palette();

  draw_status_area(DRAW_BACKGROUND | DRAW_LEDS);

  if (g_state.mode == MODE_LOGO) {
    video_display_logo();
    g_frame_ready = true;
  } else if (g_state.mode == MODE_DEBUG) {
    debug_display(true);
    g_frame_ready = true;
  } else {
    video_redraw_screen();
  }
  g_video_draw_mutex.unlock();
}

void frame_refresh() {
  if (g_screen != nullptr) {
    SDL_Flip(g_screen);
  }
}

static inline auto to_video_rect(const SDL_Rect& r) -> VideoRect_t {
  return {static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.w),
          static_cast<int>(r.h)};
}

void draw_frame_window() {
  if (g_frame_ready == false) return;

  g_video_draw_mutex.lock();
  if (g_texture != nullptr && g_screen != nullptr) {
    uint32_t* output = video_get_output_buffer();
    SDL_Rect r = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

    // Fill g_screen from RGB32 output buffer
    if (g_state.mode != MODE_DEBUG) {
      VideoSurface_t vs_texture = sdl_surface_to_video_surface(g_texture);
      VideoSurface_t vs_output{};
      vs_output.pixels = reinterpret_cast<uint8_t*>(output);
      vs_output.w = SCREEN_WIDTH;
      vs_output.h = SCREEN_HEIGHT;
      vs_output.pitch = SCREEN_WIDTH * 4;
      vs_output.bpp = 4;

      if (g_window_resized == false) {
        VideoRect_t vr = to_video_rect(r);
        video_soft_stretch(&vs_output, &vr, &vs_texture, &vr);
      } else {
        VideoRect_t vor = to_video_rect(g_orig_rect);
        VideoRect_t vnr = to_video_rect(g_new_rect);
        video_soft_stretch(&vs_output, &vor, &vs_texture, &vnr);
      }
    } else {
      // Debugger draws directly to g_debug_screen (INDEX8)
      // We need to stretch/convert it to the RGB32 g_screen surface
      extern VideoSurface_t* g_debug_screen;
      if (g_debug_screen != nullptr) {
        VideoSurface_t vs_texture = sdl_surface_to_video_surface(g_texture);
        if (g_window_resized == false) {
          VideoRect_t vr = to_video_rect(r);
          video_soft_stretch(g_debug_screen, &vr, &vs_texture, &vr);
        } else {
          VideoRect_t vor = to_video_rect(g_orig_rect);
          VideoRect_t vnr = to_video_rect(g_new_rect);
          video_soft_stretch(g_debug_screen, &vor, &vs_texture, &vnr);
        }
      }
    }

    SDL_BlitSurface(g_texture, nullptr, g_screen, nullptr);
    frame_refresh();
    g_frame_ready = false;
  }
  g_video_draw_mutex.unlock();
}

void draw_status_area(int drawflags) {
  if (g_status_surface == nullptr || g_status_surface->pixels == nullptr) {
    return;
  }
  if (font_sfc == nullptr) {
    if (fonts_initialization() == false) {
      fprintf(stderr, "Font file was not loaded.\n");
      return;
    }
  }

  VideoRect_t srect{};
  uint8_t mybluez = DARK_BLUE;

  if ((drawflags & DRAW_BACKGROUND) != 0) {
    g_status_cycle = SHOW_CYCLES;
  }
  if ((drawflags & DRAW_LEDS) != 0) {
    srect.x = 4;
    srect.y = 22;
    srect.w = static_cast<int16_t>(STATUS_PANEL_W - 8);
    srect.h = static_cast<int16_t>(STATUS_PANEL_H - 25);

    for (int y = srect.y; y < srect.y + srect.h; ++y) {
      memset(g_status_surface->pixels +
                 static_cast<ptrdiff_t>(y * g_status_surface->pitch) + srect.x,
             mybluez, static_cast<size_t>(srect.w));
    }

    std::array<char, 2> leds = {{"\x64"}};
    constexpr int led_char_base = 1;
    int drive1_status = disk_status_off;
    int drive2_status = disk_status_off;
    int hdd_status = disk_status_off;

    if (g_last_disk_status.drive0_spinning != 0) {
      drive1_status = (g_last_disk_status.drive0_writing != 0)
                          ? disk_status_write
                          : disk_status_read;
    } else if (g_last_disk_status.drive0_loaded != 0 &&
               g_last_disk_status.drive0_write_protected != 0) {
      drive1_status = disk_status_prot;
    }

    if (g_last_disk_status.drive1_spinning != 0) {
      drive2_status = (g_last_disk_status.drive1_writing != 0)
                          ? disk_status_write
                          : disk_status_read;
    } else if (g_last_disk_status.drive1_loaded != 0 &&
               g_last_disk_status.drive1_write_protected != 0) {
      drive2_status = disk_status_prot;
    }

    HarddiskStatus_t hstatus{};
    size_t hsize = sizeof(hstatus);
    if (peripheral_query(7, harddisk_cmd_get_status, &hstatus, &hsize) ==
        peripheral_ok) {
      hdd_status = hstatus.activity_status;
    }

    leds.at(0) = static_cast<char>(led_char_base + drive1_status);
    font_print(8, 23, leds.data(), g_status_surface, 4.0f, 2.7f);

    leds.at(0) = static_cast<char>(led_char_base + drive2_status);
    font_print(40, 23, leds.data(), g_status_surface, 4.0f, 2.7f);

    leds.at(0) = static_cast<char>(led_char_base + hdd_status);
    font_print(71, 23, leds.data(), g_status_surface, 4.0f, 2.7f);

    if ((drive1_status | drive2_status | hdd_status) != 0) {
      g_status_cycle = SHOW_CYCLES;
    }
  }
}

void frame_show_help_screen(int sx, int sy) {
  (void)sy;

  if (g_screen == nullptr) {
    return;
  }

  VideoSurface_t* tempSurface = nullptr;

  if (font_sfc == nullptr) {
    if (fonts_initialization() == false) {
      fprintf(stderr, "Font file was not loaded.\n");
      return;
    }
  }
  if (g_window_resized == false) {
    if (g_state.mode == MODE_LOGO) {
      tempSurface = g_logo_bitmap;
    } else {
      tempSurface = g_device_bitmap;
    }
  } else {
    tempSurface = g_origscreen;
  }

  if (tempSurface == nullptr) {
    // Wrap g_screen as fallback
    static VideoSurface_t vs_screen;
    vs_screen = sdl_surface_to_video_surface(g_screen);
    tempSurface = &vs_screen;
  }

  VideoSurface_t vs_actual_g_screen = sdl_surface_to_video_surface(g_screen);

  // Capture original g_screen
  video_soft_stretch(tempSurface, nullptr, &vs_actual_g_screen, nullptr);

  // Blur the background by downscaling and upscaling
  // We use a small temporary surface (1/16 size) to create a pixelated blur
  // effect
  SDL_Surface* blur_temp =
      SDL_CreateRGBSurface(0, g_screen->w / 16, g_screen->h / 16, 32,
                           0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
  if (blur_temp != nullptr) {
    VideoSurface_t vs_blur = sdl_surface_to_video_surface(blur_temp);
    video_soft_stretch(&vs_actual_g_screen, nullptr, &vs_blur,
                       nullptr);  // Downscale
    video_soft_stretch(&vs_blur, nullptr, &vs_actual_g_screen,
                       nullptr);  // Upscale back
    SDL_FreeSurface(blur_temp);
  }

  // Dim the background using SDL blending for better text readability
  SDL_Surface* dim_surface =
      SDL_CreateRGBSurface(0, g_screen->w, g_screen->h, 32, 0x00FF0000,
                           0x0000FF00, 0x000000FF, 0xFF000000);
  if (dim_surface != nullptr) {
    Uint32 dim_color = SDL_MapRGBA(dim_surface->format, 0, 0, 0, 200);
    SDL_FillRect(dim_surface, nullptr, dim_color);
    SDL_SetAlpha(dim_surface, SDL_SRCALPHA, 200);
    SDL_BlitSurface(dim_surface, nullptr, g_screen, nullptr);
    SDL_FreeSurface(dim_surface);
  }

  const float facx_f = static_cast<float>(g_state.screen_width) /
                       static_cast<float>(SCREEN_WIDTH);
  const float facy_f = static_cast<float>(g_state.screen_height) /
                       static_cast<float>(SCREEN_HEIGHT);

  const float scale_x = facx_f;
  const float scale_y = facy_f;

  const int hdr_top = static_cast<int>(4.0f * facy_f);
  const int hdr_height = static_cast<int>(42.0f * facy_f);
  rectangle(&vs_actual_g_screen, static_cast<int>(4.0f * facx_f), hdr_top,
            static_cast<int>(g_state.screen_width - (8.0f * facx_f)),
            hdr_height, 0xFFFF00);

  font_print_centered(sx / 2, hdr_top + static_cast<int>(4.0f * facy_f),
                      const_cast<char*>(HELP_HEADER_STRINGS.at(0)),
                      &vs_actual_g_screen, scale_x, scale_y);
  font_print_centered(sx / 2, hdr_top + static_cast<int>(16.0f * facy_f),
                      const_cast<char*>(HELP_HEADER_STRINGS.at(1)),
                      &vs_actual_g_screen, scale_x, scale_y);
  font_print_centered(sx / 2, hdr_top + static_cast<int>(28.0f * facy_f),
                      const_cast<char*>(HELP_HEADER_STRINGS.at(2)),
                      &vs_actual_g_screen, scale_x, scale_y);

  const int body_top = hdr_top + hdr_height + static_cast<int>(4.0f * facy_f);
  const int body_height =
      static_cast<int>(g_state.screen_height - body_top - (4.0f * facy_f));
  rectangle(&vs_actual_g_screen, static_cast<int>(4.0f * facx_f), body_top,
            static_cast<int>(g_state.screen_width - (8.0f * facx_f)),
            body_height, 0xFFFFFF);

  const float line_spacing = 13.0f * facy_f;
  for (size_t i = 0; i < HELP_BODY_LINES.size(); i++) {
    if (HELP_BODY_LINES.at(i).text != nullptr &&
        HELP_BODY_LINES.at(i).text[0] != '\0') {
      font_print(
          static_cast<int>(16.0f * facx_f),
          body_top + static_cast<int>(6.0f * facy_f +
                                      static_cast<float>(i) * line_spacing),
          const_cast<char*>(HELP_BODY_LINES.at(i).text), &vs_actual_g_screen,
          scale_x, scale_y);
    }
  }

  // Logo bit
  VideoSurface_t vs_icon =
      sdl_surface_to_video_surface(static_cast<SDL_Surface*>(assets->icon));

  VideoRect_t logo{};
  VideoRect_t scrr{};
  logo.x = logo.y = 0;
  logo.w = vs_icon.w;
  logo.h = vs_icon.h;
  scrr.x = static_cast<int16_t>(460.0f * facx_f);
  scrr.y = static_cast<int16_t>(270.0f * facy_f);
  scrr.w = static_cast<int16_t>(100.0f * facy_f);
  scrr.h = static_cast<int16_t>(100.0f * facy_f);
  video_soft_stretch_or(&vs_icon, &logo, &vs_actual_g_screen, &scrr);

  frame_refresh();

  SDL_Event event;
  bool waiting = true;
  while (waiting) {
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_F12) {
          g_state.mode = MODE_EXIT;
          SDL_Event qe = {};
          qe.type = SDL_QUIT;
          SDL_PushEvent(&qe);
        }
        waiting = false;
        break;
      }
      if (event.type == SDL_QUIT) {
        SDL_PushEvent(&event);
        g_state.mode = MODE_EXIT;
        waiting = false;
        break;
      }
    }
    if (waiting) {
      SDL_Delay(10);
    }
  }

  if (g_screen != nullptr) {
    SDL_FillRect(g_screen, nullptr, 0);
  }
  g_frame_ready = true;
  draw_frame_window();
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): num and mod are
// semantically distinct
void frame_quick_state(int num, int mod) {
  // quick load or save state with number num, if Shift is pressed, state is
  // being saved, otherwise - being loaded
  std::array<char, path_max_len> fpath;
  snprintf(fpath.data(), fpath.size(), "%.*s/SaveState%d.aws",
           static_cast<int>(strlen(g_state.save_state_dir.data())),
           g_state.save_state_dir.data(), num);
  save_state_set_filename(fpath.data());
  if ((mod & KMOD_SHIFT) != 0) {
    save_state_save();
  } else {
    save_state_load();
  }
}

auto is_modifier_key(SDLKey sym) -> bool {
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

static bool is_full_screened = false;
static uint32_t s_windowed_width = 0;
static uint32_t s_windowed_height = 0;

void frame_on_resize(int width, int height) {
  if (width <= 0 || height <= 0) {
    return;
  }

  g_video_draw_mutex.lock();
  g_state.screen_width = static_cast<uint32_t>(width);
  g_state.screen_height = static_cast<uint32_t>(height);

  if (!is_full_screened) {
    s_windowed_width = static_cast<uint32_t>(width);
    s_windowed_height = static_cast<uint32_t>(height);
  }

  Uint32 flags = SDL_SWSURFACE | SDL_RESIZABLE;
  if (g_state.fullscreen) flags |= SDL_FULLSCREEN;

  g_screen =
      SDL_SetVideoMode(static_cast<int>(g_state.screen_width),
                       static_cast<int>(g_state.screen_height), 32, flags);

  if (g_texture != nullptr) {
    SDL_FreeSurface(g_texture);
    g_texture = nullptr;
  }
  g_texture =
      SDL_CreateRGBSurface(0, g_state.screen_width, g_state.screen_height, 32,
                           0x00FF0000, 0x0000FF00, 0x000000FF, 0);

  if (g_screen == nullptr || g_texture == nullptr) {
    g_state.mode = MODE_EXIT;
    g_video_draw_mutex.unlock();
    return;
  }
  g_window_resized = (g_state.screen_width != SCREEN_WIDTH) |
                     (g_state.screen_height != SCREEN_HEIGHT);
  if (g_window_resized) {
    g_orig_rect.x = g_orig_rect.y = 0;
    g_orig_rect.w = static_cast<int16_t>(SCREEN_WIDTH);
    g_orig_rect.h = static_cast<int16_t>(SCREEN_HEIGHT);
    if (is_full_screened) {
      int target_w = width;
      int target_h = (target_w * SCREEN_HEIGHT) / SCREEN_WIDTH;
      if (target_h > height) {
        target_h = height;
        target_w = (target_h * SCREEN_WIDTH) / SCREEN_HEIGHT;
      }
      int offset_x = (width - target_w) / 2;
      int offset_y = (height - target_h) / 2;
      g_new_rect.x = static_cast<int16_t>(offset_x);
      g_new_rect.y = static_cast<int16_t>(offset_y);
      g_new_rect.w = static_cast<int16_t>(target_w);
      g_new_rect.h = static_cast<int16_t>(target_h);
    } else {
      g_new_rect.x = 0;
      g_new_rect.y = 0;
      g_new_rect.w = static_cast<int16_t>(g_state.screen_width);
      g_new_rect.h = static_cast<int16_t>(g_state.screen_height);
    }
    if ((g_state.mode != MODE_LOGO) && (g_state.mode != MODE_DEBUG)) {
      video_redraw_screen();
    }
  }
  g_video_draw_mutex.unlock();
}

void frame_on_focus(bool gained) {
  g_app_active = gained;
  if (g_app_active && keyboard_get_caps_mode() == CAPS_MODE_HOST) {
    // Re-sync Caps Lock state upon regaining focus
    SDLMod mod = SDL_GetModState();
    uint8_t caps = ((mod & KMOD_CAPS) != 0) ? 1 : 0;
    peripheral_command(0, keyboard_cmd_set_caps, &caps, 1);
  }
}

void frame_on_expose() {
  if ((g_state.mode != MODE_LOGO) && (g_state.mode != MODE_DEBUG)) {
    video_redraw_screen();
  }
}

auto psp_save_state_select_image(bool saveit) -> bool {
  static size_t fileIndex = 0;
  static int backdx = 0;
  static int dirdx = 0;

  std::string filename;  // given filename
  std::string fullPath;  // full path for it
  bool isDirectory = true;

  fileIndex = static_cast<size_t>(backdx);
  fullPath = g_state.save_state_dir.data();

  while (isDirectory) {
    if (choose_an_image(g_state.screen_width, g_state.screen_height, fullPath,
                        saveit ? 1 : 0, filename, isDirectory,
                        fileIndex) == false) {
      draw_frame_window();
      return false;
    }
    if (isDirectory) {
      if (filename == "..") {
        const auto last_sep_pos = fullPath.find_last_of(file_separator);
        if (last_sep_pos != std::string::npos) {
          if (last_sep_pos == 0) {
            fullPath = "/";
          } else {
            fullPath = fullPath.substr(0, last_sep_pos);
          }
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
  util_safe_strcpy(g_state.save_state_dir.data(), fullPath.c_str(),
                   g_state.save_state_dir.size());
  Configuration_t::instance().set_string("Preferences", "Save State Directory",
                                         g_state.save_state_dir.data());
  Configuration_t::instance().save();

  backdx = static_cast<int>(fileIndex);

  fullPath += "/" + filename;

  save_state_set_filename(fullPath.c_str());
  Configuration_t::instance().set_string(
      "Preferences", REGVALUE_SAVESTATE_FILENAME, fullPath.c_str());
  Configuration_t::instance().save();
  draw_frame_window();
  return true;
}

void frame_save_bmp() {
  if (g_screen == nullptr) {
    return;
  }
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
void process_button_click(int button, int mod) {
  SDL_Event qe;

  audio_mixer_set_fade(fade_out);

  switch (button) {
    case btn_help:
      if (g_screen != nullptr) {
        frame_show_help_screen(g_screen->w, g_screen->h);
      }
      break;

    case btn_run:
      if ((mod & (KMOD_LCTRL)) == (KMOD_LCTRL) ||
          (mod & (KMOD_RCTRL)) == (KMOD_RCTRL)) {
        if (g_state.mode == MODE_LOGO) {
          peripheral_command(disk_default_slot, disk_cmd_boot, nullptr, 0);
        } else if (g_state.mode == MODE_RUNNING) {
          reset_machine_state();
        }
        if ((g_state.mode == MODE_DEBUG) || (g_state.mode == MODE_STEPPING)) {
          debug_end();
        }
        g_state.mode = MODE_RUNNING;
        draw_status_area(DRAW_TITLE);
        video_redraw_screen();
        g_state.reset_timing = true;
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
          harddisk_ui_ftp_select(button - btn_drive1);
        } else {
          harddisk_ui_select(button - btn_drive1);
        }
      } else {
        extern void disk_select(int drive);
        extern void disk_ftp_select_image(int drive);
        if ((mod & KMOD_ALT) != 0) {
          disk_ftp_select_image(button - btn_drive1);
        } else {
          disk_select(button - btn_drive1);
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
        if ((g_language != A2LANG_US) &&
            ((g_apple2_type == A2TYPE_APPLE2E) ||
             (g_apple2_type == A2TYPE_APPLE2EENHANCED))) {
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
          set_normal_mode();
        } else {
          g_state.fullscreen = true;
          set_fullscreen_mode();
        }
        peripheral_command(0, JOY_CMD_RESET, nullptr, 0);
      }
      break;

    case btn_debug:
#if ENABLE_DEBUGGER
      if (!g_state.disable_debugger) {
        if (g_state.mode != MODE_DEBUG) {
          debug_begin();
          set_using_cursor(false);
        } else if (g_state.mode == MODE_DEBUG) {
          debug_end();
        }
      }
#endif
      break;

    case btn_setup:
      if ((mod & KMOD_SHIFT) != 0) {
        Configuration_t::instance().set_int("Configuration", "Video Emulation",
                                            static_cast<int>(g_videotype));
        Configuration_t::instance().set_int("Configuration", "Emulation Speed",
                                            static_cast<int>(g_state.speed));
        Configuration_t::instance().set_int("Configuration", "Fullscreen",
                                            g_state.fullscreen ? 1 : 0);
        Configuration_t::instance().save();

      } else {
        frame_save_bmp();
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
        video_reinitialize();
        if (g_state.mode != MODE_LOGO) {
          if (g_state.mode == MODE_DEBUG) {
            uint32_t debugVideoMode = 0;
            if (debug_get_video_mode(&debugVideoMode)) {
              video_redraw_screen();
            }
          } else {
            video_redraw_screen();
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
      } else if (psp_save_state_select_image(true)) {
        save_state_save();
      }
      break;
    case btn_loadst:
      if ((mod & KMOD_CTRL) != 0) {
        if (IS_APPLE2() == false) {
          mem_reset_paging();
        }

        peripheral_manager_reset();
        if (IS_APPLE2() == false) {
          video_reset_state();
        }
        cpu_reset();
      } else if ((mod & KMOD_ALT) != 0) {
        save_state_load();
      } else if (psp_save_state_select_image(false)) {
        save_state_load();
      }
      break;
    default:
      break;
  }

  if ((g_state.mode != MODE_DEBUG) && (g_state.mode != MODE_PAUSED)) {
    audio_mixer_set_fade(fade_in);
  }
}

void reset_machine_state() {
  g_full_speed =
      false;  // Might've hit reset in middle of internal_cpu_execute()
              // - so beep may get (partially) muted

  mem_reset();
  peripheral_manager_reset();
  peripheral_command(disk_default_slot, disk_cmd_boot, nullptr, 0);
  video_reset_state();
  peripheral_command(0, JOY_CMD_RESET, nullptr, 0);
}

void set_fullscreen_mode() {
  if (!is_full_screened) {
    is_full_screened = true;
    if (s_windowed_width == 0 || s_windowed_height == 0) {
      s_windowed_width = g_state.screen_width;
      s_windowed_height = g_state.screen_height;
    }
    SDL_WM_ToggleFullScreen(g_screen);
    if (g_state.mode != MODE_DEBUG) {
      SDL_ShowCursor(SDL_DISABLE);
    }
  }
}

void set_normal_mode() {
  if (is_full_screened) {
    is_full_screened = false;
    SDL_WM_ToggleFullScreen(g_screen);
    if (s_windowed_width > 0 && s_windowed_height > 0) {
      frame_on_resize(static_cast<int>(s_windowed_width),
                      static_cast<int>(s_windowed_height));
    }
    if (!g_usingcursor) {
      SDL_ShowCursor(SDL_ENABLE);
    }
  } else if (g_state.mode == MODE_DEBUG) {
    SDL_ShowCursor(SDL_ENABLE);
    SDL_WM_GrabInput(SDL_GRAB_OFF);
  }
}

void set_using_cursor(bool newvalue) {
  g_usingcursor = newvalue;
  if (g_usingcursor) {
    SDL_ShowCursor(SDL_DISABLE);
    SDL_WM_GrabInput(SDL_GRAB_ON);
  } else {
    if (!is_full_screened || (g_state.mode == MODE_DEBUG)) {
      SDL_ShowCursor(SDL_ENABLE);
    }
    SDL_WM_GrabInput(SDL_GRAB_OFF);
  }
}

extern void sdl_asset_load_icon();
extern void sdl_asset_free_icon();

auto frame_create_window() -> int {
  sdl_asset_load_icon();
  is_full_screened = false;
  if (!g_state.fullscreen) {
    s_windowed_width = g_state.screen_width;
    s_windowed_height = g_state.screen_height;
  }

  Uint32 flags = SDL_SWSURFACE;
  if (g_state.fullscreen) flags |= SDL_FULLSCREEN;

  g_screen =
      SDL_SetVideoMode(static_cast<int>(g_state.screen_width),
                       static_cast<int>(g_state.screen_height), 32, flags);
  if (g_screen == nullptr) {
    fprintf(stderr, "Could not set video mode: %s\n", SDL_GetError());
    return 1;
  }

  g_texture =
      SDL_CreateRGBSurface(0, g_state.screen_width, g_state.screen_height, 32,
                           0x00FF0000, 0x0000FF00, 0x000000FF, 0);

  SDL_WM_SetCaption(g_app_title, g_app_title);
  set_icon();

  g_window_resized = (g_state.screen_width != SCREEN_WIDTH) |
                     (g_state.screen_height != SCREEN_HEIGHT);
  printf("Screen size is %ux%u\n", g_state.screen_width, g_state.screen_height);
  if (g_window_resized) {
    g_orig_rect.x = g_orig_rect.y = g_new_rect.x = g_new_rect.y = 0;
    g_orig_rect.w = static_cast<int16_t>(SCREEN_WIDTH);
    g_orig_rect.h = static_cast<int16_t>(SCREEN_HEIGHT);
    g_new_rect.w = static_cast<int16_t>(g_state.screen_width);
    g_new_rect.h = static_cast<int16_t>(g_state.screen_height);
  }
  return 0;
}

void frame_destroy_window() {
  if (g_texture != nullptr) {
    SDL_FreeSurface(g_texture);
    g_texture = nullptr;
  }
  g_screen = nullptr;
  sdl_asset_free_icon();
}

void set_icon() {
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
    size_t size = sizeof(g_last_disk_status);
    if (peripheral_query(disk_default_slot, disk_cmd_get_status,
                         &g_last_disk_status, &size) == peripheral_ok) {
      if (g_last_disk_status.drive0_last_error != disk_err_none &&
          g_last_disk_status.drive0_last_error !=
              g_drive0_last_reported_error) {
        fprintf(
            stderr, "Disk 1 error: %s\n",
            disk_ui_get_error_message(g_last_disk_status.drive0_last_error));
        g_drive0_last_reported_error = g_last_disk_status.drive0_last_error;
      } else if (g_last_disk_status.drive0_last_error == disk_err_none) {
        g_drive0_last_reported_error = disk_err_none;
      }

      if (g_last_disk_status.drive1_last_error != disk_err_none &&
          g_last_disk_status.drive1_last_error !=
              g_drive1_last_reported_error) {
        fprintf(
            stderr, "Disk 2 error: %s\n",
            disk_ui_get_error_message(g_last_disk_status.drive1_last_error));
        g_drive1_last_reported_error = g_last_disk_status.drive1_last_error;
      } else if (g_last_disk_status.drive1_last_error == disk_err_none) {
        g_drive1_last_reported_error = disk_err_none;
      }

      std::array<char, 512> title_buf = {};
      if (g_last_disk_status.drive0_loaded != 0) {
        snprintf(title_buf.data(), title_buf.size(), "%s - %s", g_app_title,
                 g_last_disk_status.drive0_name);
      } else {
        snprintf(title_buf.data(), title_buf.size(), "%s", g_app_title);
      }
      linapple_update_title(title_buf.data());
    }
  }
  draw_status_area(drawflags);
}
