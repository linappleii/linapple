#include "frontends/sdl3/DiskChoose.h"

#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/Video.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "frontends/common/FileBrowser.h"
#include "frontends/common/VideoStretch.h"
#include "frontends/sdl3/Frame.h"
#include "frontends/sdl3/SDL_Video.h"

using std::string;
using std::vector;

static constexpr int files_in_screen = 21;
static constexpr int key_delay = 25;
static constexpr int max_filename = 80;
static constexpr int normal_length = 60;

DiskChooseState_t g_diskChooseState;

void DiskChoose_Tick(SDL_Event* event) {
  if (!g_diskChooseState.active || !g_diskChooseState.list_handle) return;
  if (event->type != SDL_EVENT_KEY_DOWN) return;

  SDL_Keycode key = event->key.key;
  size_t list_count = file_browser_get_count(g_diskChooseState.list_handle);
  if (list_count == 0) return;

  if (key == SDLK_UP || key == SDLK_LEFT) {
    if (g_diskChooseState.act_file > 0) {
      g_diskChooseState.act_file--;
    }
    if (g_diskChooseState.act_file < g_diskChooseState.first_file) {
      g_diskChooseState.first_file = g_diskChooseState.act_file;
    }
  }

  if (key == SDLK_DOWN || key == SDLK_RIGHT) {
    if (g_diskChooseState.act_file < (list_count - 1)) {
      g_diskChooseState.act_file++;
    }
    if (g_diskChooseState.act_file >=
        (g_diskChooseState.first_file + files_in_screen)) {
      g_diskChooseState.first_file =
          g_diskChooseState.act_file - files_in_screen + 1;
    }
  }

  if (key == SDLK_PAGEUP) {
    if (g_diskChooseState.act_file <= files_in_screen) {
      g_diskChooseState.act_file = 0;
    } else {
      g_diskChooseState.act_file -= files_in_screen;
    }
    if (g_diskChooseState.act_file < g_diskChooseState.first_file) {
      g_diskChooseState.first_file = g_diskChooseState.act_file;
    }
  }

  if (key == SDLK_PAGEDOWN) {
    g_diskChooseState.act_file += files_in_screen;
    if (g_diskChooseState.act_file >= list_count) {
      g_diskChooseState.act_file = (list_count - 1);
    }
    if (g_diskChooseState.act_file >=
        (g_diskChooseState.first_file + files_in_screen)) {
      g_diskChooseState.first_file =
          g_diskChooseState.act_file - files_in_screen + 1;
    }
  }

  if (key == SDLK_RETURN) {
    const FileEntry_t* file_entry = file_browser_get_entry(
        g_diskChooseState.list_handle, g_diskChooseState.act_file);
    if (file_entry) {
      g_diskChooseState.result_filename = file_entry->name;
      g_diskChooseState.result_isdir = file_entry_is_dir_type(file_entry);
      if (g_diskChooseState.index_file_out) {
        *g_diskChooseState.index_file_out = g_diskChooseState.act_file;
      }
      g_diskChooseState.finished = true;
      g_diskChooseState.active = false;
    }
  }

  if (key == SDLK_ESCAPE) {
    g_diskChooseState.active = false;
    g_diskChooseState.cancelled = true;
  }

  if (key == SDLK_F12) {
    g_diskChooseState.active = false;
    g_diskChooseState.cancelled = true;
    g_state.mode = MODE_EXIT;
    SDL_Event qe{};
    qe.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&qe);
    return;
  }

  if (key == SDLK_HOME) {
    g_diskChooseState.act_file = 0;
    g_diskChooseState.first_file = 0;
  }

  if (key == SDLK_END) {
    g_diskChooseState.act_file = list_count - 1;
    if (g_diskChooseState.act_file <= files_in_screen - 1) {
      g_diskChooseState.first_file = 0;
    } else {
      g_diskChooseState.first_file =
          g_diskChooseState.act_file - files_in_screen + 1;
    }
  }

  // Jump to first file starting with the pressed character.
  {
    bool char_hit = false;
    if ((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z') ||
        (key >= '0' && key <= '9')) {
      char_hit = true;
    }
    if (char_hit) {
      for (size_t i = 0; i < list_count; ++i) {
        const FileEntry_t* entry =
            file_browser_get_entry(g_diskChooseState.list_handle, i);
        if (entry && strlen(entry->name) > 0) {
          if (toupper(entry->name[0]) == toupper(static_cast<char>(key))) {
            g_diskChooseState.act_file = i;
            if (g_diskChooseState.act_file < g_diskChooseState.first_file) {
              g_diskChooseState.first_file = g_diskChooseState.act_file;
            }
            if (g_diskChooseState.act_file >=
                (g_diskChooseState.first_file + files_in_screen)) {
              g_diskChooseState.first_file =
                  g_diskChooseState.act_file - files_in_screen + 1;
            }
            break;
          }
        }
      }
    }
  }
}

