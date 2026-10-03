// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/sdl3/DiskChoose.h"

#include <SDL3/SDL_blendmode.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_timer.h>

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
#include "frontends/common/VideoSurface.h"
#include "frontends/sdl3/Frame.h"
#include "frontends/sdl3/SDL_Video.h"
#include "frontends/sdl3/SdlPtr.h"

namespace {

constexpr size_t k_files_in_screen = 21;
constexpr uint32_t k_key_delay_ms = 25;
constexpr size_t k_max_filename = 80;
constexpr size_t k_normal_length = 60;

auto ensure_selection_visible(DiskChooseState_t& state,
                              size_t files_per_page) noexcept -> void {
  if (state.act_file < state.first_file) {
    state.first_file = state.act_file;
  } else if (state.act_file >= state.first_file + files_per_page) {
    state.first_file = state.act_file - files_per_page + 1;
  }
}

auto disk_choose_cleanup() -> void {
  g_diskChooseState.bg_screen.reset();
  if (g_diskChooseState.list_handle != nullptr) {
    file_browser_free_list(g_diskChooseState.list_handle);
    g_diskChooseState.list_handle = nullptr;
  }
  g_diskChooseState.index_file_out = nullptr;
}

auto wait_for_dismissal() -> void {
  SDL_Delay(k_key_delay_ms);
  SDL_Event event;
  while (true) {
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_F12) {
          system_state.mode = app_mode_exit;
          SDL_Event quit_event{};
          quit_event.type = SDL_EVENT_QUIT;
          SDL_PushEvent(&quit_event);
        }
        return;
      }
      if (event.type == SDL_EVENT_QUIT ||
          event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        SDL_PushEvent(&event);
        system_state.mode = app_mode_exit;
        return;
      }
    }
    SDL_Delay(10);
  }
}

auto prepare_dialog_background() -> bool {
  VideoSurface_t* temp_surface =
      !g_window_resized
          ? ((system_state.mode == app_mode_logo) ? g_logo_bitmap
                                                  : g_device_bitmap)
          : g_origscreen;

  VideoSurface_t vs_screen{};
  if (temp_surface == nullptr) {
    vs_screen = sdl_surface_to_video_surface(g_screen.get());
    temp_surface = &vs_screen;
  }

  if (temp_surface->w <= 0 || temp_surface->h <= 0) {
    return false;
  }

  g_diskChooseState.bg_screen.reset(SDL_CreateSurface(
      temp_surface->w, temp_surface->h, SDL_PIXELFORMAT_ARGB8888));
  if (g_diskChooseState.bg_screen == nullptr) {
    return false;
  }

  VideoSurface_t vs_bg =
      sdl_surface_to_video_surface(g_diskChooseState.bg_screen.get());
  video_soft_stretch(temp_surface, nullptr, &vs_bg, nullptr);

  const int blur_w = std::max(1, temp_surface->w / 16);
  const int blur_h = std::max(1, temp_surface->h / 16);
  SdlSurfacePtr_t blur_temp(
      SDL_CreateSurface(blur_w, blur_h, SDL_PIXELFORMAT_ARGB8888));
  if (blur_temp != nullptr) {
    VideoSurface_t vs_blur = sdl_surface_to_video_surface(blur_temp.get());
    video_soft_stretch(&vs_bg, nullptr, &vs_blur, nullptr);
    video_soft_stretch(&vs_blur, nullptr, &vs_bg, nullptr);
  }

  SdlSurfacePtr_t dim_surface(SDL_CreateSurface(
      temp_surface->w, temp_surface->h, SDL_PIXELFORMAT_ARGB8888));
  if (dim_surface != nullptr) {
    const Uint32 dim_color =
        SDL_MapRGBA(SDL_GetPixelFormatDetails(dim_surface->format),
                    SDL_GetSurfacePalette(dim_surface.get()), 0, 0, 0, 160);
    SDL_FillSurfaceRect(dim_surface.get(), nullptr, dim_color);
    SDL_SetSurfaceBlendMode(dim_surface.get(), SDL_BLENDMODE_BLEND);
    SDL_BlitSurface(dim_surface.get(), nullptr,
                    g_diskChooseState.bg_screen.get(), nullptr);
  }

  return true;
}
}  // namespace

DiskChooseState_t g_diskChooseState;

