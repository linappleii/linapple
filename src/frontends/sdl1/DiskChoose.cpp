// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/sdl1/DiskChoose.h"

#include <SDL/SDL_events.h>
#include <SDL/SDL_keysym.h>
#include <SDL/SDL_stdinc.h>
#include <SDL/SDL_timer.h>
#include <SDL/SDL_video.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "frontends/common/FileBrowser.h"
#include "frontends/common/VideoStretch.h"
#include "frontends/sdl1/Frame.h"
#include "frontends/sdl1/SDL_Video.h"

using std::string;

namespace {

constexpr size_t k_files_in_screen = 21;
constexpr uint32_t k_key_delay_ms = 25;
constexpr size_t k_max_filename = 80;
constexpr size_t k_normal_length = 60;
constexpr int k_font_char_width = 8;
constexpr int k_rect_width = 320;
constexpr int k_rect_margin = 5;

auto ensure_selection_visible(DiskChooseState_t& state,
                              size_t files_per_page) noexcept -> void {
  if (state.act_file < state.first_file) {
    state.first_file = state.act_file;
  } else if (state.act_file >= state.first_file + files_per_page) {
    state.first_file = state.act_file - files_per_page + 1;
  }
}
}  // namespace

DiskChooseState_t g_diskChooseState;

auto disk_choose_tick(SDL_Event* event) -> void {
  if (event == nullptr || !g_diskChooseState.active ||
      g_diskChooseState.list_handle == nullptr) {
    return;
  }
  if (event->type != SDL_KEYDOWN) {
    return;
  }

  const SDLKey key = event->key.keysym.sym;
  const size_t list_count =
      file_browser_get_count(g_diskChooseState.list_handle);
  if (list_count == 0) {
    return;
  }

  switch (key) {
    case SDLK_UP:
    case SDLK_LEFT:
      if (g_diskChooseState.act_file > 0) {
        --g_diskChooseState.act_file;
      }
      ensure_selection_visible(g_diskChooseState, k_files_in_screen);
      break;

    case SDLK_DOWN:
    case SDLK_RIGHT:
      if (g_diskChooseState.act_file < (list_count - 1)) {
        ++g_diskChooseState.act_file;
      }
      ensure_selection_visible(g_diskChooseState, k_files_in_screen);
      break;

    case SDLK_PAGEUP:
      if (g_diskChooseState.act_file <= k_files_in_screen) {
        g_diskChooseState.act_file = 0;
      } else {
        g_diskChooseState.act_file -= k_files_in_screen;
      }
      ensure_selection_visible(g_diskChooseState, k_files_in_screen);
      break;

    case SDLK_PAGEDOWN:
      g_diskChooseState.act_file += k_files_in_screen;
      if (g_diskChooseState.act_file >= list_count) {
        g_diskChooseState.act_file = list_count - 1;
      }
      ensure_selection_visible(g_diskChooseState, k_files_in_screen);
      break;

    case SDLK_RETURN: {
      const FileEntry_t* file_entry = file_browser_get_entry(
          g_diskChooseState.list_handle, g_diskChooseState.act_file);
      if (file_entry != nullptr) {
        g_diskChooseState.result_filename = file_entry->name;
        g_diskChooseState.result_isdir = file_entry_is_dir_type(file_entry);
        if (g_diskChooseState.index_file_out != nullptr) {
          *g_diskChooseState.index_file_out = g_diskChooseState.act_file;
        }
        g_diskChooseState.finished = true;
        g_diskChooseState.active = false;
      }
      break;
    }

    case SDLK_ESCAPE:
      g_diskChooseState.active = false;
      g_diskChooseState.cancelled = true;
      break;

    case SDLK_F12: {
      g_diskChooseState.active = false;
      g_diskChooseState.cancelled = true;
      system_state.mode = app_mode_exit;
      SDL_Event qe{};
      qe.type = SDL_QUIT;
      SDL_PushEvent(&qe);
      return;
    }

    case SDLK_HOME:
      g_diskChooseState.act_file = 0;
      g_diskChooseState.first_file = 0;
      break;

    case SDLK_END:
      g_diskChooseState.act_file = list_count - 1;
      ensure_selection_visible(g_diskChooseState, k_files_in_screen);
      break;

    default:
      if (std::isalnum(static_cast<unsigned char>(key)) != 0) {
        const auto target_ch =
            static_cast<char>(std::toupper(static_cast<unsigned char>(key)));
        for (size_t i = 0; i < list_count; ++i) {
          const FileEntry_t* entry =
              file_browser_get_entry(g_diskChooseState.list_handle, i);
          if (entry != nullptr && entry->name[0] != '\0') {
            const auto entry_ch = static_cast<char>(
                std::toupper(static_cast<unsigned char>(entry->name[0])));
            if (entry_ch == target_ch) {
              g_diskChooseState.act_file = i;
              ensure_selection_visible(g_diskChooseState, k_files_in_screen);
              break;
            }
          }
        }
      }
      break;
  }
}