extern void frame_refresh();

void DiskChoose_Draw() {
  if (!g_diskChooseState.active) return;

  const float facx_f = static_cast<float>(g_state.screen_width) /
                       static_cast<float>(SCREEN_WIDTH);
  const float facy_f = static_cast<float>(g_state.screen_height) /
                       static_cast<float>(SCREEN_HEIGHT);
  const double facy = static_cast<double>(facy_f);
  const int sx = static_cast<int>(g_state.screen_width);

  // We assume ownership of g_video_draw_mutex is handled by the caller (main
  // loop or blocking proxy)
  VideoSurface_t vs_bg =
      sdl_surface_to_video_surface(g_diskChooseState.bg_screen);
  VideoSurface_t vs_screen = sdl_surface_to_video_surface(g_screen);

  video_soft_stretch(&vs_bg, nullptr, &vs_screen, nullptr);

  font_print_centered(
      sx / 2, static_cast<int>(5 * facy),
      g_diskChooseState.current_dir.substr(0, normal_length).c_str(),
      &vs_screen, 1.5f * facx_f, 1.3f * facy_f);

  if (g_diskChooseState.slot == 6) {
    font_print_centered(sx / 2, static_cast<int>(20 * facy),
                        "Choose image for floppy 140KB drive", &vs_screen,
                        1.0f * facx_f, 1.0f * facy_f);
  } else if (g_diskChooseState.slot == 7) {
    font_print_centered(sx / 2, static_cast<int>(20 * facy),
                        "Choose image for Hard Disk", &vs_screen, 1.0f * facx_f,
                        1.0f * facy_f);
  } else if (g_diskChooseState.slot == 5) {
    font_print_centered(sx / 2, static_cast<int>(20 * facy),
                        "Choose image for floppy 800KB drive", &vs_screen,
                        1.0f * facx_f, 1.0f * facy_f);
  } else if (g_diskChooseState.slot == 1) {
    font_print_centered(sx / 2, static_cast<int>(20 * facy),
                        "Select file name for saving snapshot", &vs_screen,
                        1.0f * facx_f, 1.0f * facy_f);
  } else if (g_diskChooseState.slot == 0) {
    font_print_centered(sx / 2, static_cast<int>(20 * facy),
                        "Select snapshot file name for loading", &vs_screen,
                        1.0f * facx_f, 1.0f * facy_f);
  }
  font_print_centered(sx / 2, static_cast<int>(30 * facy),
                      "Press ENTER to choose, or ESC to cancel", &vs_screen,
                      1.0f * facx_f, 1.0f * facy_f);

  int TOPX = static_cast<int>(45 * facy);
  size_t list_count =
      g_diskChooseState.list_handle
          ? file_browser_get_count(g_diskChooseState.list_handle)
          : 0;

  for (size_t j = 0; j < files_in_screen; ++j) {
    const size_t i = g_diskChooseState.first_file + j;
    if (i >= list_count) {
      break;
    }
    const FileEntry_t* file_entry =
        file_browser_get_entry(g_diskChooseState.list_handle, i);
    if (!file_entry) continue;

    const string file_name = file_entry->name;

    if (i == g_diskChooseState.act_file) {
      SDL_Rect r;
      r.x = 2;
      r.y = static_cast<int>(static_cast<double>(TOPX) +
                             static_cast<double>(j) * 15.0 * facy - 1.0);
      if (file_name.size() > max_filename) {
        r.w = static_cast<int>(static_cast<double>(max_filename) *
                               static_cast<double>(FONT_SIZE_X) * 1.0 *
                               static_cast<double>(facx_f));
      } else {
        r.w = static_cast<int>(static_cast<double>(file_name.size()) *
                               static_cast<double>(FONT_SIZE_X) * 1.0 *
                               static_cast<double>(facx_f));
      }
      r.h = static_cast<int>(9.0 * 1.0 * facy);
      SDL_FillSurfaceRect(
          g_screen, &r,
          SDL_MapRGB(SDL_GetPixelFormatDetails(g_screen->format),
                     SDL_GetSurfacePalette(g_screen), 64, 128, 190));
    }

    char type_size_str[32] = {};
    FileEntry_FormatTypeOrSize(file_entry, type_size_str,
                               sizeof(type_size_str));

    font_print(4,
               static_cast<int>(static_cast<double>(TOPX) +
                                static_cast<double>(j) * 15.0 * facy),
               file_name.substr(0, max_filename).c_str(), &vs_screen,
               1.0f * facx_f, 1.0f * facy_f);
    font_print_right(sx - static_cast<int>(8.0 * static_cast<double>(facx_f)),
                     static_cast<int>(static_cast<double>(TOPX) +
                                      static_cast<double>(j) * 15.0 * facy),
                     type_size_str, &vs_screen, 1.0f * facx_f, 1.0f * facy_f);
  }

  rectangle(&vs_screen, 0, TOPX - 5, sx - 1, static_cast<int>(320.0 * facy),
            RGB(255, 255, 255));
  rectangle(&vs_screen, static_cast<int>(480.0 * static_cast<double>(facx_f)),
            TOPX - 5, 0, static_cast<int>(320.0 * facy), RGB(255, 255, 255));

  frame_refresh();
}

