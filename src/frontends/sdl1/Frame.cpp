// SPDX-License-Identifier: GPL-2.0-only

#include "frontends/sdl1/Frame.h"

#include <SDL/SDL.h>
#include <SDL/SDL_error.h>
#include <SDL/SDL_events.h>
#include <SDL/SDL_keysym.h>
#include <SDL/SDL_mouse.h>
#include <SDL/SDL_stdinc.h>
#include <SDL/SDL_timer.h>
#include <SDL/SDL_video.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "Debugger/Debug.h"
#include "apple2/Apple2Types.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/keyboard/KeyboardCommands.h"
#include "core/Asset.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AudioMixer.h"
#include "frontends/common/HelpText.h"
#include "frontends/common/SaveStateManager.h"
#include "frontends/common/VideoStretch.h"
#include "frontends/common/VideoSurface.h"
#include "frontends/common/sdl/DiskUI.h"
#include "frontends/sdl1/DiskChoose.h"
#include "frontends/sdl1/SDL_Asset.h"
#include "frontends/sdl1/SDL_Video.h"
#include "frontends/sdl1/SdlPtr.h"

SDL_Surface* g_screen = nullptr;
SdlSurfacePtr_t g_texture;
SDL_Rect g_orig_rect{};
SDL_Rect g_new_rect{};

int g_buttondown = -1;
bool g_window_resized = false;
bool g_usingcursor = false;

namespace {

static bool s_app_active = false;
static DiskStatus_t s_last_disk_status{};
static int s_drive0_last_reported_error = disk_err_none;
static int s_drive1_last_reported_error = disk_err_none;
static bool s_is_fullscreen = false;
static uint32_t s_windowed_width = 0;
static uint32_t s_windowed_height = 0;

auto to_video_rect(const SDL_Rect& r) noexcept -> VideoRect_t {
  return {static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.w),
          static_cast<int>(r.h)};
}

auto reset_machine_state() -> void {
  full_speed = false;
  linapple_reset_hard();
}

auto set_icon() -> void {
  if (assets == nullptr || assets->icon == nullptr) {
    return;
  }
  auto* icon = static_cast<SDL_Surface*>(assets->icon);
  const Uint32 colorkey = SDL_MapRGB(icon->format, 0, 0, 0);
  SDL_SetColorKey(icon, SDL_SRCCOLORKEY, colorkey);
  SDL_WM_SetIcon(icon, nullptr);
}

auto frame_save_bmp() -> void {
  static int s_screenshot_counter = 0;
  std::array<char, 32> bmp_name{};
  snprintf(bmp_name.data(), bmp_name.size(), "linapple%04d.bmp",
           s_screenshot_counter);
  if (SDL_SaveBMP(g_screen, bmp_name.data()) == 0) {
    printf("File %s saved!\n", bmp_name.data());
  } else {
    fprintf(stderr, "Failed to save screenshot %s: %s\n", bmp_name.data(),
            SDL_GetError());
  }
  ++s_screenshot_counter;
}

auto handle_btn_run(int mod) -> void {
  if ((mod & KMOD_LCTRL) != 0 || (mod & KMOD_RCTRL) != 0) {
    if (system_state.mode == app_mode_logo) {
      linapple_reset_hard();
    } else if (system_state.mode == app_mode_running) {
      reset_machine_state();
    }
#if ENABLE_DEBUGGER
    if (system_state.mode == app_mode_debug ||
        system_state.mode == app_mode_stepping) {
      debug_end();
    }
#endif
    system_state.mode = app_mode_running;
    draw_status_area(draw_title);
    video_redraw_screen();
    system_state.reset_timing = true;
  } else if ((mod & KMOD_SHIFT) != 0) {
    system_state.restart = true;
    SDL_Event qe{};
    qe.type = SDL_QUIT;
    SDL_PushEvent(&qe);
  }
}

