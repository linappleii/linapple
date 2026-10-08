// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/sdl2/Frame.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_blendmode.h>
#include <SDL2/SDL_error.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_messagebox.h>
#include <SDL2/SDL_mouse.h>
#include <SDL2/SDL_pixels.h>
#include <SDL2/SDL_rect.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

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
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AudioMixer.h"
#include "frontends/common/Frontend.h"
#include "frontends/common/HelpText.h"
#include "frontends/common/MouseFrontend.h"
#include "frontends/common/SaveStateManager.h"
#include "frontends/common/VideoStretch.h"
#include "frontends/common/VideoSurface.h"
#include "frontends/common/sdl/DiskUI.h"
#include "frontends/common/sdl/MouseInput.h"
#include "frontends/sdl2/DiskChoose.h"
#include "frontends/sdl2/SDL_Asset.h"
#include "frontends/sdl2/SDL_Video.h"
#include "frontends/sdl2/SdlPtr.h"

#if ENABLE_DEBUGGER
#include "Debugger/Debug.h"
extern VideoSurface* g_debug_screen;
#endif

SdlSurfacePtr g_screen;
SdlWindowPtr g_window;
SdlRendererPtr g_renderer;
SdlTexturePtr g_texture;
SDL_Rect g_orig_rect{};
SDL_Rect g_new_rect{};
int g_buttondown = -1;
bool g_window_resized = false;

