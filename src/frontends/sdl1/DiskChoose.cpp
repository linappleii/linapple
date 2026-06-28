#include "frontends/sdl1/DiskChoose.h"

#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/Video.h"
#include "frontends/common/VideoStretch.h"
#include "core/Common.h"
#include "frontends/common/FileBrowser.h"
#include "frontends/sdl1/Frame.h"
#include "frontends/sdl1/SDL_Video.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,
// bugprone-easily-swappable-parameters,
// cppcoreguidelines-narrowing-conversions,
// cppcoreguidelines-pro-type-const-cast,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-pro-type-member-init, modernize-use-auto,
// bugprone-switch-missing-default-case, bugprone-branch-clone) Justification:
// Immediate mode GUI layout code relies heavily on numeric literals for pixel
// coordinates, colors, and scaling factors. Extracting all to constants reduces
// readability. API signatures dictate parameter types causing swappable
// warnings.

using std::string;
using std::vector;

static constexpr int FILES_IN_SCREEN = 21;
static constexpr int KEY_DELAY = 25;
static constexpr int MAX_FILENAME = 80;
static constexpr int NORMAL_LENGTH = 60;
static constexpr int FONT_CHAR_WIDTH = 8;
static constexpr int RECT_WIDTH = 320;
static constexpr int RECT_MARGIN = 5;

DiskChooseState g_diskChooseState;

void DiskChoose_Tick(SDL_Event* event) {
  if (g_diskChooseState.active == false ||
      g_diskChooseState.list_handle == nullptr)
    return;
  if (event->type != SDL_KEYDOWN) return;

  SDLKey key = event->key.keysym.sym;
  size_t list_count = FileBrowser_GetCount(g_diskChooseState.list_handle);
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
        (g_diskChooseState.first_file + FILES_IN_SCREEN)) {
      g_diskChooseState.first_file =
          g_diskChooseState.act_file - FILES_IN_SCREEN + 1;
    }
  }

  if (key == SDLK_PAGEUP) {
    if (g_diskChooseState.act_file <= FILES_IN_SCREEN) {
      g_diskChooseState.act_file = 0;
    } else {
      g_diskChooseState.act_file -= FILES_IN_SCREEN;
    }
    if (g_diskChooseState.act_file < g_diskChooseState.first_file) {
      g_diskChooseState.first_file = g_diskChooseState.act_file;
    }
  }

  if (key == SDLK_PAGEDOWN) {
    g_diskChooseState.act_file += FILES_IN_SCREEN;
    if (g_diskChooseState.act_file >= list_count) {
      g_diskChooseState.act_file = (list_count - 1);
    }
    if (g_diskChooseState.act_file >=
        (g_diskChooseState.first_file + FILES_IN_SCREEN)) {
      g_diskChooseState.first_file =
          g_diskChooseState.act_file - FILES_IN_SCREEN + 1;
    }
  }

  if (key == SDLK_RETURN) {
    const file_entry_t* file_entry = FileBrowser_GetEntry(
        g_diskChooseState.list_handle, g_diskChooseState.act_file);
    if (file_entry != nullptr) {
      g_diskChooseState.result_filename = file_entry->name;
      g_diskChooseState.result_isdir = FileEntry_IsDirType(file_entry);
      if (g_diskChooseState.p_index_file != nullptr) {
        *g_diskChooseState.p_index_file = g_diskChooseState.act_file;
      }
      g_diskChooseState.finished = true;
      g_diskChooseState.active = false;
    }
  }

  if (key == SDLK_ESCAPE) {
    g_diskChooseState.active = false;
    g_diskChooseState.cancelled = true;
  }

  if (key == SDLK_HOME) {
    g_diskChooseState.act_file = 0;
    g_diskChooseState.first_file = 0;
  }

  if (key == SDLK_END) {
    g_diskChooseState.act_file = list_count - 1;
    if (g_diskChooseState.act_file <= FILES_IN_SCREEN - 1) {
      g_diskChooseState.first_file = 0;
    } else {
      g_diskChooseState.first_file =
          g_diskChooseState.act_file - FILES_IN_SCREEN + 1;
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
        const file_entry_t* entry =
            FileBrowser_GetEntry(g_diskChooseState.list_handle, i);
        if (entry != nullptr && strlen(entry->name) > 0) {
          if (toupper(entry->name[0]) == toupper(static_cast<char>(key))) {
            g_diskChooseState.act_file = i;
            if (g_diskChooseState.act_file < g_diskChooseState.first_file) {
              g_diskChooseState.first_file = g_diskChooseState.act_file;
            }
            if (g_diskChooseState.act_file >=
                (g_diskChooseState.first_file + FILES_IN_SCREEN)) {
              g_diskChooseState.first_file =
                  g_diskChooseState.act_file - FILES_IN_SCREEN + 1;
            }
            break;
          }
        }
      }
    }
  }
}