auto handle_btn_drive(int drive_idx, int mod) -> void {
  if ((mod & KMOD_CTRL) != 0) {
    if ((mod & KMOD_SHIFT) != 0) {
      printf("HDD  Eject Drive #%d\n", drive_idx + 1);
      HarddiskEjectCmd_t ecmd{static_cast<uint8_t>(drive_idx)};
      peripheral_command(harddisk_default_slot, harddisk_cmd_eject, &ecmd,
                         sizeof(ecmd));
    } else {
      printf("Disk Eject Drive #%d\n", drive_idx + 1);
      DiskEjectCmd_t ecmd{};
      ecmd.drive = static_cast<uint8_t>(drive_idx);
      if (peripheral_command(disk_default_slot, disk_cmd_eject, &ecmd,
                             sizeof(ecmd)) == peripheral_ok) {
        app_controller_save_disk_config(drive_idx);
      }
    }
    return;
  }

  if ((mod & KMOD_SHIFT) != 0) {
    if ((mod & KMOD_ALT) != 0) {
      harddisk_ui_ftp_select(drive_idx);
    } else {
      harddisk_ui_select(drive_idx);
    }
  } else {
    if ((mod & KMOD_ALT) != 0) {
      disk_ftp_select_image(drive_idx);
    } else {
      disk_select(drive_idx);
    }
  }
}

auto handle_btn_fullscreen(int mod) -> void {
  if ((mod & KMOD_SHIFT) != 0) {
    if ((current_language != A2LANG_US) &&
        ((current_apple2_type == A2TYPE_APPLE2E) ||
         (current_apple2_type == A2TYPE_APPLE2EENHANCED))) {
      uint8_t cur_rocker = 0;
      size_t rocker_sz = sizeof(cur_rocker);
      peripheral_query_by_id(0, "linapple.keyboard", keyboard_query_rocker,
                             &cur_rocker, &rocker_sz);
      const uint8_t new_rocker = (cur_rocker != 0) ? 0 : 1;
      peripheral_command_by_id(0, "linapple.keyboard", keyboard_cmd_set_rocker,
                               &new_rocker, 1);
      printf("Toggling keyboard rocker switch. Selected character set: %s...\n",
             (new_rocker != 0) ? "local" : "standard/US");
    }
  } else {
    if (system_state.fullscreen) {
      system_state.fullscreen = false;
      set_normal_mode();
    } else {
      system_state.fullscreen = true;
      set_fullscreen_mode();
    }
  }
}

auto handle_btn_setup(int mod) -> void {
  if ((mod & KMOD_SHIFT) != 0) {
    Configuration_t::instance().set_int("Configuration", "Video Emulation",
                                        static_cast<int>(g_videotype));
    Configuration_t::instance().set_int("Configuration", "Emulation Speed",
                                        static_cast<int>(system_state.speed));
    Configuration_t::instance().set_int("Configuration", "Fullscreen",
                                        system_state.fullscreen ? 1 : 0);
    Configuration_t::instance().save();
  } else {
    frame_save_bmp();
  }
}

auto handle_btn_cycle(int mod) -> void {
  if ((mod & KMOD_SHIFT) != 0) {
    set_budget_video(!get_budget_video());
  } else {
    ++g_videotype;
    if (g_videotype >= VT_NUM_MODES) {
      g_videotype = 0;
    }
    video_reinitialize();
    if (system_state.mode != app_mode_logo) {
#if ENABLE_DEBUGGER
      if (system_state.mode == app_mode_debug) {
        uint32_t debug_video_mode = 0;
        if (debug_get_video_mode(&debug_video_mode)) {
          video_redraw_screen();
        }
      } else {
        video_redraw_screen();
      }
#else
      video_redraw_screen();
#endif
    }
  }
}

}  // namespace

