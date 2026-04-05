/*
////////////////////////////////////////////////////////////////////////////
////////////  Choose disk image for given slot number? ////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
*/
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include "ctype.h"


#include "stdafx.h"

#include "file_entry.h"

#include "Video.h" // for contention avoidance w/video thread
#include "DiskChoose.h"
#include <errno.h>
#include <pthread.h>

#include <iostream>
#include <vector>

// how many file names we are able to see at once!
#define FILES_IN_SCREEN    21

// delay after key pressed (in milliseconds??)
#define KEY_DELAY    25

#define MAX_FILENAME  80

int getstat(const char *catalog, const char *fname, uintmax_t *size)
{
  // gets file status and returns: 0 - special or error, 1 - file is a directory, 2 - file is a normal file
  // In: catalog - working directory, fname - file name
  struct stat info;
  char tempname[MAX_PATH];

  snprintf(tempname, MAX_PATH, "%s/%s", catalog, fname);
  if (stat(tempname, &info) == -1) {
    return 0;
  }
  if (S_ISDIR(info.st_mode)) {
    return 1;
  }
  if (S_ISREG(info.st_mode)) {
    if (size != NULL) {
      *size = info.st_size / 1024;
    }
    return 2;
  }

  return 0;
}

int compareNames(const void *const A, const void *const B)
{
    return strcasecmp((*(struct dirent **) A)->d_name, (*(struct dirent **) B)->d_name);
}

// get_sorted_directory
// Read and sort the current directory, subdirectories at top followed by
//  sorted files.
// Entry: incoming_dir path
//        file list
//        size list
// Exit: true == success
bool get_sorted_directory(const char *incoming_dir, vector<file_entry_t> &file_list)
{
  DIR *dp;
  struct dirent *entry;

  dp = opendir(incoming_dir);
  if (dp == NULL)
  {
      fprintf(stderr, "cannot open `%s'\n", incoming_dir);
      return false;
  }

  while ((entry = readdir(dp)) != NULL) {
    const char *file_name = entry->d_name;
    const size_t name_length = strlen(file_name);

    if (name_length < 1 || file_name[0] == '.')
      continue;

    uintmax_t fsize = 0;
    const int what = getstat(incoming_dir, file_name, &fsize);

    switch (what) {
    case 1:
      file_list.push_back({ file_name, file_entry_t::DIR, 0 });
      continue;

    case 2:
      file_list.push_back({ file_name, file_entry_t::FILE, fsize*1024});
      continue;

    default:
      ;
    }
  }
  closedir(dp);

  std::sort(file_list.begin(), file_list.end());

  return true;
}


struct disk_file_list_generator_t : public file_list_generator_t {
  disk_file_list_generator_t(const std::string& dir) :
    directory(dir)
  {}

  const std::vector<file_entry_t> generate_file_list();

  const std::string get_starting_message() {
    return "Reading directory listing...";
  }

  const std::string get_failure_message() {
    return failure_message;
  }

private:
  std::string directory;
  std::string failure_message = "(success)";
};


const std::vector<file_entry_t> disk_file_list_generator_t::generate_file_list()
{
  std::vector<file_entry_t> file_list;

  // Forcibly add ".." as first entry to navigate upward
  // to parent directory.
  if (directory != "/") {
    file_list.push_back({ "..", file_entry_t::UP, 0});
  }

  if (get_sorted_directory(directory.c_str(), file_list)) {
    return file_list;
  }

  failure_message = "Failed to list directory: " + directory;
  file_list.clear();
  return file_list;
}


DiskChooseState g_diskChooseState;