extern void FrameRefresh();

void DiskChoose_Draw() {
  if (g_diskChooseState.active == false) return;

  const float facx_f = static_cast<float>(g_state.ScreenWidth) /
                       static_cast<float>(SCREEN_WIDTH);
  const float facy_f = static_cast<float>(g_state.ScreenHeight) /
                       static_cast<float>(SCREEN_HEIGHT);
  const auto facy = static_cast<double>(facy_f);
  const int sx = static_cast<int>(g_state.ScreenWidth);

  // We assume ownership of g_video_draw_mutex is handled by the caller (main
  // loop or blocking proxy)
  VideoSurface vs_bg = SDLSurfaceToVideoSurface(g_diskChooseState.bg_screen);
  VideoSurface vs_screen = SDLSurfaceToVideoSurface(g_screen);

  VideoSoftStretch(&vs_bg, nullptr, &vs_screen, nullptr);

  font_print_centered(
      sx / 2, static_cast<int>(5 * facy),
      g_diskChooseState.current_dir.substr(0, NORMAL_LENGTH).c_str(),
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
  size_t list_count = g_diskChooseState.list_handle != nullptr
                          ? FileBrowser_GetCount(g_diskChooseState.list_handle)
                          : 0;

  for (size_t j = 0; j < FILES_IN_SCREEN; ++j) {
    const size_t i = g_diskChooseState.first_file + j;
    if (i >= list_count) {
      break;
    }
    const file_entry_t* file_entry =
        FileBrowser_GetEntry(g_diskChooseState.list_handle, i);
    if (file_entry == nullptr) continue;

    const string file_name = file_entry->name;

    if (i == g_diskChooseState.act_file) {
      SDL_Rect r = {};
      r.x = 2;
      r.y = static_cast<int>(static_cast<double>(TOPX) +
                             static_cast<double>(j) * 15.0 * facy - 1.0);
      if (file_name.size() > MAX_FILENAME) {
        r.w = static_cast<int>(static_cast<double>(MAX_FILENAME) *
                               static_cast<double>(FONT_CHAR_WIDTH) * 1.0 *
                               static_cast<double>(facx_f));
      } else {
        r.w = static_cast<int>(static_cast<double>(file_name.size()) *
                               static_cast<double>(FONT_CHAR_WIDTH) * 1.0 *
                               static_cast<double>(facx_f));
      }
      r.h = static_cast<int>(9.0 * 1.0 * facy);
      SDL_FillRect(g_screen, &r, SDL_MapRGB(g_screen->format, 64, 128, 190));
    }

    std::array<char, 32> type_size_str = {};
    FileEntry_FormatTypeOrSize(file_entry, type_size_str.data(),
                               type_size_str.size());

    font_print(4,
               static_cast<int>(static_cast<double>(TOPX) +
                                static_cast<double>(j) * 15.0 * facy),
               file_name.substr(0, MAX_FILENAME).c_str(), &vs_screen,
               1.0f * facx_f, 1.0f * facy_f);
    font_print(sx - static_cast<int>(70.0 * static_cast<double>(facx_f)),
               static_cast<int>(static_cast<double>(TOPX) +
                                static_cast<double>(j) * 15.0 * facy),
               type_size_str.data(), &vs_screen, 1.0f * facx_f, 1.0f * facy_f);
  }

  rectangle(&vs_screen, 0, TOPX - RECT_MARGIN, sx,
            static_cast<int>(RECT_WIDTH * facy), RGB(255, 255, 255));
  rectangle(&vs_screen, static_cast<int>(480.0 * static_cast<double>(facx_f)),
            TOPX - RECT_MARGIN, 0, static_cast<int>(RECT_WIDTH * facy),
            RGB(255, 255, 255));

  FrameRefresh();
}

auto ChooseImageDialog(int sx, int sy, const string& dir, int slot,
                       FileListGenerator_t* file_list_generator,
                       std::string& filename, bool& isdir, size_t& index_file)
    -> bool {
  (void)sy;
  const auto facx = static_cast<double>(g_state.ScreenWidth) /
                    static_cast<double>(SCREEN_WIDTH);
  const auto facy = static_cast<double>(g_state.ScreenHeight) /
                    static_cast<double>(SCREEN_HEIGHT);

  if (font_sfc == nullptr) {
    if (fonts_initialization() == false) {
      return false;
    }
  }

  // Claim ownership of video buffer for modal rendering.
  g_video_draw_mutex.lock();

  VideoSurface* tempSurface = nullptr;
  if (g_WindowResized == false) {
    if (g_state.mode == MODE_LOGO) {
      tempSurface = g_hLogoBitmap;
    } else {
      tempSurface = g_hDeviceBitmap;
    }
  } else {
    tempSurface = g_origscreen;
  }

  static VideoSurface vs_screen;
  if (tempSurface == nullptr) {
    vs_screen = SDLSurfaceToVideoSurface(g_screen);
    tempSurface = &vs_screen;
  }

  g_diskChooseState.bg_screen =
      SDL_CreateRGBSurface(SDL_HWSURFACE, tempSurface->w, tempSurface->h, 32,
                           0x00FF0000, 0x0000FF00, 0x000000FF, 0);

  VideoSurface vs_bg = SDLSurfaceToVideoSurface(g_diskChooseState.bg_screen);
  VideoSurface vs_actual_screen = SDLSurfaceToVideoSurface(g_screen);

  // Capture original screen
  VideoSoftStretch(tempSurface, nullptr, &vs_bg, nullptr);

  // Blur the background by downscaling and upscaling
  // We use a small temporary surface (1/16 size) to create a pixelated blur
  // effect
  SDL_Surface* blur_temp =
      SDL_CreateRGBSurface(0, tempSurface->w / 16, tempSurface->h / 16, 32,
                           0x00FF0000, 0x0000FF00, 0x000000FF, 0);
  if (blur_temp != nullptr) {
    VideoSurface vs_blur = SDLSurfaceToVideoSurface(blur_temp);
    VideoSoftStretch(&vs_bg, nullptr, &vs_blur, nullptr);  // Downscale
    VideoSoftStretch(&vs_blur, nullptr, &vs_bg, nullptr);  // Upscale back
    SDL_FreeSurface(blur_temp);
  }

  // Dim the background using SDL blending for better text readability
  SDL_Surface* dim_surface =
      SDL_CreateRGBSurface(0, tempSurface->w, tempSurface->h, 32, 0x00FF0000,
                           0x0000FF00, 0x000000FF, 0);
  if (dim_surface != nullptr) {
    Uint32 dim_color = SDL_MapRGBA(dim_surface->format, 0, 0, 0, 160);
    SDL_FillRect(dim_surface, nullptr, dim_color);
    SDL_SetAlpha(dim_surface, SDL_SRCALPHA, 160);
    SDL_BlitSurface(dim_surface, nullptr, g_diskChooseState.bg_screen, nullptr);
    SDL_FreeSurface(dim_surface);
  }

  VideoSoftStretch(&vs_bg, nullptr, &vs_actual_screen, nullptr);

  font_print_centered(sx / 2, static_cast<int>(5 * facy),
                      dir.substr(0, NORMAL_LENGTH).c_str(), &vs_actual_screen,
                      1.5 * facx, 1.3 * facy);
  font_print_centered(
      sx / 2, static_cast<int>(20 * facy),
      file_list_generator->get_starting_message(file_list_generator),
      &vs_actual_screen, 1 * facx, 1 * facy);
  FrameRefresh();

  g_diskChooseState.list_handle =
      file_list_generator->generate_file_list(file_list_generator);
  if (g_diskChooseState.list_handle == nullptr ||
      FileBrowser_GetCount(g_diskChooseState.list_handle) < 1) {
    printf("%s\n",
           file_list_generator->get_failure_message(file_list_generator));

    font_print_centered(sx / 2, static_cast<int>(30 * facy),
                        "Failure. Press any key!", &vs_actual_screen,
                        1.4 * facx, 1.1 * facy);
    FrameRefresh();

    g_video_draw_mutex.unlock();
    SDL_Delay(KEY_DELAY);
    SDL_Event event = {};

    event.type = SDL_QUIT;
    while (event.type != SDL_KEYDOWN) {
      SDL_Delay(100);
      SDL_PollEvent(&event);
    }
    SDL_FreeSurface(g_diskChooseState.bg_screen);
    g_diskChooseState.bg_screen = nullptr;
    if (g_diskChooseState.list_handle != nullptr) {
      FileBrowser_FreeList(g_diskChooseState.list_handle);
      g_diskChooseState.list_handle = nullptr;
    }
    return false;
  }

  g_diskChooseState.slot = slot;
  g_diskChooseState.current_dir = dir;
  g_diskChooseState.act_file = index_file;
  if (g_diskChooseState.act_file >=
      FileBrowser_GetCount(g_diskChooseState.list_handle)) {
    g_diskChooseState.act_file = 0;
  }
  if (g_diskChooseState.act_file <= static_cast<size_t>(FILES_IN_SCREEN / 2)) {
    g_diskChooseState.first_file = 0;
  } else {
    g_diskChooseState.first_file =
        g_diskChooseState.act_file - static_cast<size_t>(FILES_IN_SCREEN / 2);
  }
  g_diskChooseState.active = true;
  g_diskChooseState.finished = false;
  g_diskChooseState.cancelled = false;
  g_diskChooseState.p_index_file = &index_file;

  AppMode_e old_mode = g_state.mode;
  g_state.mode = MODE_DISK_CHOOSE;

  g_video_draw_mutex.unlock();

  // Run a blocking input/render loop to simplify state management for modal
  // dialogs.
  while (g_diskChooseState.active) {
    SDL_Event event = {};
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT) {
        g_state.mode = MODE_EXIT;
        g_diskChooseState.active = false;
        break;
      }
      DiskChoose_Tick(&event);
    }
    g_video_draw_mutex.lock();
    DiskChoose_Draw();
    g_video_draw_mutex.unlock();
    SDL_Delay(10);
  }

  g_state.mode = old_mode;
  SDL_FreeSurface(g_diskChooseState.bg_screen);
  g_diskChooseState.bg_screen = nullptr;

  if (g_diskChooseState.list_handle != nullptr) {
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

auto ChooseAnImage(int sx, int sy, const std::string& incoming_dir, int slot,
                   std::string& filename, bool& isdir, size_t& index_file)
    -> bool {
  FileListGenerator_t* generator =
      FileBrowser_CreateLocalGenerator(incoming_dir.c_str());
  if (generator == nullptr) return false;

  bool result = ChooseImageDialog(sx, sy, incoming_dir, slot, generator,
                                  filename, isdir, index_file);
  generator->destroy(generator);
  return result;
}