auto draw_apple_content() -> void {
  const std::lock_guard<std::recursive_mutex> lock(g_video_draw_mutex);
  video_realize_palette();

  draw_status_area(draw_background | draw_leds);

  if (system_state.mode == app_mode_logo) {
    video_display_logo();
    video_set_frame_ready(true);
  } else if (system_state.mode == app_mode_debug) {
#if ENABLE_DEBUGGER
    debug_display(true);
#endif
    video_set_frame_ready(true);
  } else {
    video_redraw_screen();
  }
}

auto frame_refresh() -> void {
  if (g_screen != nullptr) {
    SDL_Flip(g_screen);
  }
}

auto draw_frame_window() -> void {
  if (!g_frame_ready) {
    return;
  }

  const std::lock_guard<std::recursive_mutex> lock(g_video_draw_mutex);
  if (g_texture == nullptr || g_screen == nullptr) {
    return;
  }

  const SDL_Rect r{0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
  VideoSurface_t vs_texture = sdl_surface_to_video_surface(g_texture.get());
  VideoRect_t src_rect =
      g_window_resized ? to_video_rect(g_orig_rect) : to_video_rect(r);
  VideoRect_t dst_rect =
      g_window_resized ? to_video_rect(g_new_rect) : to_video_rect(r);

  if (system_state.mode != app_mode_debug) {
    uint32_t* output = video_get_output_buffer();
    if (output == nullptr) {
      return;
    }
    VideoSurface_t vs_output{};
    vs_output.pixels = reinterpret_cast<uint8_t*>(output);
    vs_output.w = SCREEN_WIDTH;
    vs_output.h = SCREEN_HEIGHT;
    vs_output.pitch = SCREEN_WIDTH * 4;
    vs_output.bpp = 4;
    video_soft_stretch(&vs_output, &src_rect, &vs_texture, &dst_rect);
  } else {
#if ENABLE_DEBUGGER
    extern VideoSurface_t* g_debug_screen;
    if (g_debug_screen != nullptr) {
      video_soft_stretch(g_debug_screen, &src_rect, &vs_texture, &dst_rect);
    }
#endif
  }

  SDL_BlitSurface(g_texture.get(), nullptr, g_screen, nullptr);
  frame_refresh();
  video_set_frame_ready(false);
}

auto draw_status_area(int drawflags) -> void {
  if (g_status_surface == nullptr || g_status_surface->pixels == nullptr) {
    return;
  }
  if (font_sfc == nullptr && !fonts_initialization()) {
    fprintf(stderr, "Font file was not loaded.\n");
    return;
  }

  if ((drawflags & draw_background) != 0) {
    g_status_cycle = k_show_cycles;
  }

  if ((drawflags & draw_leds) != 0) {
    VideoRect_t srect{};
    const uint8_t mybluez = DARK_BLUE;
    srect.x = 4;
    srect.y = 22;
    srect.w = static_cast<int16_t>(STATUS_PANEL_W - 8);
    srect.h = static_cast<int16_t>(STATUS_PANEL_H - 25);

    if (srect.x >= 0 && srect.y >= 0 &&
        (srect.x + srect.w) <= g_status_surface->w &&
        (srect.y + srect.h) <= g_status_surface->h) {
      for (int y = srect.y; y < srect.y + srect.h; ++y) {
        memset(g_status_surface->pixels +
                   static_cast<ptrdiff_t>(y * g_status_surface->pitch) +
                   srect.x,
               mybluez, static_cast<size_t>(srect.w));
      }
    }

    std::array<char, 2> leds = {{"\x64"}};
    constexpr int led_char_base = 1;
    int drive1_status = disk_status_off;
    int drive2_status = disk_status_off;
    int hdd_status = disk_status_off;

    if (s_last_disk_status.drive0_spinning != 0) {
      drive1_status = (s_last_disk_status.drive0_writing != 0)
                          ? disk_status_write
                          : disk_status_read;
    } else if (s_last_disk_status.drive0_loaded != 0 &&
               s_last_disk_status.drive0_write_protected != 0) {
      drive1_status = disk_status_prot;
    }

    if (s_last_disk_status.drive1_spinning != 0) {
      drive2_status = (s_last_disk_status.drive1_writing != 0)
                          ? disk_status_write
                          : disk_status_read;
    } else if (s_last_disk_status.drive1_loaded != 0 &&
               s_last_disk_status.drive1_write_protected != 0) {
      drive2_status = disk_status_prot;
    }

    HarddiskStatus_t hstatus{};
    size_t hsize = sizeof(hstatus);
    if (peripheral_query(harddisk_default_slot, harddisk_query_status, &hstatus,
                         &hsize) == peripheral_ok) {
      hdd_status = hstatus.activity_status;
    }

    leds.at(0) = static_cast<char>(led_char_base + drive1_status);
    font_print(8, 23, leds.data(), g_status_surface, 4.0f, 2.7f);

    leds.at(0) = static_cast<char>(led_char_base + drive2_status);
    font_print(40, 23, leds.data(), g_status_surface, 4.0f, 2.7f);

    leds.at(0) = static_cast<char>(led_char_base + hdd_status);
    font_print(71, 23, leds.data(), g_status_surface, 4.0f, 2.7f);

    if ((drive1_status | drive2_status | hdd_status) != 0) {
      g_status_cycle = k_show_cycles;
    }
  }
}

auto frame_show_help_screen(int sx, int sy) -> void {
  (void)sy;
  if (g_screen == nullptr) {
    return;
  }
  if (font_sfc == nullptr && !fonts_initialization()) {
    fprintf(stderr, "Font file was not loaded.\n");
    return;
  }

  VideoSurface_t* temp_surface = nullptr;
  if (!g_window_resized) {
    temp_surface =
        (system_state.mode == app_mode_logo) ? g_logo_bitmap : g_device_bitmap;
  } else {
    temp_surface = g_origscreen;
  }

  VideoSurface_t vs_screen_fallback{};
  if (temp_surface == nullptr) {
    vs_screen_fallback = sdl_surface_to_video_surface(g_screen);
    temp_surface = &vs_screen_fallback;
  }

  VideoSurface_t vs_actual_g_screen = sdl_surface_to_video_surface(g_screen);
  video_soft_stretch(temp_surface, nullptr, &vs_actual_g_screen, nullptr);

  const int blur_w = std::max(1, g_screen->w / 16);
  const int blur_h = std::max(1, g_screen->h / 16);
  SdlSurfacePtr_t blur_temp(SDL_CreateRGBSurface(
      0, blur_w, blur_h, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000));
  if (blur_temp != nullptr) {
    VideoSurface_t vs_blur = sdl_surface_to_video_surface(blur_temp.get());
    video_soft_stretch(&vs_actual_g_screen, nullptr, &vs_blur, nullptr);
    video_soft_stretch(&vs_blur, nullptr, &vs_actual_g_screen, nullptr);
  }

  SdlSurfacePtr_t dim_surface(SDL_CreateRGBSurface(0, g_screen->w, g_screen->h,
                                                   32, 0x00FF0000, 0x0000FF00,
                                                   0x000000FF, 0xFF000000));
  if (dim_surface != nullptr) {
    const Uint32 dim_color = SDL_MapRGBA(dim_surface->format, 0, 0, 0, 200);
    SDL_FillRect(dim_surface.get(), nullptr, dim_color);
    SDL_SetAlpha(dim_surface.get(), SDL_SRCALPHA, 200);
    SDL_BlitSurface(dim_surface.get(), nullptr, g_screen, nullptr);
  }

  const float facx_f = static_cast<float>(system_state.screen_width) /
                       static_cast<float>(SCREEN_WIDTH);
  const float facy_f = static_cast<float>(system_state.screen_height) /
                       static_cast<float>(SCREEN_HEIGHT);
  const float scale_x = facx_f;
  const float scale_y = facy_f;

  const int hdr_top = static_cast<int>(4.0f * facy_f);
  const int hdr_height = static_cast<int>(42.0f * facy_f);
  rectangle(&vs_actual_g_screen, static_cast<int>(4.0f * facx_f), hdr_top,
            static_cast<int>(system_state.screen_width - (8.0f * facx_f)),
            hdr_height, 0xFFFF00);

  font_print_centered(sx / 2, hdr_top + static_cast<int>(4.0f * facy_f),
                      HELP_HEADER_STRINGS.at(0), &vs_actual_g_screen, scale_x,
                      scale_y);
  font_print_centered(sx / 2, hdr_top + static_cast<int>(16.0f * facy_f),
                      HELP_HEADER_STRINGS.at(1), &vs_actual_g_screen, scale_x,
                      scale_y);
  font_print_centered(sx / 2, hdr_top + static_cast<int>(28.0f * facy_f),
                      HELP_HEADER_STRINGS.at(2), &vs_actual_g_screen, scale_x,
                      scale_y);

  const int body_top = hdr_top + hdr_height + static_cast<int>(4.0f * facy_f);
  const int body_height =
      static_cast<int>(system_state.screen_height - body_top - (4.0f * facy_f));
  rectangle(&vs_actual_g_screen, static_cast<int>(4.0f * facx_f), body_top,
            static_cast<int>(system_state.screen_width - (8.0f * facx_f)),
            body_height, 0xFFFFFF);

  const float line_spacing = 13.0f * facy_f;
  for (size_t i = 0; i < HELP_BODY_LINES.size(); ++i) {
    if (HELP_BODY_LINES.at(i).text != nullptr &&
        HELP_BODY_LINES.at(i).text[0] != '\0') {
      font_print(
          static_cast<int>(16.0f * facx_f),
          body_top + static_cast<int>(6.0f * facy_f +
                                      static_cast<float>(i) * line_spacing),
          HELP_BODY_LINES.at(i).text, &vs_actual_g_screen, scale_x, scale_y);
    }
  }

  if (assets != nullptr && assets->icon != nullptr) {
    VideoSurface_t vs_icon =
        sdl_surface_to_video_surface(static_cast<SDL_Surface*>(assets->icon));
    VideoRect_t logo{0, 0, static_cast<int16_t>(vs_icon.w),
                     static_cast<int16_t>(vs_icon.h)};
    VideoRect_t scrr{static_cast<int16_t>(460.0f * facx_f),
                     static_cast<int16_t>(270.0f * facy_f),
                     static_cast<int16_t>(100.0f * facy_f),
                     static_cast<int16_t>(100.0f * facy_f)};
    video_soft_stretch_or(&vs_icon, &logo, &vs_actual_g_screen, &scrr);
  }

  frame_refresh();

  SDL_Event event{};
  bool waiting = true;
  while (waiting) {
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_F12) {
          system_state.mode = app_mode_exit;
          SDL_Event qe{};
          qe.type = SDL_QUIT;
          SDL_PushEvent(&qe);
        }
        waiting = false;
        break;
      }
      if (event.type == SDL_QUIT) {
        SDL_PushEvent(&event);
        system_state.mode = app_mode_exit;
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
  video_set_frame_ready(true);
  draw_frame_window();
}

auto frame_quick_state(int num, int mod) -> void {
  std::array<char, path_max_len> fpath{};
  snprintf(fpath.data(), fpath.size(), "%.*s/SaveState%d.aws",
           static_cast<int>(strlen(system_state.save_state_dir.data())),
           system_state.save_state_dir.data(), num);
  save_state_set_filename(fpath.data());
  if ((mod & KMOD_SHIFT) != 0) {
    save_state_save();
  } else {
    save_state_load();
  }
}

auto is_modifier_key(SDLKey sym) noexcept -> bool {
  return sym == SDLK_NUMLOCK || sym == SDLK_CAPSLOCK || sym == SDLK_SCROLLOCK ||
         sym == SDLK_RSHIFT || sym == SDLK_LSHIFT || sym == SDLK_RCTRL ||
         sym == SDLK_LCTRL || sym == SDLK_RALT || sym == SDLK_LALT ||
         sym == SDLK_RMETA || sym == SDLK_LMETA || sym == SDLK_RSUPER ||
         sym == SDLK_LSUPER || sym == SDLK_MODE || sym == SDLK_COMPOSE;
}

auto frame_on_resize(int width, int height) -> void {
  if (width <= 0 || height <= 0) {
    return;
  }

  const std::lock_guard<std::recursive_mutex> lock(g_video_draw_mutex);
  system_state.screen_width = static_cast<uint32_t>(width);
  system_state.screen_height = static_cast<uint32_t>(height);

  if (!s_is_fullscreen) {
    s_windowed_width = static_cast<uint32_t>(width);
    s_windowed_height = static_cast<uint32_t>(height);
  }

  Uint32 flags = SDL_SWSURFACE;
  if (system_state.fullscreen) {
    flags |= SDL_FULLSCREEN;
  }

  g_screen = SDL_SetVideoMode(width, height, 32, flags);
  if (g_screen == nullptr) {
    fprintf(stderr, "Could not resize video mode: %s\n", SDL_GetError());
    return;
  }

  g_texture.reset(SDL_CreateRGBSurface(0, system_state.screen_width,
                                       system_state.screen_height, 32,
                                       0x00FF0000, 0x0000FF00, 0x000000FF, 0));

  g_window_resized = (system_state.screen_width != SCREEN_WIDTH) ||
                     (system_state.screen_height != SCREEN_HEIGHT);
  if (g_window_resized) {
    g_orig_rect.x = g_orig_rect.y = 0;
    g_orig_rect.w = static_cast<int16_t>(SCREEN_WIDTH);
    g_orig_rect.h = static_cast<int16_t>(SCREEN_HEIGHT);
    if (s_is_fullscreen) {
      int target_w = width;
      int target_h = (target_w * SCREEN_HEIGHT) / SCREEN_WIDTH;
      if (target_h > height) {
        target_h = height;
        target_w = (target_h * SCREEN_WIDTH) / SCREEN_HEIGHT;
      }
      const int offset_x = (width - target_w) / 2;
      const int offset_y = (height - target_h) / 2;
      g_new_rect.x = static_cast<int16_t>(offset_x);
      g_new_rect.y = static_cast<int16_t>(offset_y);
      g_new_rect.w = static_cast<int16_t>(target_w);
      g_new_rect.h = static_cast<int16_t>(target_h);
    } else {
      g_new_rect.x = 0;
      g_new_rect.y = 0;
      g_new_rect.w = static_cast<int16_t>(system_state.screen_width);
      g_new_rect.h = static_cast<int16_t>(system_state.screen_height);
    }
  }

  draw_apple_content();
}

auto frame_on_focus(bool gained) -> void {
  s_app_active = gained;
  if (s_app_active) {
    if ((system_state.mode != app_mode_debug) &&
        (system_state.mode != app_mode_stepping) &&
        (system_state.mode != app_mode_paused)) {
      audio_mixer_set_fade(fade_in);
    }
  } else {
    audio_mixer_set_fade(fade_out);
  }
}

auto frame_on_expose() -> void { draw_apple_content(); }

auto psp_save_state_select_image(bool saveit) -> bool {
  static size_t s_file_index = 0;
  static int s_backdx = 0;
  static int s_dirdx = 0;

  std::string filename;
  bool isdir = false;
  const int slot = saveit ? 1 : 0;
  std::string dir = system_state.save_state_dir.data();

  const bool result = choose_an_image(s_backdx, s_dirdx, dir, slot, filename,
                                      isdir, s_file_index);

  if (result) {
    std::string full_path = dir + "/" + filename;
    save_state_set_filename(full_path.c_str());
    return true;
  }
  return false;
}

auto process_button_click(int button, int mod) -> void {
  audio_mixer_set_fade(fade_out);

  switch (button) {
    case k_btn_help:
      if (g_screen != nullptr) {
        frame_show_help_screen(g_screen->w, g_screen->h);
      }
      break;

    case k_btn_run:
      handle_btn_run(mod);
      break;

    case k_btn_drive1:
    case k_btn_drive2:
      handle_btn_drive(button - k_btn_drive1, mod);
      break;

    case k_btn_driveswap:
      if (peripheral_command(disk_default_slot, disk_cmd_swap_drives, nullptr,
                             0) == peripheral_ok) {
        app_controller_save_disk_config(0);
        app_controller_save_disk_config(1);
      }
      break;

    case k_btn_fullscr:
      handle_btn_fullscreen(mod);
      break;

    case k_btn_debug:
#if ENABLE_DEBUGGER
      if (!system_state.disable_debugger) {
        if (system_state.mode != app_mode_debug) {
          debug_begin();
          set_using_cursor(false);
        } else {
          debug_end();
        }
      }
#endif
      break;

    case k_btn_setup:
      handle_btn_setup(mod);
      break;

    case k_btn_cycle:
      handle_btn_cycle(mod);
      break;

    case k_btn_quit: {
      SDL_Event qe{};
      qe.type = SDL_QUIT;
      SDL_PushEvent(&qe);
      break;
    }

    case k_btn_savest:
      if ((mod & KMOD_ALT) != 0 || psp_save_state_select_image(true)) {
        save_state_save();
      }
      break;

    case k_btn_loadst:
      if ((mod & KMOD_CTRL) != 0) {
        linapple_reset_soft();
      } else if ((mod & KMOD_ALT) != 0 || psp_save_state_select_image(false)) {
        save_state_load();
      }
      break;

    default:
      break;
  }

  if (system_state.mode != app_mode_debug &&
      system_state.mode != app_mode_paused) {
    audio_mixer_set_fade(fade_in);
  }
}

auto set_fullscreen_mode() -> void {
  if (!s_is_fullscreen) {
    s_is_fullscreen = true;
    if (s_windowed_width == 0 || s_windowed_height == 0) {
      s_windowed_width = system_state.screen_width;
      s_windowed_height = system_state.screen_height;
    }
    SDL_WM_ToggleFullScreen(g_screen);
    if (system_state.mode != app_mode_debug) {
      SDL_ShowCursor(SDL_DISABLE);
    }
  }
}

auto set_normal_mode() -> void {
  if (s_is_fullscreen) {
    s_is_fullscreen = false;
    SDL_WM_ToggleFullScreen(g_screen);
    if (s_windowed_width > 0 && s_windowed_height > 0) {
      frame_on_resize(static_cast<int>(s_windowed_width),
                      static_cast<int>(s_windowed_height));
    }
    if (!g_usingcursor) {
      SDL_ShowCursor(SDL_ENABLE);
    }
  } else if (system_state.mode == app_mode_debug) {
    SDL_ShowCursor(SDL_ENABLE);
    SDL_WM_GrabInput(SDL_GRAB_OFF);
  }
}

auto set_using_cursor(bool enable) -> void {
  g_usingcursor = enable;
  if (g_usingcursor) {
    SDL_ShowCursor(SDL_DISABLE);
    SDL_WM_GrabInput(SDL_GRAB_ON);
  } else {
    if (!s_is_fullscreen || (system_state.mode == app_mode_debug)) {
      SDL_ShowCursor(SDL_ENABLE);
    }
    SDL_WM_GrabInput(SDL_GRAB_OFF);
  }
}

auto frame_create_window() -> int {
  sdl_asset_load_icon();
  s_is_fullscreen = false;
  if (!system_state.fullscreen) {
    s_windowed_width = system_state.screen_width;
    s_windowed_height = system_state.screen_height;
  }

  Uint32 flags = SDL_SWSURFACE;
  if (system_state.fullscreen) {
    flags |= SDL_FULLSCREEN;
  }

  g_screen =
      SDL_SetVideoMode(static_cast<int>(system_state.screen_width),
                       static_cast<int>(system_state.screen_height), 32, flags);
  if (g_screen == nullptr) {
    fprintf(stderr, "Could not set video mode: %s\n", SDL_GetError());
    return 1;
  }

  g_texture.reset(SDL_CreateRGBSurface(0, system_state.screen_width,
                                       system_state.screen_height, 32,
                                       0x00FF0000, 0x0000FF00, 0x000000FF, 0));
  if (g_texture == nullptr) {
    fprintf(stderr, "Could not create frame texture: %s\n", SDL_GetError());
    return 1;
  }

  SDL_WM_SetCaption(app_title, app_title);
  set_icon();

  g_window_resized = (system_state.screen_width != SCREEN_WIDTH) ||
                     (system_state.screen_height != SCREEN_HEIGHT);
  printf("Screen size is %ux%u\n", system_state.screen_width,
         system_state.screen_height);
  if (g_window_resized) {
    g_orig_rect.x = g_orig_rect.y = g_new_rect.x = g_new_rect.y = 0;
    g_orig_rect.w = static_cast<int16_t>(SCREEN_WIDTH);
    g_orig_rect.h = static_cast<int16_t>(SCREEN_HEIGHT);
    g_new_rect.w = static_cast<int16_t>(system_state.screen_width);
    g_new_rect.h = static_cast<int16_t>(system_state.screen_height);
  }
  return 0;
}

auto frame_destroy_window() -> void {
  g_texture.reset();
  g_screen = nullptr;
  sdl_asset_free_icon();
}

auto init_sdl() -> int {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
    fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
    return 1;
  }
  return 0;
}