auto disk_choose_draw() -> void {
  if (!g_diskChooseState.active || g_screen == nullptr ||
      g_screen->format == nullptr) {
    return;
  }

  const float facx_f = static_cast<float>(system_state.screen_width) /
                       static_cast<float>(SCREEN_WIDTH);
  const float facy_f = static_cast<float>(system_state.screen_height) /
                       static_cast<float>(SCREEN_HEIGHT);
  const auto facy = static_cast<double>(facy_f);
  const int sx = static_cast<int>(system_state.screen_width);

  VideoSurface_t vs_bg =
      sdl_surface_to_video_surface(g_diskChooseState.bg_screen.get());
  VideoSurface_t vs_screen = sdl_surface_to_video_surface(g_screen);

  video_soft_stretch(&vs_bg, nullptr, &vs_screen, nullptr);

  font_print_centered(
      sx / 2, static_cast<int>(5 * facy),
      g_diskChooseState.current_dir.substr(0, k_normal_length).c_str(),
      &vs_screen, 1.5f * facx_f, 1.3f * facy_f);

  const char* title = disk_browser_get_title(g_diskChooseState.slot);
  if (title[0] != '\0') {
    font_print_centered(sx / 2, static_cast<int>(20 * facy), title, &vs_screen,
                        1.0f * facx_f, 1.0f * facy_f);
  }

  font_print_centered(sx / 2, static_cast<int>(30 * facy),
                      "Press ENTER to choose, or ESC to cancel", &vs_screen,
                      1.0f * facx_f, 1.0f * facy_f);

  const int top_y = static_cast<int>(45 * facy);
  const size_t list_count =
      g_diskChooseState.list_handle != nullptr
          ? file_browser_get_count(g_diskChooseState.list_handle)
          : 0;

  for (size_t j = 0; j < k_files_in_screen; ++j) {
    const size_t i = g_diskChooseState.first_file + j;
    if (i >= list_count) {
      break;
    }
    const FileEntry_t* file_entry =
        file_browser_get_entry(g_diskChooseState.list_handle, i);
    if (file_entry == nullptr) {
      continue;
    }

    const string file_name = file_entry->name;

    if (i == g_diskChooseState.act_file) {
      SDL_Rect r{};
      r.x = 2;
      r.y = static_cast<int>(static_cast<double>(top_y) +
                             static_cast<double>(j) * 15.0 * facy - 1.0);
      const auto display_len = std::min(file_name.size(), k_max_filename);
      r.w = static_cast<int>(
          static_cast<double>(display_len * k_font_char_width) *
          static_cast<double>(facx_f));
      r.h = static_cast<int>(9.0 * facy);
      SDL_FillRect(g_screen, &r, SDL_MapRGB(g_screen->format, 64, 128, 190));
    }

    std::array<char, 32> type_size_str{};
    file_entry_format_type_or_size(file_entry, type_size_str.data(),
                                   type_size_str.size());

    font_print(4,
               static_cast<int>(static_cast<double>(top_y) +
                                static_cast<double>(j) * 15.0 * facy),
               file_name.substr(0, k_max_filename).c_str(), &vs_screen,
               1.0f * facx_f, 1.0f * facy_f);
    font_print_right(sx - static_cast<int>(8.0 * static_cast<double>(facx_f)),
                     static_cast<int>(static_cast<double>(top_y) +
                                      static_cast<double>(j) * 15.0 * facy),
                     type_size_str.data(), &vs_screen, 1.0f * facx_f,
                     1.0f * facy_f);
  }

  rectangle(&vs_screen, 0, top_y - k_rect_margin, sx - 1,
            static_cast<int>(k_rect_width * facy), RGB(255, 255, 255));
  rectangle(&vs_screen, static_cast<int>(480.0 * static_cast<double>(facx_f)),
            top_y - k_rect_margin, 0, static_cast<int>(k_rect_width * facy),
            RGB(255, 255, 255));

  frame_refresh();
}