void DiskChoose_Tick(SDL_Event* event)
{
  if (!g_diskChooseState.active) return;
  if (event->type != SDL_EVENT_KEY_DOWN) return;

  SDL_Keycode key = event->key.key;

  if (key == SDLK_UP || key == SDLK_LEFT) {
    if (g_diskChooseState.act_file > 0)
      g_diskChooseState.act_file--;
    if (g_diskChooseState.act_file < g_diskChooseState.first_file)
      g_diskChooseState.first_file = g_diskChooseState.act_file;
  }

  if (key == SDLK_DOWN || key == SDLK_RIGHT) {
    if (g_diskChooseState.act_file < (g_diskChooseState.file_list.size() - 1))
      g_diskChooseState.act_file++;
    if (g_diskChooseState.act_file >= (g_diskChooseState.first_file + FILES_IN_SCREEN))
      g_diskChooseState.first_file = g_diskChooseState.act_file - FILES_IN_SCREEN + 1;
  }

  if (key == SDLK_PAGEUP) {
    if (g_diskChooseState.act_file <= FILES_IN_SCREEN) {
      g_diskChooseState.act_file = 0;
    } else {
      g_diskChooseState.act_file -= FILES_IN_SCREEN;
    }
    if (g_diskChooseState.act_file < g_diskChooseState.first_file)
      g_diskChooseState.first_file = g_diskChooseState.act_file;
  }

  if (key == SDLK_PAGEDOWN) {
    g_diskChooseState.act_file += FILES_IN_SCREEN;
    if (g_diskChooseState.act_file >= g_diskChooseState.file_list.size())
      g_diskChooseState.act_file = (g_diskChooseState.file_list.size() - 1);
    if (g_diskChooseState.act_file >= (g_diskChooseState.first_file + FILES_IN_SCREEN))
      g_diskChooseState.first_file = g_diskChooseState.act_file - FILES_IN_SCREEN + 1;
  }

  if (key == SDLK_RETURN) {
    const file_entry_t& file_entry = g_diskChooseState.file_list[g_diskChooseState.act_file];
    g_diskChooseState.result_filename = file_entry.name;
    g_diskChooseState.result_isdir = file_entry.is_dir_type();
    if (g_diskChooseState.p_index_file) {
      *g_diskChooseState.p_index_file = g_diskChooseState.act_file;
    }
    g_diskChooseState.finished = true;
    g_diskChooseState.active = false;
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
    g_diskChooseState.act_file = g_diskChooseState.file_list.size() - 1;
    if (g_diskChooseState.act_file <= FILES_IN_SCREEN - 1) {
      g_diskChooseState.first_file = 0;
    } else {
      g_diskChooseState.first_file = g_diskChooseState.act_file - FILES_IN_SCREEN + 1;
    }
  }

  // Check for A-Z, a-z, 0-9 and jump to first file starting therewith
  {
    bool char_hit = false;
    if ((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z') || (key >= '0' && key <= '9')) {
      char_hit = true;
    }
    if (char_hit) {
      for (size_t i = 0; i < g_diskChooseState.file_list.size(); ++i) {
        if (g_diskChooseState.file_list[i].name.size() > 0) {
          if (toupper(g_diskChooseState.file_list[i].name[0]) == toupper((char) key)) {
            g_diskChooseState.act_file = i;
            if (g_diskChooseState.act_file < g_diskChooseState.first_file) {
              g_diskChooseState.first_file = g_diskChooseState.act_file;
            }
            if (g_diskChooseState.act_file >= (g_diskChooseState.first_file + FILES_IN_SCREEN)) {
              g_diskChooseState.first_file = g_diskChooseState.act_file - FILES_IN_SCREEN + 1;
            }
            break;
          }
        }
      }
    }
  }
}

void DiskChoose_Draw()
{
  if (!g_diskChooseState.active) return;

  const double facx = double(g_state.ScreenWidth) / double(SCREEN_WIDTH);
  const double facy = double(g_state.ScreenHeight) / double(SCREEN_HEIGHT);
  const int sx = g_state.ScreenWidth;

  // We assume ownership of video_draw_mutex is handled by the caller (main loop or blocking proxy)

  VideoSurface vs_bg = SDLSurfaceToVideoSurface(g_diskChooseState.bg_screen);
  VideoSurface vs_screen = SDLSurfaceToVideoSurface(screen);

  VideoSoftStretch(&vs_bg, NULL, &vs_screen, NULL);
  
#define  NORMAL_LENGTH 60
  font_print_centered(sx / 2, 5 * facy, g_diskChooseState.current_dir.substr(0, NORMAL_LENGTH).c_str(), &vs_screen, 1.5 * facx, 1.3 * facy);
  
  if (g_diskChooseState.slot == 6) {
    font_print_centered(sx / 2, 20 * facy, "Choose image for floppy 140KB drive", &vs_screen, 1 * facx, 1 * facy);
  } else if (g_diskChooseState.slot == 7) {
    font_print_centered(sx / 2, 20 * facy, "Choose image for Hard Disk", &vs_screen, 1 * facx, 1 * facy);
  } else if (g_diskChooseState.slot == 5) {
    font_print_centered(sx / 2, 20 * facy, "Choose image for floppy 800KB drive", &vs_screen, 1 * facx, 1 * facy);
  } else if (g_diskChooseState.slot == 1) {
    font_print_centered(sx / 2, 20 * facy, "Select file name for saving snapshot", &vs_screen, 1 * facx, 1 * facy);
  } else if (g_diskChooseState.slot == 0) {
    font_print_centered(sx / 2, 20 * facy, "Select snapshot file name for loading", &vs_screen, 1 * facx, 1 * facy);
  }
  font_print_centered(sx / 2, 30 * facy, "Press ENTER to choose, or ESC to cancel", &vs_screen, 1.0 * facx, 1.0 * facy);

  int TOPX = int(45 * facy);

  for (size_t j = 0; j < FILES_IN_SCREEN; ++j) {
    const size_t i = g_diskChooseState.first_file + j;
    if (i >= g_diskChooseState.file_list.size()) {
      break;
    }
    const file_entry_t file_entry = g_diskChooseState.file_list[i];
    const string& file_name = file_entry.name;

    if (i == g_diskChooseState.act_file) {
      SDL_Rect r;
      r.x = 2;
      r.y = TOPX + j * 15 * facy - 1;
      if (file_name.size() > MAX_FILENAME) {
        r.w = MAX_FILENAME * FONT_SIZE_X * 1.0 * facx;
      } else {
        r.w = file_name.size() * FONT_SIZE_X * 1.0 * facx;
      }
      r.h = 9 * 1.0 * facy;
      SDL_FillSurfaceRect(screen, &r, SDL_MapRGB(SDL_GetPixelFormatDetails(screen->format), SDL_GetSurfacePalette(screen), 64, 128, 190));
    }

    font_print(4, TOPX + j * 15 * facy, file_name.substr(0, MAX_FILENAME).c_str(), &vs_screen, 1.0 * facx, 1.0 * facy);
    font_print(sx - 70 * facx, TOPX + j * 15 * facy, file_entry.type_or_size_as_string().c_str(), &vs_screen, 1.0 * facx,
               1.0 * facy);
  }

  rectangle(&vs_screen, 0, TOPX - 5, sx, 320 * facy, RGB(255, 255, 255));
  rectangle(&vs_screen, 480 * facx, TOPX - 5, 0, 320 * facy, RGB(255, 255, 255));

  DrawFrameWindow();
}

bool ChooseAnImage(int sx, int sy, const std::string& incoming_dir, int slot,
                   std::string& filename, bool& isdir, size_t& index_file)
{
  disk_file_list_generator_t file_list_generator(incoming_dir);
  return ChooseImageDialog(sx, sy, incoming_dir, slot, &file_list_generator,
                           filename, isdir, index_file);
}

bool ChooseImageDialog(int sx, int sy, const string& dir, int slot, file_list_generator_t* file_list_generator,
                       std::string& filename, bool& isdir, size_t& index_file)
{
  (void)sy;
  const double facx = double(g_state.ScreenWidth) / double(SCREEN_WIDTH);
  const double facy = double(g_state.ScreenHeight) / double(SCREEN_HEIGHT);

  if (font_sfc == NULL) {
    if (!fonts_initialization()) {
      return false;
    }
  }

  // Wait for video refresh and claim ownership
  pthread_mutex_lock(&video_draw_mutex);

  VideoSurface *tempSurface = NULL;
  if (!g_WindowResized) {
    if (g_state.mode == MODE_LOGO) {
      tempSurface = g_hLogoBitmap;
    } else {
      tempSurface = g_hDeviceBitmap;
    }
  } else
    tempSurface = g_origscreen;

  static VideoSurface vs_screen;
  if (tempSurface == NULL) {
    vs_screen = SDLSurfaceToVideoSurface(screen);
    tempSurface = &vs_screen;
  }

  g_diskChooseState.bg_screen = SDL_CreateSurface(tempSurface->w, tempSurface->h, SDL_PIXELFORMAT_XRGB8888);
  
  VideoSurface vs_bg = SDLSurfaceToVideoSurface(g_diskChooseState.bg_screen);
  VideoSurface vs_actual_screen = SDLSurfaceToVideoSurface(screen);

  surface_fader(&vs_bg, 0.2F, 0.2F, 0.2F, -1, NULL);
  VideoSoftStretch(tempSurface, NULL, &vs_bg, NULL);
  VideoSoftStretch(&vs_bg, NULL, &vs_actual_screen, NULL);

  font_print_centered(sx / 2, 5 * facy, dir.substr(0, NORMAL_LENGTH).c_str(), &vs_actual_screen, 1.5 * facx, 1.3 * facy);
  font_print_centered(sx / 2, 20 * facy, file_list_generator->get_starting_message().c_str(), &vs_actual_screen, 1 * facx, 1 * facy);
  DrawFrameWindow();

  g_diskChooseState.file_list = file_list_generator->generate_file_list();
  if (g_diskChooseState.file_list.size() < 1) {
    printf("%s\n", file_list_generator->get_failure_message().c_str());

    font_print_centered(sx / 2, 30 * facy, "Failure. Press any key!", &vs_actual_screen, 1.4 * facx, 1.1 * facy);
    DrawFrameWindow();

    pthread_mutex_unlock(&video_draw_mutex);
    SDL_Delay(KEY_DELAY);
    SDL_Event event;

    event.type = SDL_EVENT_QUIT;
    while (event.type != SDL_EVENT_KEY_DOWN) {
      SDL_Delay(100);
      SDL_PollEvent(&event);
    }
    SDL_DestroySurface(g_diskChooseState.bg_screen);
    g_diskChooseState.bg_screen = NULL;
    return false;
  }

  g_diskChooseState.slot = slot;
  g_diskChooseState.current_dir = dir;
  g_diskChooseState.act_file = index_file;
  if (g_diskChooseState.act_file >= g_diskChooseState.file_list.size()) {
    g_diskChooseState.act_file = 0;
  }
  if (g_diskChooseState.act_file <= FILES_IN_SCREEN / 2) {
    g_diskChooseState.first_file = 0;
  } else {
    g_diskChooseState.first_file = g_diskChooseState.act_file - (FILES_IN_SCREEN / 2);
  }
  g_diskChooseState.active = true;
  g_diskChooseState.finished = false;
  g_diskChooseState.cancelled = false;
  g_diskChooseState.p_index_file = &index_file;

  AppMode_e old_mode = g_state.mode;
  g_state.mode = MODE_DISK_CHOOSE;

  pthread_mutex_unlock(&video_draw_mutex);

  // Still blocking for now, but using the new tick/draw functions via Sys_Input/Sys_Draw
  while (g_diskChooseState.active) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        g_state.mode = MODE_EXIT;
        g_diskChooseState.active = false;
        break;
      }
      DiskChoose_Tick(&event);
    }
    pthread_mutex_lock(&video_draw_mutex);
    DiskChoose_Draw();
    pthread_mutex_unlock(&video_draw_mutex);
    SDL_Delay(10);
  }

  g_state.mode = old_mode;
  SDL_DestroySurface(g_diskChooseState.bg_screen);
  g_diskChooseState.bg_screen = NULL;

  if (g_diskChooseState.finished) {
    filename = g_diskChooseState.result_filename;
    isdir = g_diskChooseState.result_isdir;
    return true;
  }
  
  return false;
}
