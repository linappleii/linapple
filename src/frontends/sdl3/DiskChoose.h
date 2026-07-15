#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "frontends/common/FileBrowser.h"

struct DiskChooseState_t {
  int slot;
  std::string current_dir;
  FileList_t* list_handle;  // Opaque handle from C API
  size_t act_file;
  size_t first_file;
  bool active;

  // Surface for background
  SDL_Surface* bg_screen;

  // Callback or storage for result
  std::string result_filename;
  bool result_isdir;
  bool finished;
  bool cancelled;

  // For returning results to the original caller (which is still blocking for
  // now)
  size_t* p_index_file;
};

void DiskChoose_Tick(SDL_Event* event);
void DiskChoose_Draw();

auto choose_an_image(int sx, int sy, const std::string& incoming_dir, int slot,
                   std::string& filename, bool& isdir, size_t& index_file)
    -> bool;

auto choose_image_dialog(int sx, int sy, const std::string& dir, int slot,
                       FileListGenerator_t* file_list_generator,
                       std::string& filename, bool& isdir, size_t& index_file)
    -> bool;