auto choose_image_dialog(int sx, int sy, const string& dir, int slot,
                         FileListGenerator_t* file_list_generator,
                         std::string& filename, bool& isdir, size_t& index_file)
    -> bool {
  (void)sy;
  if (file_list_generator == nullptr) {
    return false;
  }

  const auto facx = static_cast<double>(system_state.screen_width) /
                    static_cast<double>(SCREEN_WIDTH);
  const auto facy = static_cast<double>(system_state.screen_height) /
                    static_cast<double>(SCREEN_HEIGHT);

  if (font_sfc == nullptr && !fonts_initialization()) {
    return false;
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

  if (temp_surface->w <= 0 || temp_surface->h <= 0) {
    return false;
  }

  g_diskChooseState.bg_screen.reset(
      SDL_CreateRGBSurface(SDL_HWSURFACE, temp_surface->w, temp_surface->h, 32,
                           0x00FF0000, 0x0000FF00, 0x000000FF, 0));
  if (g_diskChooseState.bg_screen == nullptr) {
    return false;
  }

  VideoSurface_t vs_bg =
      sdl_surface_to_video_surface(g_diskChooseState.bg_screen.get());
  VideoSurface_t vs_actual_screen = sdl_surface_to_video_surface(g_screen);

  video_soft_stretch(temp_surface, nullptr, &vs_bg, nullptr);

  const int blur_w = std::max(1, temp_surface->w / 16);
  const int blur_h = std::max(1, temp_surface->h / 16);
  SdlSurfacePtr_t blur_temp(SDL_CreateRGBSurface(
      0, blur_w, blur_h, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0));
  if (blur_temp != nullptr) {
    VideoSurface_t vs_blur = sdl_surface_to_video_surface(blur_temp.get());
    video_soft_stretch(&vs_bg, nullptr, &vs_blur, nullptr);
    video_soft_stretch(&vs_blur, nullptr, &vs_bg, nullptr);
  }

  SdlSurfacePtr_t dim_surface(
      SDL_CreateRGBSurface(0, temp_surface->w, temp_surface->h, 32, 0x00FF0000,
                           0x0000FF00, 0x000000FF, 0));
  if (dim_surface != nullptr) {
    const Uint32 dim_color = SDL_MapRGBA(dim_surface->format, 0, 0, 0, 160);
    SDL_FillRect(dim_surface.get(), nullptr, dim_color);
    SDL_SetAlpha(dim_surface.get(), SDL_SRCALPHA, 160);
    SDL_BlitSurface(dim_surface.get(), nullptr,
                    g_diskChooseState.bg_screen.get(), nullptr);
  }

  {
    const std::lock_guard<std::recursive_mutex> lock(g_video_draw_mutex);
    video_soft_stretch(&vs_bg, nullptr, &vs_actual_screen, nullptr);

    font_print_centered(sx / 2, static_cast<int>(5 * facy),
                        dir.substr(0, k_normal_length).c_str(),
                        &vs_actual_screen, 1.5 * facx, 1.3 * facy);
    font_print_centered(
        sx / 2, static_cast<int>(20 * facy),
        file_list_generator->get_starting_message(file_list_generator),
        &vs_actual_screen, 1.0 * facx, 1.0 * facy);
    frame_refresh();
  }

  g_diskChooseState.list_handle =
      file_list_generator->generate_file_list(file_list_generator);
  if (g_diskChooseState.list_handle == nullptr ||
      file_browser_get_count(g_diskChooseState.list_handle) < 1) {
    printf("%s\n",
           file_list_generator->get_failure_message(file_list_generator));

    {
      const std::lock_guard<std::recursive_mutex> lock(g_video_draw_mutex);
      font_print_centered(sx / 2, static_cast<int>(30 * facy),
                          "Failure. Press any key!", &vs_actual_screen,
                          1.4 * facx, 1.1 * facy);
      frame_refresh();
    }

    SDL_Delay(k_key_delay_ms);
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
    g_diskChooseState.bg_screen.reset();
    if (g_diskChooseState.list_handle != nullptr) {
      file_browser_free_list(g_diskChooseState.list_handle);
      g_diskChooseState.list_handle = nullptr;
    }
    return false;
  }

  g_diskChooseState.slot = slot;
  g_diskChooseState.current_dir = dir;
  g_diskChooseState.act_file = index_file;
  if (g_diskChooseState.act_file >=
      file_browser_get_count(g_diskChooseState.list_handle)) {
    g_diskChooseState.act_file = 0;
  }
  if (g_diskChooseState.act_file <= k_files_in_screen / 2) {
    g_diskChooseState.first_file = 0;
  } else {
    g_diskChooseState.first_file =
        g_diskChooseState.act_file - (k_files_in_screen / 2);
  }
  g_diskChooseState.active = true;
  g_diskChooseState.finished = false;
  g_diskChooseState.cancelled = false;
  g_diskChooseState.index_file_out = &index_file;

  const AppMode_t old_mode = system_state.mode;
  system_state.mode = app_mode_disk_choose;

  while (g_diskChooseState.active) {
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT) {
        SDL_PushEvent(&event);
        system_state.mode = app_mode_exit;
        g_diskChooseState.active = false;
        g_diskChooseState.cancelled = true;
        break;
      }
      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F12) {
        system_state.mode = app_mode_exit;
        g_diskChooseState.active = false;
        g_diskChooseState.cancelled = true;
        SDL_Event qe{};
        qe.type = SDL_QUIT;
        SDL_PushEvent(&qe);
        break;
      }
      disk_choose_tick(&event);
    }
    {
      const std::lock_guard<std::recursive_mutex> lock(g_video_draw_mutex);
      disk_choose_draw();
    }
    SDL_Delay(10);
  }

  if (system_state.mode != app_mode_exit) {
    system_state.mode = old_mode;
  }
  g_diskChooseState.bg_screen.reset();

  if (g_diskChooseState.list_handle != nullptr) {
    file_browser_free_list(g_diskChooseState.list_handle);
    g_diskChooseState.list_handle = nullptr;
  }

  if (g_diskChooseState.finished) {
    filename = g_diskChooseState.result_filename;
    isdir = g_diskChooseState.result_isdir;
    return true;
  }

  return false;
}

auto choose_an_image(int sx, int sy, const std::string& incoming_dir, int slot,
                     std::string& filename, bool& isdir, size_t& index_file)
    -> bool {
  std::array<char, 256> supported_exts{};
  linapple_get_supported_disk_extensions(slot, supported_exts.data(),
                                         supported_exts.size());

  FileListGenerator_t* generator = file_browser_create_local_generator(
      incoming_dir.c_str(), supported_exts.data());
  if (generator == nullptr) {
    return false;
  }

  const bool result = choose_image_dialog(sx, sy, incoming_dir, slot, generator,
                                          filename, isdir, index_file);
  generator->destroy(generator);
  return result;
}
