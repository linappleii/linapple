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
#include "frontends/common/VideoSurface.h"
#include "frontends/sdl1/Frame.h"
#include "frontends/sdl1/SDL_Video.h"
#include "frontends/sdl1/SdlPtr.h"

using std::string;

namespace {

constexpr size_t files_in_screen = 21;
constexpr uint32_t key_delay_ms = 25;
constexpr size_t max_filename = 80;
constexpr size_t normal_length = 60;
constexpr int font_char_width = 8;
constexpr int rect_width = 320;
constexpr int rect_margin = 5;

auto ensure_selection_visible(DiskChooseState& state,
                              size_t files_per_page) noexcept -> void {
  if (state.act_file < state.first_file) {
    state.first_file = state.act_file;
  } else if (state.act_file >= state.first_file + files_per_page) {
    state.first_file = state.act_file - files_per_page + 1;
  }
}

auto disk_choose_cleanup() -> void {
  g_disk_choose_state.bg_screen.reset();
  if (g_disk_choose_state.list_handle != nullptr) {
    file_browser_free_list(g_disk_choose_state.list_handle);
    g_disk_choose_state.list_handle = nullptr;
  }
  g_disk_choose_state.index_file_out = nullptr;
}

auto wait_for_dismissal() -> void {
  SDL_Delay(key_delay_ms);
  SDL_Event event{};
  while (true) {
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_F12) {
          system_state.mode = app_mode_exit;
          SDL_Event qe{};
          qe.type = SDL_QUIT;
          SDL_PushEvent(&qe);
        }
        return;
      }
      if (event.type == SDL_QUIT) {
        SDL_PushEvent(&event);
        system_state.mode = app_mode_exit;
        return;
      }
    }
    SDL_Delay(10);
  }
}

auto prepare_dialog_background() -> bool {
  VideoSurface* temp_surface =
      !g_window_resized
          ? ((system_state.mode == app_mode_logo) ? g_logo_bitmap
                                                  : g_device_bitmap)
          : g_origscreen;

  ScopedSurfaceLock lock_screen(g_screen);
  const VideoSurfaceView src_view =
      (temp_surface != nullptr) ? *temp_surface : lock_screen.view();

  if (src_view.w <= 0 || src_view.h <= 0) {
    return false;
  }

  g_disk_choose_state.bg_screen.reset(
      SDL_CreateRGBSurface(SDL_HWSURFACE, src_view.w, src_view.h, 32,
                           0x00FF0000, 0x0000FF00, 0x000000FF, 0));
  if (g_disk_choose_state.bg_screen == nullptr) {
    return false;
  }

  {
    ScopedSurfaceLock lock_bg(g_disk_choose_state.bg_screen.get());
    video_soft_stretch(src_view, nullptr, lock_bg.view(), nullptr);

    const int blur_w = std::max(1, src_view.w / 16);
    const int blur_h = std::max(1, src_view.h / 16);
    SdlSurfacePtr blur_temp(SDL_CreateRGBSurface(
        0, blur_w, blur_h, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0));
    if (blur_temp != nullptr) {
      ScopedSurfaceLock lock_blur(blur_temp.get());
      video_soft_stretch(lock_bg.view(), nullptr, lock_blur.view(), nullptr);
      video_soft_stretch(lock_blur.view(), nullptr, lock_bg.view(), nullptr);
    }
  }

  SdlSurfacePtr dim_surface(SDL_CreateRGBSurface(
      0, src_view.w, src_view.h, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0));
  if (dim_surface != nullptr) {
    const Uint32 dim_color = SDL_MapRGBA(dim_surface->format, 0, 0, 0, 160);
    SDL_FillRect(dim_surface.get(), nullptr, dim_color);
    SDL_SetAlpha(dim_surface.get(), SDL_SRCALPHA, 160);
    SDL_BlitSurface(dim_surface.get(), nullptr,
                    g_disk_choose_state.bg_screen.get(), nullptr);
  }

  return true;
}
}  // namespace

DiskChooseState g_disk_choose_state;

