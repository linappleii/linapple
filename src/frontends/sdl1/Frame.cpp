// SPDX-License-Identifier: GPL-2.0-only

#include "frontends/sdl1/Frame.h"

#include <SDL/SDL.h>
#include <SDL/SDL_error.h>
#include <SDL/SDL_events.h>
#include <SDL/SDL_keyboard.h>
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
#include "core/Asset.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AudioMixer.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/HarddiskFrontend.h"
#include "frontends/common/HelpText.h"
#include "frontends/common/MouseFrontend.h"
#include "frontends/common/SaveStateManager.h"
#include "frontends/common/VideoStretch.h"
#include "frontends/common/VideoSurface.h"
#include "frontends/common/sdl/DiskUI.h"
#include "frontends/common/sdl/MouseInput.h"
#include "frontends/sdl1/DiskChoose.h"
#include "frontends/sdl1/SDL_Asset.h"
#include "frontends/sdl1/SDL_Video.h"
#include "frontends/sdl1/SdlPtr.h"

SDL_Surface* g_screen = nullptr;
SdlSurfacePtr g_texture;
SDL_Rect g_orig_rect{};
SDL_Rect g_new_rect{};

int g_buttondown = -1;
bool g_window_resized = false;

namespace {

static bool s_app_active = false;
static DiskStatus_t s_last_disk_status{};
static int s_drive0_last_reported_error = disk_err_none;
static int s_drive1_last_reported_error = disk_err_none;
static bool s_is_fullscreen = false;
static uint32_t s_windowed_width = 0;
static uint32_t s_windowed_height = 0;

auto to_video_rect(const SDL_Rect& r) noexcept -> VideoRect {
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
      harddisk_frontend_eject(drive_idx);
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
      const bool local = !linapple_get_rocker_switch();
      linapple_set_rocker_switch(local);
      printf("Toggling keyboard rocker switch. Selected character set: %s...\n",
             local ? "local" : "standard/US");
    }
    return;
  }

  system_state.fullscreen = !system_state.fullscreen;
  if (system_state.fullscreen) {
    set_fullscreen_mode();
    return;
  }
  set_normal_mode();
}

auto handle_btn_setup(int mod) -> void {
  if ((mod & KMOD_SHIFT) != 0) {
    Configuration::instance().set_int("Configuration", "Video Emulation",
                                      static_cast<int>(g_videotype));
    Configuration::instance().set_int("Configuration", "Emulation Speed",
                                      static_cast<int>(system_state.speed));
    Configuration::instance().set_int("Configuration", "Fullscreen",
                                      system_state.fullscreen ? 1 : 0);
    Configuration::instance().save();
  } else {
    frame_save_bmp();
  }
}

auto handle_btn_cycle(int mod) -> void {
  if ((mod & KMOD_SHIFT) != 0) {
    set_budget_video(!get_budget_video());
    return;
  }

  g_videotype = (g_videotype + 1) % VT_NUM_MODES;
  video_reinitialize();

  if (system_state.mode == app_mode_logo) {
    return;
  }

#if ENABLE_DEBUGGER
  if (system_state.mode == app_mode_debug) {
    uint32_t debug_video_mode = 0;
    if (debug_get_video_mode(&debug_video_mode)) {
      video_redraw_screen();
    }
    return;
  }
#endif

  video_redraw_screen();
}

auto compute_aspect_fit_rect(int width, int height) noexcept -> SDL_Rect {
  int target_w = width;
  int target_h = (target_w * SCREEN_HEIGHT) / SCREEN_WIDTH;
  if (target_h > height) {
    target_h = height;
    target_w = (target_h * SCREEN_WIDTH) / SCREEN_HEIGHT;
  }
  const int offset_x = (width - target_w) / 2;
  const int offset_y = (height - target_h) / 2;
  return SDL_Rect{
      static_cast<int16_t>(offset_x), static_cast<int16_t>(offset_y),
      static_cast<uint16_t>(target_w), static_cast<uint16_t>(target_h)};
}