auto choose_image_dialog(int sx, int sy, const string& dir, int slot,
                         FileListGenerator_t* file_list_generator,
                         std::string& filename, bool& isdir, size_t& index_file)
    -> bool {
  (void)sy;
  const double facx = static_cast<double>(g_state.screen_width) /
                      static_cast<double>(SCREEN_WIDTH);
  const double facy = static_cast<double>(g_state.screen_height) /
                      static_cast<double>(SCREEN_HEIGHT);

  if (font_sfc == nullptr) {
    if (!fonts_initialization()) {
      return false;
    }
  }

  // Claim ownership of video buffer for modal rendering.
  g_video_draw_mutex.lock();

  VideoSurface_t* tempSurface = nullptr;
  if (!g_window_resized) {
    if (g_state.mode == MODE_LOGO) {
      tempSurface = g_logo_bitmap;
    } else {
      tempSurface = g_device_bitmap;
    }
  } else {
    tempSurface = g_origscreen;
  }

  static VideoSurface_t vs_screen;
  if (tempSurface == nullptr) {
    vs_screen = sdl_surface_to_video_surface(g_screen);
    tempSurface = &vs_screen;
  }

  g_diskChooseState.bg_screen = SDL_CreateSurface(
      tempSurface->w, tempSurface->h, SDL_PIXELFORMAT_ARGB8888);

  VideoSurface_t vs_bg =
      sdl_surface_to_video_surface(g_diskChooseState.bg_screen);
  VideoSurface_t vs_actual_screen = sdl_surface_to_video_surface(g_screen);

  // Capture original g_screen
  video_soft_stretch(tempSurface, nullptr, &vs_bg, nullptr);

  // Blur the background by downscaling and upscaling
  // We use a small temporary surface (1/16 size) to create a pixelated blur
  // effect
  SDL_Surface* blur_temp = SDL_CreateSurface(
      tempSurface->w / 16, tempSurface->h / 16, SDL_PIXELFORMAT_ARGB8888);
  if (blur_temp) {
    VideoSurface_t vs_blur = sdl_surface_to_video_surface(blur_temp);
    video_soft_stretch(&vs_bg, nullptr, &vs_blur, nullptr);  // Downscale
    video_soft_stretch(&vs_blur, nullptr, &vs_bg, nullptr);  // Upscale back
    SDL_DestroySurface(blur_temp);
  }

  // Dim the background using SDL blending for better text readability
  SDL_Surface* dim_surface = SDL_CreateSurface(tempSurface->w, tempSurface->h,
                                               SDL_PIXELFORMAT_ARGB8888);
  if (dim_surface) {
    Uint32 dim_color =
        SDL_MapRGBA(SDL_GetPixelFormatDetails(dim_surface->format),
                    SDL_GetSurfacePalette(dim_surface), 0, 0, 0, 160);
    SDL_FillSurfaceRect(dim_surface, nullptr, dim_color);
    SDL_SetSurfaceBlendMode(dim_surface, SDL_BLENDMODE_BLEND);
    SDL_BlitSurface(dim_surface, nullptr, g_diskChooseState.bg_screen, nullptr);
    SDL_DestroySurface(dim_surface);
  }

  video_soft_stretch(&vs_bg, nullptr, &vs_actual_screen, nullptr);

  font_print_centered(sx / 2, 5 * facy, dir.substr(0, normal_length).c_str(),
                      &vs_actual_screen, 1.5 * facx, 1.3 * facy);
  font_print_centered(
      sx / 2, 20 * facy,
      file_list_generator->get_starting_message(file_list_generator),
      &vs_actual_screen, 1 * facx, 1 * facy);
  frame_refresh();
  g_video_draw_mutex.unlock();

  g_diskChooseState.list_handle =
      file_list_generator->generate_file_list(file_list_generator);
  if (!g_diskChooseState.list_handle ||
      file_browser_get_count(g_diskChooseState.list_handle) < 1) {
    printf("%s\n",
           file_list_generator->get_failure_message(file_list_generator));

    g_video_draw_mutex.lock();
    font_print_centered(sx / 2, 30 * facy, "Failure. Press any key!",
                        &vs_actual_screen, 1.4 * facx, 1.1 * facy);
    frame_refresh();
    g_video_draw_mutex.unlock();

    SDL_Delay(key_delay);
    SDL_Event event;
    bool waiting = true;
    while (waiting) {
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_KEY_DOWN) {
          if (event.key.key == SDLK_F12) {
            g_state.mode = MODE_EXIT;
            SDL_Event qe{};
            qe.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&qe);
          }
          waiting = false;
          break;
        }
        if (event.type == SDL_EVENT_QUIT ||
            event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
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
    SDL_DestroySurface(g_diskChooseState.bg_screen);
    g_diskChooseState.bg_screen = nullptr;
    if (g_diskChooseState.list_handle) {
      FileBrowser_FreeList(g_diskChooseState.list_handle);
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
  if (g_diskChooseState.act_file <= files_in_screen / 2) {
    g_diskChooseState.first_file = 0;
  } else {
    g_diskChooseState.first_file =
        g_diskChooseState.act_file - (files_in_screen / 2);
  }
  g_diskChooseState.active = true;
  g_diskChooseState.finished = false;
  g_diskChooseState.cancelled = false;
  g_diskChooseState.index_file_out = &index_file;

  AppMode_t old_mode = g_state.mode;
  g_state.mode = MODE_DISK_CHOOSE;

  // Run a blocking input/render loop to simplify state management for modal
  // dialogs.
  while (g_diskChooseState.active) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT ||
          event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        SDL_PushEvent(&event);
        g_state.mode = MODE_EXIT;
        g_diskChooseState.active = false;
        g_diskChooseState.cancelled = true;
        break;
      }
      if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F12) {
        g_state.mode = MODE_EXIT;
        g_diskChooseState.active = false;
        g_diskChooseState.cancelled = true;
        SDL_Event qe{};
        qe.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&qe);
        break;
      }
      DiskChoose_Tick(&event);
    }
    g_video_draw_mutex.lock();
    DiskChoose_Draw();
    g_video_draw_mutex.unlock();
    SDL_Delay(10);
  }

  if (g_state.mode != MODE_EXIT) {
    g_state.mode = old_mode;
  }
  SDL_DestroySurface(g_diskChooseState.bg_screen);
  g_diskChooseState.bg_screen = nullptr;

  if (g_diskChooseState.list_handle) {
    FileBrowser_FreeList(g_diskChooseState.list_handle);
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
  char supported_exts[256] = {};
  size_t exts_size = sizeof(supported_exts);
  if (slot == 7) {
    (void)peripheral_query(7, harddisk_cmd_get_supported_extensions,
                           supported_exts, &exts_size);
  } else {
    (void)peripheral_query(slot, disk_cmd_get_supported_extensions,
                           supported_exts, &exts_size);
  }

  FileListGenerator_t* generator =
      file_browser_create_local_generator(incoming_dir.c_str(), supported_exts);
  if (!generator) return false;

  bool result = choose_image_dialog(sx, sy, incoming_dir, slot, generator,
                                    filename, isdir, index_file);
  generator->destroy(generator);
  return result;
}