auto frame_refresh_status(int drawflags) -> void {
  if ((drawflags & draw_leds) != 0) {
    size_t size = sizeof(s_last_disk_status);
    if (peripheral_query(disk_default_slot, disk_query_status,
                         &s_last_disk_status, &size) == peripheral_ok) {
      if (s_last_disk_status.drive0_last_error != disk_err_none &&
          s_last_disk_status.drive0_last_error !=
              s_drive0_last_reported_error) {
        fprintf(
            stderr, "Disk 1 error: %s\n",
            disk_ui_get_error_message(s_last_disk_status.drive0_last_error));
        s_drive0_last_reported_error = s_last_disk_status.drive0_last_error;
      } else if (s_last_disk_status.drive0_last_error == disk_err_none) {
        s_drive0_last_reported_error = disk_err_none;
      }

      if (s_last_disk_status.drive1_last_error != disk_err_none &&
          s_last_disk_status.drive1_last_error !=
              s_drive1_last_reported_error) {
        fprintf(
            stderr, "Disk 2 error: %s\n",
            disk_ui_get_error_message(s_last_disk_status.drive1_last_error));
        s_drive1_last_reported_error = s_last_disk_status.drive1_last_error;
      } else if (s_last_disk_status.drive1_last_error == disk_err_none) {
        s_drive1_last_reported_error = disk_err_none;
      }

      std::array<char, 512> title_buf{};
      if (s_last_disk_status.drive0_loaded != 0) {
        std::array<char, k_disk_ui_display_name_max + 1> display_name{};
        disk_ui_format_display_name(s_last_disk_status.drive0_name,
                                    display_name.data(), display_name.size());
        snprintf(title_buf.data(), title_buf.size(), "%s - %s", app_title,
                 display_name.data());
      } else {
        snprintf(title_buf.data(), title_buf.size(), "%s", app_title);
      }
      linapple_update_title(title_buf.data());
    }
  }
  draw_status_area(drawflags);
}