// A load can take the mouse card away; a captured pointer would feed nothing.
auto load_save_state() -> void {
  save_state_load();
  if (!mouse_input_consumer_present()) {
    mouse_input_release();
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
  frame_poll_activity();
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

  {
    const SDL_Rect r{0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    ScopedSurfaceLock lock_texture(g_texture.get());
    VideoRect src_rect =
        g_window_resized ? to_video_rect(g_orig_rect) : to_video_rect(r);
    VideoRect dst_rect =
        g_window_resized ? to_video_rect(g_new_rect) : to_video_rect(r);

    if (system_state.mode != app_mode_debug) {
      uint32_t* output = video_get_output_buffer();
      if (output == nullptr) {
        return;
      }
      const VideoSurfaceView vs_output(output, SCREEN_WIDTH, SCREEN_HEIGHT,
                                       SCREEN_WIDTH * 4, 4);
      video_soft_stretch(vs_output, &src_rect, lock_texture.view(), &dst_rect);
    } else {
#if ENABLE_DEBUGGER
      extern VideoSurface* g_debug_screen;
      if (g_debug_screen != nullptr) {
        video_soft_stretch(g_debug_screen, &src_rect, lock_texture.view(),
                           &dst_rect);
      }
#endif
    }
  }

  SDL_BlitSurface(g_texture.get(), nullptr, g_screen, nullptr);
  frame_refresh();
  video_set_frame_ready(false);
}

// The glyphs as last composed, so what the panel shows can be read back, and
// an activity mark the poll took before the repaint it asked for.
static std::array<char, 3> s_last_leds = {{1, 1, 1}};
static bool s_harddisk_activity_seen = false;

auto draw_status_area(int drawflags) -> void {
  if (g_status_surface == nullptr || g_status_surface->pixels == nullptr) {
    return;
  }
  if (font_sfc == nullptr && !fonts_initialization()) {
    fprintf(stderr, "Font file was not loaded.\n");
    return;
  }

  if ((drawflags & draw_background) != 0) {
    g_status_cycle = show_cycles;
  }

  if ((drawflags & draw_leds) != 0) {
    VideoRect srect{};
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

    const int hd_slot = harddisk_frontend_slot();
    HarddiskStatus_t hstatus{};
    size_t hsize = sizeof(hstatus);
    if (hd_slot != harddisk_frontend_no_card &&
        peripheral_query(hd_slot, harddisk_query_status, &hstatus, &hsize) ==
            peripheral_ok) {
      // The card reports a transfer still in flight; the manager remembers
      // one that began and ended inside the frame.
      const bool seen =
          s_harddisk_activity_seen || peripheral_activity_poll(hd_slot);
      s_harddisk_activity_seen = false;
      if (seen || hstatus.activity_status != harddisk_status_off) {
        hdd_status = (hstatus.activity_status == harddisk_status_write)
                         ? harddisk_status_write
                         : harddisk_status_read;
      } else if ((hstatus.drive0_loaded != 0 &&
                  hstatus.drive0_write_protected != 0) ||
                 (hstatus.drive1_loaded != 0 &&
                  hstatus.drive1_write_protected != 0)) {
        hdd_status = harddisk_status_prot;
      }
    }

    s_last_leds = {{static_cast<char>(led_char_base + drive1_status),
                    static_cast<char>(led_char_base + drive2_status),
                    static_cast<char>(led_char_base + hdd_status)}};

    leds.at(0) = s_last_leds.at(0);
    font_print(8, 23, leds.data(), g_status_surface, 4.0f, 2.7f);

    leds.at(0) = s_last_leds.at(1);
    font_print(40, 23, leds.data(), g_status_surface, 4.0f, 2.7f);

    leds.at(0) = s_last_leds.at(2);
    font_print(71, 23, leds.data(), g_status_surface, 4.0f, 2.7f);

    if ((drive1_status | drive2_status | hdd_status) != 0) {
      g_status_cycle = show_cycles;
    }
  }
}

auto frame_status_led(int index) -> char {
  if (index < 0 || static_cast<size_t>(index) >= s_last_leds.size()) {
    return 0;
  }
  return s_last_leds.at(static_cast<size_t>(index));
}

// No frontend repaints the panel each frame, so a transfer that began and
// ended between two repaints would never light the lamp; one repaint per
// frame of activity, and one more when the panel's hold runs out, so that a
// lamp left lit goes dark.
auto frame_poll_activity() -> void {
  const int hd_slot = harddisk_frontend_slot();
  if (hd_slot == harddisk_frontend_no_card) {
    return;
  }
  if (peripheral_activity_poll(hd_slot)) {
    s_harddisk_activity_seen = true;
    frame_refresh_status(draw_leds);
    return;
  }
  constexpr int led_char_base = 1;
  const char lamp = s_last_leds.at(2);
  const bool lit = lamp == led_char_base + harddisk_status_read ||
                   lamp == led_char_base + harddisk_status_write;
  if (lit && g_status_cycle == 0) {
    frame_refresh_status(draw_leds);
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

  VideoSurface* temp_surface = nullptr;
  if (!g_window_resized) {
    temp_surface =
        (system_state.mode == app_mode_logo) ? g_logo_bitmap : g_device_bitmap;
  } else {
    temp_surface = g_origscreen;
  }

  {
    ScopedSurfaceLock lock_screen(g_screen);
    const VideoSurfaceView view_temp =
        (temp_surface != nullptr) ? *temp_surface : lock_screen.view();
    video_soft_stretch(view_temp, nullptr, lock_screen.view(), nullptr);

    const int blur_w = std::max(1, g_screen->w / 16);
    const int blur_h = std::max(1, g_screen->h / 16);
    SdlSurfacePtr blur_temp(SDL_CreateRGBSurface(
        0, blur_w, blur_h, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000));
    if (blur_temp != nullptr) {
      ScopedSurfaceLock lock_blur(blur_temp.get());
      video_soft_stretch(lock_screen.view(), nullptr, lock_blur.view(),
                         nullptr);
      video_soft_stretch(lock_blur.view(), nullptr, lock_screen.view(),
                         nullptr);
    }
  }

  SdlSurfacePtr dim_surface(SDL_CreateRGBSurface(0, g_screen->w, g_screen->h,
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

  {
    ScopedSurfaceLock lock_screen(g_screen);
    rectangle(lock_screen.view(), static_cast<int>(4.0f * facx_f), hdr_top,
              static_cast<int>(system_state.screen_width - (8.0f * facx_f)),
              hdr_height, 0xFFFF00);

    font_print_centered(sx / 2, hdr_top + static_cast<int>(4.0f * facy_f),
                        help_header_strings.at(0), lock_screen.view(), scale_x,
                        scale_y);
    font_print_centered(sx / 2, hdr_top + static_cast<int>(16.0f * facy_f),
                        help_header_strings.at(1), lock_screen.view(), scale_x,
                        scale_y);
    font_print_centered(sx / 2, hdr_top + static_cast<int>(28.0f * facy_f),
                        help_header_strings.at(2), lock_screen.view(), scale_x,
                        scale_y);

    const int body_top = hdr_top + hdr_height + static_cast<int>(4.0f * facy_f);
    const int body_height = static_cast<int>(system_state.screen_height -
                                             body_top - (4.0f * facy_f));
    rectangle(lock_screen.view(), static_cast<int>(4.0f * facx_f), body_top,
              static_cast<int>(system_state.screen_width - (8.0f * facx_f)),
              body_height, 0xFFFFFF);

    const float line_spacing = 13.0f * facy_f;
    for (size_t i = 0; i < help_body_lines.size(); ++i) {
      if (help_body_lines.at(i).text != nullptr &&
          help_body_lines.at(i).text[0] != '\0') {
        font_print(
            static_cast<int>(16.0f * facx_f),
            body_top + static_cast<int>(6.0f * facy_f +
                                        static_cast<float>(i) * line_spacing),
            help_body_lines.at(i).text, lock_screen.view(), scale_x, scale_y);
      }
    }

    if (assets != nullptr && assets->icon != nullptr) {
      ScopedSurfaceLock lock_icon(static_cast<SDL_Surface*>(assets->icon));
      VideoRect logo{0, 0, static_cast<int16_t>(lock_icon.view().w),
                     static_cast<int16_t>(lock_icon.view().h)};
      VideoRect scrr{static_cast<int16_t>(460.0f * facx_f),
                     static_cast<int16_t>(270.0f * facy_f),
                     static_cast<int16_t>(100.0f * facy_f),
                     static_cast<int16_t>(100.0f * facy_f)};
      video_soft_stretch_or(lock_icon.view(), &logo, lock_screen.view(), &scrr);
    }
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
    load_save_state();
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
  if (!g_window_resized) {
    draw_apple_content();
    return;
  }

  g_orig_rect = SDL_Rect{0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
  g_new_rect =
      s_is_fullscreen
          ? compute_aspect_fit_rect(width, height)
          : SDL_Rect{0, 0, static_cast<uint16_t>(system_state.screen_width),
                     static_cast<uint16_t>(system_state.screen_height)};

  draw_apple_content();
}

auto frame_on_focus(bool gained) -> void {
  s_app_active = gained;
  if (!gained) {
    keyboard_release_host_modifiers();
    audio_mixer_set_fade(fade_out);
    return;
  }
  keyboard_sync_host_caps(SDL_GetModState());
  if ((system_state.mode != app_mode_debug) &&
      (system_state.mode != app_mode_stepping) &&
      (system_state.mode != app_mode_paused)) {
    audio_mixer_set_fade(fade_in);
  }
}

auto frame_on_expose() -> void { draw_apple_content(); }

namespace {

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

auto handle_btn_help() -> void {
  if (g_screen != nullptr) {
    frame_show_help_screen(g_screen->w, g_screen->h);
  }
}

auto handle_btn_drive_swap() -> void {
  if (peripheral_command(disk_default_slot, disk_cmd_swap_drives, nullptr, 0) ==
      peripheral_ok) {
    app_controller_save_disk_config(0);
    app_controller_save_disk_config(1);
  }
}

auto handle_btn_debug() -> void {
#if ENABLE_DEBUGGER
  if (system_state.disable_debugger) {
    return;
  }
  if (system_state.mode != app_mode_debug) {
    debug_begin();
    mouse_input_release();
    return;
  }
  debug_end();
#endif
}

auto handle_btn_quit() -> void {
  SDL_Event qe{};
  qe.type = SDL_QUIT;
  SDL_PushEvent(&qe);
}

auto handle_btn_save_state(int mod) -> void {
  if ((mod & KMOD_ALT) != 0 || psp_save_state_select_image(true)) {
    save_state_save();
  }
}

auto handle_btn_load_state(int mod) -> void {
  if ((mod & KMOD_CTRL) != 0) {
    linapple_reset_soft();
    return;
  }
  if ((mod & KMOD_ALT) != 0 || psp_save_state_select_image(false)) {
    load_save_state();
  }
}

}  // namespace

auto process_button_click(int button, int mod) -> void {
  audio_mixer_set_fade(fade_out);

  switch (button) {
    case btn_help:
      handle_btn_help();
      break;

    case btn_run:
      handle_btn_run(mod);
      break;

    case btn_drive1:
    case btn_drive2:
      handle_btn_drive(button - btn_drive1, mod);
      break;

    case btn_driveswap:
      handle_btn_drive_swap();
      break;

    case btn_fullscr:
      handle_btn_fullscreen(mod);
      break;

    case btn_debug:
      handle_btn_debug();
      break;

    case btn_setup:
      handle_btn_setup(mod);
      break;

    case btn_cycle:
      handle_btn_cycle(mod);
      break;

    case btn_quit:
      handle_btn_quit();
      break;

    case btn_savest:
      handle_btn_save_state(mod);
      break;

    case btn_loadst:
      handle_btn_load_state(mod);
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
  if (s_is_fullscreen) {
    return;
  }

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

auto set_normal_mode() -> void {
  if (!s_is_fullscreen) {
    if (system_state.mode != app_mode_debug) {
      return;
    }
    SDL_ShowCursor(SDL_ENABLE);
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    return;
  }

  s_is_fullscreen = false;
  SDL_WM_ToggleFullScreen(g_screen);
  if (s_windowed_width > 0 && s_windowed_height > 0) {
    frame_on_resize(static_cast<int>(s_windowed_width),
                    static_cast<int>(s_windowed_height));
  }
  if (!mouse_input_is_captured()) {
    SDL_ShowCursor(SDL_ENABLE);
  }
}

// SDL 1.2 has no relative mode: a grabbed window with a hidden cursor already
// reports unbounded relative motion, and x and y stay the host pointer's.
auto frame_pointer_capture(bool captured, bool relative) -> void {
  (void)relative;
  SDL_WM_GrabInput(captured ? SDL_GRAB_ON : SDL_GRAB_OFF);

  if (captured) {
    SDL_ShowCursor(SDL_DISABLE);
    return;
  }

  if (!s_is_fullscreen || system_state.mode == app_mode_debug) {
    SDL_ShowCursor(SDL_ENABLE);
  }
}

// g_new_rect is set only on a resize, so until one the picture is the window.
auto frame_picture_rect() -> MousePictureRect {
  if (g_window_resized) {
    return {g_new_rect.x, g_new_rect.y, g_new_rect.w, g_new_rect.h};
  }
  return {0, 0, static_cast<int>(system_state.screen_width),
          static_cast<int>(system_state.screen_height)};
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
    g_orig_rect = SDL_Rect{0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    g_new_rect =
        SDL_Rect{0, 0, static_cast<uint16_t>(system_state.screen_width),
                 static_cast<uint16_t>(system_state.screen_height)};
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

namespace {

auto report_disk_error(const char* title, int current_error,
                       int& last_reported_error) -> void {
  if (current_error == disk_err_none) {
    last_reported_error = disk_err_none;
    return;
  }
  if (current_error == last_reported_error) {
    return;
  }
  std::fprintf(stderr, "%s: %s\n", title,
               disk_ui_get_error_message(current_error));
  last_reported_error = current_error;
}

auto refresh_disk_status() -> void {
  size_t size = sizeof(s_last_disk_status);
  if (peripheral_query(disk_default_slot, disk_query_status,
                       &s_last_disk_status, &size) != peripheral_ok) {
    return;
  }

  report_disk_error("Disk 1 error", s_last_disk_status.drive0_last_error,
                    s_drive0_last_reported_error);
  report_disk_error("Disk 2 error", s_last_disk_status.drive1_last_error,
                    s_drive1_last_reported_error);

  std::array<char, 512> title_buf{};
  if (s_last_disk_status.drive0_loaded != 0) {
    std::array<char, disk_ui_display_name_max + 1> display_name{};
    disk_ui_format_display_name(s_last_disk_status.drive0_name,
                                display_name.data(), display_name.size());
    std::snprintf(title_buf.data(), title_buf.size(), "%s - %s", app_title,
                  display_name.data());
  } else {
    std::snprintf(title_buf.data(), title_buf.size(), "%s", app_title);
  }
  linapple_update_title(title_buf.data());
}

}  // namespace

auto frame_refresh_status(int drawflags) -> void {
  if ((drawflags & draw_leds) != 0) {
    refresh_disk_status();
  }
  draw_status_area(drawflags);
}