namespace {

bool s_app_active = false;
DiskStatus_t s_last_disk_status{};
int s_drive0_last_reported_error = disk_err_none;
int s_drive1_last_reported_error = disk_err_none;

bool s_is_fullscreen = false;
uint32_t s_windowed_width = 0;
uint32_t s_windowed_height = 0;

auto reset_machine_state() -> void {
  full_speed = false;
  linapple_reset_hard();
}

auto set_icon() -> void {
  if (assets == nullptr || assets->icon == nullptr || !g_window) {
    return;
  }
  auto* icon = static_cast<SDL_Surface*>(assets->icon);
  const Uint32 colorkey = SDL_MapRGB(icon->format, 0, 0, 0);
  SDL_SetColorKey(icon, SDL_TRUE, colorkey);
  SDL_SetWindowIcon(g_window.get(), icon);
}

auto to_video_rect(const SDL_Rect& r) noexcept -> VideoRect {
  VideoRect vr{};
  vr.x = static_cast<int16_t>(r.x);
  vr.y = static_cast<int16_t>(r.y);
  vr.w = static_cast<uint16_t>(r.w);
  vr.h = static_cast<uint16_t>(r.h);
  return vr;
}

auto frame_save_bmp() -> void {
  if (g_screen == nullptr) {
    return;
  }
  struct stat bufp{};
  static int s_screenshot_index = 1;
  std::array<char, 32> bmp_name{};

  std::snprintf(bmp_name.data(), bmp_name.size(), "linapple%7d.bmp",
                s_screenshot_index);
  while (stat(bmp_name.data(), &bufp) == 0) {
    s_screenshot_index++;
    std::snprintf(bmp_name.data(), bmp_name.size(), "linapple%7d.bmp",
                  s_screenshot_index);
  }

  SDL_SaveBMP(g_screen.get(), bmp_name.data());
  std::printf("File %s saved!\n", bmp_name.data());
  s_screenshot_index++;
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

auto handle_btn_drive(int drive_index, int mod) -> void {
  if ((mod & KMOD_CTRL) != 0) {
    if ((mod & KMOD_SHIFT) != 0) {
      std::printf("HDD  Eject Drive #%d\n", drive_index + 1);
      HarddiskEjectCmd_t ecmd{static_cast<uint8_t>(drive_index)};
      peripheral_command(harddisk_default_slot, harddisk_cmd_eject, &ecmd,
                         sizeof(ecmd));
    } else {
      std::printf("Disk Eject Drive #%d\n", drive_index + 1);
      DiskEjectCmd_t ecmd{};
      ecmd.drive = static_cast<uint8_t>(drive_index);
      if (peripheral_command(disk_default_slot, disk_cmd_eject, &ecmd,
                             sizeof(ecmd)) == peripheral_ok) {
        app_controller_save_disk_config(drive_index);
      }
    }
    return;
  }

  if ((mod & KMOD_SHIFT) != 0) {
    if ((mod & KMOD_ALT) != 0) {
      harddisk_ui_ftp_select(drive_index);
    } else {
      harddisk_ui_select(drive_index);
    }
  } else {
    if ((mod & KMOD_ALT) != 0) {
      disk_ftp_select_image(drive_index);
    } else {
      disk_select(drive_index);
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
      std::printf(
          "Toggling keyboard rocker switch. Selected character set: %s...\n",
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
  return SDL_Rect{offset_x, offset_y, target_w, target_h};
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
  if (g_texture != nullptr && g_screen != nullptr && g_renderer != nullptr) {
    SDL_UpdateTexture(g_texture.get(), nullptr, g_screen->pixels,
                      g_screen->pitch);
    SDL_RenderCopy(g_renderer.get(), g_texture.get(), nullptr, nullptr);
    SDL_RenderPresent(g_renderer.get());
  }
}

auto draw_frame_window() -> void {
  if (!video_is_frame_ready()) {
    return;
  }

  {
    const std::lock_guard<std::recursive_mutex> lock(g_video_draw_mutex);
    if (g_texture == nullptr || g_screen == nullptr) {
      return;
    }
    uint32_t* output = video_get_output_buffer();
    if (output == nullptr) {
      return;
    }
    const SDL_Rect r = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

    if (system_state.mode != app_mode_debug) {
      ScopedSurfaceLock lock_screen(g_screen.get());
      const VideoSurfaceView vs_output(output, SCREEN_WIDTH, SCREEN_HEIGHT,
                                       SCREEN_WIDTH * 4, 4);

      if (!g_window_resized) {
        VideoRect vr = to_video_rect(r);
        video_soft_stretch(vs_output, &vr, lock_screen.view(), &vr);
      } else {
        VideoRect vor = to_video_rect(g_orig_rect);
        VideoRect vnr = to_video_rect(g_new_rect);
        video_soft_stretch(vs_output, &vor, lock_screen.view(), &vnr);
      }
    } else {
#if ENABLE_DEBUGGER
      if (g_debug_screen != nullptr) {
        ScopedSurfaceLock lock_screen(g_screen.get());
        if (!g_window_resized) {
          VideoRect vr = to_video_rect(r);
          video_soft_stretch(g_debug_screen, &vr, lock_screen.view(), &vr);
        } else {
          VideoRect vor = to_video_rect(g_orig_rect);
          VideoRect vnr = to_video_rect(g_new_rect);
          video_soft_stretch(g_debug_screen, &vor, lock_screen.view(), &vnr);
        }
      }
#endif
    }
    video_set_frame_ready(false);
  }

  frame_refresh();
}

auto draw_status_area(int drawflags) -> void {
  if (g_status_surface == nullptr || g_status_surface->pixels == nullptr) {
    return;
  }
  if (font_sfc == nullptr && !fonts_initialization()) {
    std::fprintf(stderr, "Font file was not loaded.\n");
    return;
  }

  VideoRect srect{};
  const uint8_t mybluez = DARK_BLUE;

  if ((drawflags & draw_background) != 0) {
    g_status_cycle = show_cycles;
  }
  if ((drawflags & draw_leds) != 0) {
    srect.x = 4;
    srect.y = 22;
    srect.w = static_cast<uint16_t>(STATUS_PANEL_W - 8);
    srect.h = static_cast<uint16_t>(STATUS_PANEL_H - 25);
    if (srect.x >= 0 && srect.y >= 0 &&
        (srect.x + srect.w) <= g_status_surface->w &&
        (srect.y + srect.h) <= g_status_surface->h) {
      for (int y = srect.y; y < srect.y + srect.h; ++y) {
        std::memset(static_cast<uint8_t*>(g_status_surface->pixels) +
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
    if (peripheral_query(7, harddisk_query_status, &hstatus, &hsize) ==
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
      g_status_cycle = show_cycles;
    }
  }
}

auto frame_show_help_screen(int sx, int sy) -> void {
  (void)sy;
  VideoSurface* temp_surface = nullptr;
  if (font_sfc == nullptr && !fonts_initialization()) {
    std::fprintf(stderr, "Font file was not loaded.\n");
    return;
  }
  if (!g_window_resized) {
    temp_surface =
        (system_state.mode == app_mode_logo) ? g_logo_bitmap : g_device_bitmap;
  } else {
    temp_surface = g_origscreen;
  }

  {
    ScopedSurfaceLock lock_screen(g_screen.get());
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
    SDL_SetSurfaceBlendMode(dim_surface.get(), SDL_BLENDMODE_BLEND);
    SDL_BlitSurface(dim_surface.get(), nullptr, g_screen.get(), nullptr);
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
    ScopedSurfaceLock lock_screen(g_screen.get());
    rectangle(lock_screen.view(), static_cast<int>(4.0f * facx_f), hdr_top,
              static_cast<int>(system_state.screen_width - (8.0f * facx_f)),
              hdr_height, RGB(255, 255, 0));

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
              body_height, RGB(255, 255, 255));

    const float line_spacing = 13.0f * facy_f;
    for (size_t i = 0; i < help_body_lines.size(); i++) {
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
      VideoRect logo{0, 0, static_cast<uint16_t>(lock_icon.view().w),
                     static_cast<uint16_t>(lock_icon.view().h)};
      VideoRect scrr{static_cast<int16_t>(460.0f * facx_f),
                     static_cast<int16_t>(270.0f * facy_f),
                     static_cast<uint16_t>(100.0f * facy_f),
                     static_cast<uint16_t>(100.0f * facy_f)};
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
      if (event.type == SDL_QUIT ||
          (event.type == SDL_WINDOWEVENT &&
           event.window.event == SDL_WINDOWEVENT_CLOSE)) {
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
    SDL_FillRect(g_screen.get(), nullptr, 0);
  }
  video_set_frame_ready(true);
  draw_frame_window();
}

auto frame_quick_state(int state, int mod) -> void {
  std::array<char, path_max_len> fpath{};
  std::snprintf(
      fpath.data(), fpath.size(), "%.*s/SaveState%d.aws",
      static_cast<int>(std::strlen(system_state.save_state_dir.data())),
      system_state.save_state_dir.data(), state);
  save_state_set_filename(fpath.data());
  if ((mod & KMOD_SHIFT) != 0) {
    save_state_save();
  } else {
    load_save_state();
  }
}

auto is_modifier_key(SDL_Keycode sym) noexcept -> bool {
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

  g_screen.reset(SDL_CreateRGBSurfaceWithFormat(
      0, static_cast<int>(system_state.screen_width),
      static_cast<int>(system_state.screen_height), 32,
      SDL_PIXELFORMAT_ARGB8888));

  g_texture.reset(SDL_CreateTexture(
      g_renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
      static_cast<int>(system_state.screen_width),
      static_cast<int>(system_state.screen_height)));

  if (g_screen == nullptr || g_texture == nullptr) {
    system_state.mode = app_mode_exit;
    return;
  }
  g_window_resized = (system_state.screen_width != SCREEN_WIDTH) ||
                     (system_state.screen_height != SCREEN_HEIGHT);
  if (!g_window_resized) {
    return;
  }

  g_orig_rect = SDL_Rect{0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
  g_new_rect = s_is_fullscreen
                   ? compute_aspect_fit_rect(width, height)
                   : SDL_Rect{0, 0, static_cast<int>(system_state.screen_width),
                              static_cast<int>(system_state.screen_height)};

  if ((system_state.mode != app_mode_logo) &&
      (system_state.mode != app_mode_debug)) {
    video_redraw_screen();
  }
}

auto frame_on_focus(bool gained) -> void {
  s_app_active = gained;
  if (gained) {
    keyboard_sync_host_caps(SDL_GetModState());
  } else {
    keyboard_release_host_modifiers();
  }
}

auto frame_on_expose() -> void {
  if ((system_state.mode != app_mode_logo) &&
      (system_state.mode != app_mode_debug)) {
    video_redraw_screen();
  }
}

namespace {

auto psp_save_state_select_image(bool saveit) -> bool {
  static size_t s_file_index = 0;
  static int s_backdx = 0;
  static int s_dirdx = 0;

  std::string filename;
  std::string full_path;
  bool is_directory = true;

  s_file_index = static_cast<size_t>(s_backdx);
  full_path = system_state.save_state_dir.data();

  while (is_directory) {
    if (!choose_an_image(static_cast<int>(system_state.screen_width),
                         static_cast<int>(system_state.screen_height),
                         full_path, saveit ? 1 : 0, filename, is_directory,
                         s_file_index)) {
      draw_frame_window();
      return false;
    }
    if (is_directory) {
      if (filename == "..") {
        const auto last_sep_pos = full_path.find_last_of(file_separator);
        if (last_sep_pos != std::string::npos) {
          if (last_sep_pos == 0) {
            full_path = "/";
          } else {
            full_path = full_path.substr(0, last_sep_pos);
          }
        }
        if (full_path.empty()) {
          full_path = "/";
        }
        s_file_index = static_cast<size_t>(s_dirdx);
      } else {
        if (full_path != "/") {
          full_path += "/" + filename;
        } else {
          full_path = "/" + filename;
        }
        s_dirdx = static_cast<int>(s_file_index);
        s_file_index = 0;
      }
    }
  }
  util_safe_strcpy(system_state.save_state_dir.data(), full_path.c_str(),
                   system_state.save_state_dir.size());
  Configuration::instance().set_string("Preferences", "Save State Directory",
                                       system_state.save_state_dir.data());
  Configuration::instance().save();

  s_backdx = static_cast<int>(s_file_index);
  full_path += "/" + filename;

  save_state_set_filename(full_path.c_str());
  Configuration::instance().set_string("Preferences", cfg_savestate_filename,
                                       full_path.c_str());
  Configuration::instance().save();
  draw_frame_window();
  return true;
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

  if ((system_state.mode != app_mode_debug) &&
      (system_state.mode != app_mode_paused)) {
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
  if (g_window != nullptr) {
    SDL_SetWindowFullscreen(g_window.get(), SDL_WINDOW_FULLSCREEN);
  }
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
    if (g_window != nullptr) {
      SDL_SetWindowGrab(g_window.get(), SDL_FALSE);
    }
    return;
  }

  s_is_fullscreen = false;
  if (g_window != nullptr) {
    SDL_SetWindowFullscreen(g_window.get(), 0);
    if (s_windowed_width > 0 && s_windowed_height > 0) {
      SDL_SetWindowSize(g_window.get(), static_cast<int>(s_windowed_width),
                        static_cast<int>(s_windowed_height));
      frame_on_resize(static_cast<int>(s_windowed_width),
                      static_cast<int>(s_windowed_height));
    }
  }
  if (!mouse_input_is_captured()) {
    SDL_ShowCursor(SDL_ENABLE);
  }
}

auto frame_pointer_capture(bool captured, bool relative) -> void {
  if (g_window != nullptr) {
    SDL_SetWindowGrab(g_window.get(), captured ? SDL_TRUE : SDL_FALSE);
    SDL_SetRelativeMouseMode(captured && relative ? SDL_TRUE : SDL_FALSE);
  }

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

  Uint32 flags = 0;
  if (system_state.fullscreen) {
    flags |= SDL_WINDOW_FULLSCREEN;
  }

  if (g_window == nullptr) {
    g_window.reset(SDL_CreateWindow(
        app_title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        static_cast<int>(system_state.screen_width),
        static_cast<int>(system_state.screen_height), flags));
  } else {
    SDL_SetWindowSize(g_window.get(),
                      static_cast<int>(system_state.screen_width),
                      static_cast<int>(system_state.screen_height));
  }
  if (g_window == nullptr) {
    std::fprintf(stderr, "Could not create SDL window: %s\n", SDL_GetError());
    return 1;
  }

  if (g_renderer == nullptr) {
    g_renderer.reset(
        SDL_CreateRenderer(g_window.get(), -1, SDL_RENDERER_ACCELERATED));
  }
  if (g_renderer == nullptr) {
    g_renderer.reset(
        SDL_CreateRenderer(g_window.get(), -1, SDL_RENDERER_SOFTWARE));
  }
  if (g_renderer == nullptr) {
    std::fprintf(stderr, "Could not create SDL renderer: %s\n", SDL_GetError());
    return 1;
  }

  g_screen.reset(SDL_CreateRGBSurfaceWithFormat(
      0, static_cast<int>(system_state.screen_width),
      static_cast<int>(system_state.screen_height), 32,
      SDL_PIXELFORMAT_ARGB8888));
  if (g_screen == nullptr) {
    std::fprintf(stderr, "Could not create SDL surface: %s\n", SDL_GetError());
    return 1;
  }

  g_texture.reset(SDL_CreateTexture(
      g_renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
      static_cast<int>(system_state.screen_width),
      static_cast<int>(system_state.screen_height)));
  if (g_texture == nullptr) {
    std::fprintf(stderr, "Could not create SDL texture: %s\n", SDL_GetError());
    return 1;
  }

  SDL_ShowWindow(g_window.get());
  set_icon();

  g_window_resized = (system_state.screen_width != SCREEN_WIDTH) ||
                     (system_state.screen_height != SCREEN_HEIGHT);
  if (g_window_resized) {
    g_orig_rect = SDL_Rect{0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    g_new_rect = SDL_Rect{0, 0, static_cast<int>(system_state.screen_width),
                          static_cast<int>(system_state.screen_height)};
  }
  std::printf("Screen size is %ux%u\n", system_state.screen_width,
              system_state.screen_height);
  return 0;
}

auto frame_destroy_window() -> void {
  g_texture.reset();
  g_screen.reset();
  g_renderer.reset();
  g_window.reset();
  sdl_asset_free_icon();
}

auto init_sdl() -> int {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
    std::fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
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
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title,
                           disk_ui_get_error_message(current_error),
                           g_window.get());
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
