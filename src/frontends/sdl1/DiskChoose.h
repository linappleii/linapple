// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL/SDL_events.h>

#include <cstddef>
#include <string>

#include "frontends/sdl1/SdlPtr.h"

struct FileList;
struct FileListGenerator;

struct DiskChooseState {
  int slot = 0;
  std::string current_dir;
  FileList* list_handle = nullptr;
  size_t act_file = 0;
  size_t first_file = 0;
  bool active = false;

  SdlSurfacePtr bg_screen;

  std::string result_filename;
  bool result_isdir = false;
  bool finished = false;
  bool cancelled = false;

  size_t* index_file_out = nullptr;
};

extern DiskChooseState g_disk_choose_state;

auto disk_choose_tick(SDL_Event* event) -> void;
auto disk_choose_draw() -> void;

auto choose_an_image(int sx, int sy, const std::string& incoming_dir, int slot,
                     std::string& filename, bool& isdir, size_t& index_file)
    -> bool;

auto choose_image_dialog(int sx, int sy, const std::string& dir, int slot,
                         FileListGenerator* file_list_generator,
                         std::string& filename, bool& isdir, size_t& index_file)
    -> bool;
