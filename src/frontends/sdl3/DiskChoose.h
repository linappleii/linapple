// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL3/SDL_events.h>

#include <cstddef>
#include <string>

#include "frontends/sdl3/SdlPtr.h"

struct FileList_t;
struct FileListGenerator_t;

struct DiskChooseState_t {
  int slot = 0;
  std::string current_dir;
  FileList_t* list_handle = nullptr;
  size_t act_file = 0;
  size_t first_file = 0;
  bool active = false;

  // Surface for background
  SdlSurfacePtr_t bg_screen;

  // Callback or storage for result
  std::string result_filename;
  bool result_isdir = false;
  bool finished = false;
  bool cancelled = false;

  // For returning results to the original caller (which is still blocking for
  // now)
  size_t* index_file_out = nullptr;
};

extern DiskChooseState_t g_diskChooseState;

auto disk_choose_tick(SDL_Event* event) -> void;
auto disk_choose_draw() -> void;

[[nodiscard]] auto choose_an_image(int screen_w, int screen_h,
                                   const std::string& incoming_dir, int slot,
                                   std::string& filename, bool& isdir,
                                   size_t& index_file) -> bool;

[[nodiscard]] auto choose_image_dialog(int screen_w, int screen_h,
                                       const std::string& dir, int slot,
                                       FileListGenerator_t* file_list_generator,
                                       std::string& filename, bool& isdir,
                                       size_t& index_file) -> bool;