auto disk_choose_tick(SDL_Event* event) -> void {
  if (event == nullptr || !g_disk_choose_state.active ||
      g_disk_choose_state.list_handle == nullptr ||
      event->type != SDL_KEYDOWN) {
    return;
  }

  const SDLKey key = event->key.keysym.sym;
  const size_t list_count =
      file_browser_get_count(g_disk_choose_state.list_handle);
  if (list_count == 0) {
    return;
  }

  bool selection_changed = false;

  switch (key) {
    case SDLK_UP:
    case SDLK_LEFT:
      if (g_disk_choose_state.act_file > 0) {
        --g_disk_choose_state.act_file;
        selection_changed = true;
      }
      break;

    case SDLK_DOWN:
    case SDLK_RIGHT:
      if (g_disk_choose_state.act_file + 1 < list_count) {
        ++g_disk_choose_state.act_file;
        selection_changed = true;
      }
      break;

    case SDLK_PAGEUP:
      g_disk_choose_state.act_file =
          (g_disk_choose_state.act_file <= files_in_screen)
              ? 0
              : g_disk_choose_state.act_file - files_in_screen;
      selection_changed = true;
      break;

    case SDLK_PAGEDOWN:
      g_disk_choose_state.act_file = std::min(
          g_disk_choose_state.act_file + files_in_screen, list_count - 1);
      selection_changed = true;
      break;

    case SDLK_HOME:
      g_disk_choose_state.act_file = 0;
      g_disk_choose_state.first_file = 0;
      break;

    case SDLK_END:
      g_disk_choose_state.act_file = list_count - 1;
      selection_changed = true;
      break;

    case SDLK_RETURN: {
      const FileEntry* file_entry = file_browser_get_entry(
          g_disk_choose_state.list_handle, g_disk_choose_state.act_file);
      if (file_entry == nullptr) {
        break;
      }
      g_disk_choose_state.result_filename = file_entry->name;
      g_disk_choose_state.result_isdir = file_entry_is_dir_type(file_entry);
      if (g_disk_choose_state.index_file_out != nullptr) {
        *g_disk_choose_state.index_file_out = g_disk_choose_state.act_file;
      }
      g_disk_choose_state.finished = true;
      g_disk_choose_state.active = false;
      break;
    }

    case SDLK_ESCAPE:
      g_disk_choose_state.active = false;
      g_disk_choose_state.cancelled = true;
      break;

    case SDLK_F12: {
      g_disk_choose_state.active = false;
      g_disk_choose_state.cancelled = true;
      system_state.mode = app_mode_exit;
      SDL_Event qe{};
      qe.type = SDL_QUIT;
      SDL_PushEvent(&qe);
      break;
    }

    default: {
      if (std::isalnum(static_cast<unsigned char>(key)) == 0) {
        break;
      }
      const auto target_ch =
          static_cast<char>(std::toupper(static_cast<unsigned char>(key)));
      for (size_t i = 0; i < list_count; ++i) {
        const FileEntry* entry =
            file_browser_get_entry(g_disk_choose_state.list_handle, i);
        if (entry == nullptr || entry->name[0] == '\0') {
          continue;
        }
        const auto entry_ch = static_cast<char>(
            std::toupper(static_cast<unsigned char>(entry->name[0])));
        if (entry_ch == target_ch) {
          g_disk_choose_state.act_file = i;
          selection_changed = true;
          break;
        }
      }
      break;
    }
  }

  if (selection_changed) {
    ensure_selection_visible(g_disk_choose_state, files_in_screen);
  }
}

auto disk_choose_draw() -> void {
  if (!g_disk_choose_state.active || g_screen == nullptr ||
      g_screen->format == nullptr) {
    return;
  }

  const float facx_f = static_cast<float>(system_state.screen_width) /
                       static_cast<float>(SCREEN_WIDTH);
  const float facy_f = static_cast<float>(system_state.screen_height) /
                       static_cast<float>(SCREEN_HEIGHT);
  const auto facy = static_cast<double>(facy_f);
  const int sx = static_cast<int>(system_state.screen_width);

  {
    ScopedSurfaceLock lock_bg(g_disk_choose_state.bg_screen.get());
    ScopedSurfaceLock lock_screen(g_screen);

    video_soft_stretch(lock_bg.view(), nullptr, lock_screen.view(), nullptr);

    font_print_centered(
        sx / 2, static_cast<int>(5 * facy),
        g_disk_choose_state.current_dir.substr(0, normal_length).c_str(),
        lock_screen.view(), 1.5f * facx_f, 1.3f * facy_f);

    const char* title = disk_browser_get_title(g_disk_choose_state.slot);
    if (title[0] != '\0') {
      font_print_centered(sx / 2, static_cast<int>(20 * facy), title,
                          lock_screen.view(), 1.0f * facx_f, 1.0f * facy_f);
    }

    font_print_centered(sx / 2, static_cast<int>(30 * facy),
                        "Press ENTER to choose, or ESC to cancel",
                        lock_screen.view(), 1.0f * facx_f, 1.0f * facy_f);

    const int top_y = static_cast<int>(45 * facy);
    const size_t list_count =
        g_disk_choose_state.list_handle != nullptr
            ? file_browser_get_count(g_disk_choose_state.list_handle)
            : 0;

    const Uint32 sel_color = SDL_MapRGB(g_screen->format, 64, 128, 190);

    for (size_t j = 0; j < files_in_screen; ++j) {
      const size_t i = g_disk_choose_state.first_file + j;
      if (i >= list_count) {
        break;
      }
      const FileEntry* file_entry =
          file_browser_get_entry(g_disk_choose_state.list_handle, i);
      if (file_entry == nullptr) {
        continue;
      }

      const string file_name = file_entry->name;

      if (i == g_disk_choose_state.act_file) {
        const int rx = 2;
        const int ry =
            static_cast<int>(static_cast<double>(top_y) +
                             static_cast<double>(j) * 15.0 * facy - 1.0);
        const auto display_len = std::min(file_name.size(), max_filename);
        const int rw = static_cast<int>(
            static_cast<double>(display_len * font_char_width) *
            static_cast<double>(facx_f));
        const int rh = static_cast<int>(9.0 * facy);
        fill_rectangle(lock_screen.view(), rx, ry, rw, rh, sel_color);
      }

      std::array<char, 32> type_size_str{};
      file_entry_format_type_or_size(file_entry, type_size_str.data(),
                                     type_size_str.size());

      font_print(4,
                 static_cast<int>(static_cast<double>(top_y) +
                                  static_cast<double>(j) * 15.0 * facy),
                 file_name.substr(0, max_filename).c_str(), lock_screen.view(),
                 1.0f * facx_f, 1.0f * facy_f);
      font_print_right(sx - static_cast<int>(8.0 * static_cast<double>(facx_f)),
                       static_cast<int>(static_cast<double>(top_y) +
                                        static_cast<double>(j) * 15.0 * facy),
                       type_size_str.data(), lock_screen.view(), 1.0f * facx_f,
                       1.0f * facy_f);
    }

    rectangle(lock_screen.view(), 0, top_y - rect_margin, sx - 1,
              static_cast<int>(rect_width * facy), RGB(255, 255, 255));
    rectangle(lock_screen.view(),
              static_cast<int>(480.0 * static_cast<double>(facx_f)),
              top_y - rect_margin, 0, static_cast<int>(rect_width * facy),
              RGB(255, 255, 255));
  }

  frame_refresh();
}