auto disk_choose_tick(SDL_Event* event) -> void {
  if (event == nullptr || !g_diskChooseState.active ||
      g_diskChooseState.list_handle == nullptr ||
      event->type != SDL_EVENT_KEY_DOWN) {
    return;
  }

  const SDL_Keycode key = event->key.key;
  const size_t list_count =
      file_browser_get_count(g_diskChooseState.list_handle);
  if (list_count == 0) {
    return;
  }

  bool selection_changed = false;

  switch (key) {
    case SDLK_UP:
    case SDLK_LEFT:
      if (g_diskChooseState.act_file > 0) {
        g_diskChooseState.act_file--;
        selection_changed = true;
      }
      break;

    case SDLK_DOWN:
    case SDLK_RIGHT:
      if (g_diskChooseState.act_file + 1 < list_count) {
        g_diskChooseState.act_file++;
        selection_changed = true;
      }
      break;

    case SDLK_PAGEUP:
      g_diskChooseState.act_file =
          (g_diskChooseState.act_file <= k_files_in_screen)
              ? 0
              : g_diskChooseState.act_file - k_files_in_screen;
      selection_changed = true;
      break;

    case SDLK_PAGEDOWN:
      g_diskChooseState.act_file = std::min(
          g_diskChooseState.act_file + k_files_in_screen, list_count - 1);
      selection_changed = true;
      break;

    case SDLK_HOME:
      g_diskChooseState.act_file = 0;
      g_diskChooseState.first_file = 0;
      break;

    case SDLK_END:
      g_diskChooseState.act_file = list_count - 1;
      selection_changed = true;
      break;

    case SDLK_RETURN: {
      const FileEntry_t* file_entry = file_browser_get_entry(
          g_diskChooseState.list_handle, g_diskChooseState.act_file);
      if (file_entry == nullptr) {
        break;
      }
      g_diskChooseState.result_filename = file_entry->name;
      g_diskChooseState.result_isdir = file_entry_is_dir_type(file_entry);
      if (g_diskChooseState.index_file_out != nullptr) {
        *g_diskChooseState.index_file_out = g_diskChooseState.act_file;
      }
      g_diskChooseState.finished = true;
      g_diskChooseState.active = false;
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
      SDL_Event quit_event{};
      quit_event.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&quit_event);
      break;
    }

    default: {
      if (std::isalnum(static_cast<unsigned char>(key)) == 0) {
        break;
      }
      const auto target_ch =
          static_cast<char>(std::toupper(static_cast<unsigned char>(key)));
      for (size_t i = 0; i < list_count; ++i) {
        const FileEntry_t* entry =
            file_browser_get_entry(g_diskChooseState.list_handle, i);
        if (entry == nullptr || entry->name[0] == '\0') {
          continue;
        }
        const auto entry_ch = static_cast<char>(
            std::toupper(static_cast<unsigned char>(entry->name[0])));
        if (entry_ch == target_ch) {
          g_diskChooseState.act_file = i;
          selection_changed = true;
          break;
        }
      }
      break;
    }
  }

  if (selection_changed) {
    ensure_selection_visible(g_diskChooseState, k_files_in_screen);
  }
}

auto disk_choose_draw() -> void {
  if (!g_diskChooseState.active || g_screen == nullptr) {
    return;
  }

  const float facx_f = static_cast<float>(system_state.screen_width) /
                       static_cast<float>(SCREEN_WIDTH);
  const float facy_f = static_cast<float>(system_state.screen_height) /
                       static_cast<float>(SCREEN_HEIGHT);
  const double facy = static_cast<double>(facy_f);
  const int screen_w = static_cast<int>(system_state.screen_width);

  VideoSurface_t vs_bg =
      sdl_surface_to_video_surface(g_diskChooseState.bg_screen.get());
  VideoSurface_t vs_screen = sdl_surface_to_video_surface(g_screen.get());

  video_soft_stretch(&vs_bg, nullptr, &vs_screen, nullptr);

  font_print_centered(
      screen_w / 2, static_cast<int>(5 * facy),
      g_diskChooseState.current_dir.substr(0, k_normal_length).c_str(),
      &vs_screen, 1.5f * facx_f, 1.3f * facy_f);

  const char* slot_title = disk_browser_get_title(g_diskChooseState.slot);
  if (slot_title[0] != '\0') {
    font_print_centered(screen_w / 2, static_cast<int>(20 * facy), slot_title,
                        &vs_screen, 1.0f * facx_f, 1.0f * facy_f);
  }

  font_print_centered(screen_w / 2, static_cast<int>(30 * facy),
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

    const std::string file_name = file_entry->name;

    if (i == g_diskChooseState.act_file) {
      SDL_Rect r{};
      r.x = 2;
      r.y = static_cast<int>(static_cast<double>(top_y) +
                             static_cast<double>(j) * 15.0 * facy - 1.0);
      const auto display_len = std::min(file_name.size(), k_max_filename);
      r.w = static_cast<int>(static_cast<double>(display_len * k_font_size_x) *
                             facx_f);
      r.h = static_cast<int>(9.0 * facy);
      SDL_FillSurfaceRect(
          g_screen.get(), &r,
          SDL_MapRGB(SDL_GetPixelFormatDetails(g_screen->format),
                     SDL_GetSurfacePalette(g_screen.get()), 64, 128, 190));
    }

    std::array<char, 32> type_size_str{};
    file_entry_format_type_or_size(file_entry, type_size_str.data(),
                                   type_size_str.size());

    font_print(4,
               static_cast<int>(static_cast<double>(top_y) +
                                static_cast<double>(j) * 15.0 * facy),
               file_name.substr(0, k_max_filename).c_str(), &vs_screen,
               1.0f * facx_f, 1.0f * facy_f);
    font_print_right(
        screen_w - static_cast<int>(8.0 * static_cast<double>(facx_f)),
        static_cast<int>(static_cast<double>(top_y) +
                         static_cast<double>(j) * 15.0 * facy),
        type_size_str.data(), &vs_screen, 1.0f * facx_f, 1.0f * facy_f);
  }

  rectangle(&vs_screen, 0, top_y - 5, screen_w - 1,
            static_cast<int>(320.0 * facy), RGB(255, 255, 255));
  rectangle(&vs_screen, static_cast<int>(480.0 * static_cast<double>(facx_f)),
            top_y - 5, 0, static_cast<int>(320.0 * facy), RGB(255, 255, 255));

  frame_refresh();
}