auto choose_image_dialog(int sx, int sy, const string& dir, int slot,
                         FileListGenerator* file_list_generator,
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

  if (!prepare_dialog_background()) {
    return false;
  }

  {
    const std::lock_guard<std::recursive_mutex> lock(g_video_draw_mutex);
    ScopedSurfaceLock lock_bg(g_disk_choose_state.bg_screen.get());
    ScopedSurfaceLock lock_screen(g_screen);

    video_soft_stretch(lock_bg.view(), nullptr, lock_screen.view(), nullptr);

    font_print_centered(sx / 2, static_cast<int>(5 * facy),
                        dir.substr(0, normal_length).c_str(),
                        lock_screen.view(), 1.5 * facx, 1.3 * facy);
    font_print_centered(
        sx / 2, static_cast<int>(20 * facy),
        file_list_generator->get_starting_message(file_list_generator),
        lock_screen.view(), 1.0 * facx, 1.0 * facy);
    frame_refresh();
  }

  g_disk_choose_state.list_handle =
      file_list_generator->generate_file_list(file_list_generator);
  if (g_disk_choose_state.list_handle == nullptr ||
      file_browser_get_count(g_disk_choose_state.list_handle) < 1) {
    printf("%s\n",
           file_list_generator->get_failure_message(file_list_generator));

    {
      const std::lock_guard<std::recursive_mutex> lock(g_video_draw_mutex);
      ScopedSurfaceLock lock_screen(g_screen);
      font_print_centered(sx / 2, static_cast<int>(30 * facy),
                          "Failure. Press any key!", lock_screen.view(),
                          1.4 * facx, 1.1 * facy);
      frame_refresh();
    }

    wait_for_dismissal();
    disk_choose_cleanup();
    return false;
  }

  g_disk_choose_state.slot = slot;
  g_disk_choose_state.current_dir = dir;
  g_disk_choose_state.act_file = index_file;
  if (g_disk_choose_state.act_file >=
      file_browser_get_count(g_disk_choose_state.list_handle)) {
    g_disk_choose_state.act_file = 0;
  }
  g_disk_choose_state.first_file =
      (g_disk_choose_state.act_file > files_in_screen / 2)
          ? g_disk_choose_state.act_file - (files_in_screen / 2)
          : 0;
  g_disk_choose_state.active = true;
  g_disk_choose_state.finished = false;
  g_disk_choose_state.cancelled = false;
  g_disk_choose_state.index_file_out = &index_file;

  const AppMode old_mode = system_state.mode;
  system_state.mode = app_mode_disk_choose;

  while (g_disk_choose_state.active) {
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT) {
        SDL_PushEvent(&event);
        system_state.mode = app_mode_exit;
        g_disk_choose_state.active = false;
        g_disk_choose_state.cancelled = true;
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

  if (g_disk_choose_state.finished) {
    filename = g_disk_choose_state.result_filename;
    isdir = g_disk_choose_state.result_isdir;
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

  FileListGenerator* generator = file_browser_create_local_generator(
      incoming_dir.c_str(), supported_exts.data());
  if (generator == nullptr) {
    return false;
  }

  const bool result = choose_image_dialog(sx, sy, incoming_dir, slot, generator,
                                          filename, isdir, index_file);
  generator->destroy(generator);
  return result;
}