auto choose_image_dialog(int screen_w, int screen_h, const std::string& dir,
                         int slot, FileListGenerator_t* file_list_generator,
                         std::string& filename, bool& isdir, size_t& index_file)
    -> bool {
  (void)screen_h;
  if (file_list_generator == nullptr) {
    return false;
  }

  const double facx = static_cast<double>(system_state.screen_width) /
                      static_cast<double>(SCREEN_WIDTH);
  const double facy = static_cast<double>(system_state.screen_height) /
                      static_cast<double>(SCREEN_HEIGHT);

  if (font_sfc == nullptr && !fonts_initialization()) {
    return false;
  }

  if (!prepare_dialog_background()) {
    return false;
  }

  VideoSurface_t vs_bg =
      sdl_surface_to_video_surface(g_diskChooseState.bg_screen.get());
  VideoSurface_t vs_actual_screen =
      sdl_surface_to_video_surface(g_screen.get());

  {
    const std::lock_guard<std::recursive_mutex> lock(g_video_draw_mutex);
    video_soft_stretch(&vs_bg, nullptr, &vs_actual_screen, nullptr);

    font_print_centered(screen_w / 2, static_cast<int>(5 * facy),
                        dir.substr(0, k_normal_length).c_str(),
                        &vs_actual_screen, static_cast<float>(1.5 * facx),
                        static_cast<float>(1.3 * facy));
    font_print_centered(
        screen_w / 2, static_cast<int>(20 * facy),
        file_list_generator->get_starting_message(file_list_generator),
        &vs_actual_screen, static_cast<float>(1.0 * facx),
        static_cast<float>(1.0 * facy));
    frame_refresh();
  }

  g_diskChooseState.list_handle =
      file_list_generator->generate_file_list(file_list_generator);
  if (g_diskChooseState.list_handle == nullptr ||
      file_browser_get_count(g_diskChooseState.list_handle) < 1) {
    std::printf("%s\n",
                file_list_generator->get_failure_message(file_list_generator));

    {
      const std::lock_guard<std::recursive_mutex> lock(g_video_draw_mutex);
      font_print_centered(screen_w / 2, static_cast<int>(30 * facy),
                          "Failure. Press any key!", &vs_actual_screen,
                          static_cast<float>(1.4 * facx),
                          static_cast<float>(1.1 * facy));
      frame_refresh();
    }

    wait_for_dismissal();
    disk_choose_cleanup();
    return false;
  }

  g_diskChooseState.slot = slot;
  g_diskChooseState.current_dir = dir;
  g_diskChooseState.act_file = index_file;
  if (g_diskChooseState.act_file >=
      file_browser_get_count(g_diskChooseState.list_handle)) {
    g_diskChooseState.act_file = 0;
  }
  g_diskChooseState.first_file =
      (g_diskChooseState.act_file > k_files_in_screen / 2)
          ? g_diskChooseState.act_file - (k_files_in_screen / 2)
          : 0;
  g_diskChooseState.active = true;
  g_diskChooseState.finished = false;
  g_diskChooseState.cancelled = false;
  g_diskChooseState.index_file_out = &index_file;

  const AppMode_t old_mode = system_state.mode;
  system_state.mode = app_mode_disk_choose;

  while (g_diskChooseState.active) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT ||
          event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        SDL_PushEvent(&event);
        system_state.mode = app_mode_exit;
        g_diskChooseState.active = false;
        g_diskChooseState.cancelled = true;
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
  disk_choose_cleanup();

  if (g_diskChooseState.finished) {
    filename = g_diskChooseState.result_filename;
    isdir = g_diskChooseState.result_isdir;
    return true;
  }

  return false;
}

auto choose_an_image(int screen_w, int screen_h,
                     const std::string& incoming_dir, int slot,
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

  const bool result =
      choose_image_dialog(screen_w, screen_h, incoming_dir, slot, generator,
                          filename, isdir, index_file);
  generator->destroy(generator);
  return result;
}
