#pragma once

#include <vector>
#include <string>
#include <SDL3/SDL.h>
#include "file_entry.h"

struct DiskChooseState {
  int slot;
  std::string current_dir;
  std::vector<file_entry_t> file_list;
  size_t act_file;
  size_t first_file;
  bool active;
  
  // Surface for background
  SDL_Surface *bg_screen;
  
  // Callback or storage for result
  std::string result_filename;
  bool result_isdir;
  bool finished;
  bool cancelled;

  // For returning results to the original caller (which is still blocking for now)
  size_t* p_index_file;
};

void DiskChoose_Tick(SDL_Event* event);
void DiskChoose_Draw();

bool ChooseAnImage(int sx, int sy, const std::string& incoming_dir, int slot,
                   std::string& filename, bool& isdir, size_t& index_file);

struct file_list_generator_t {
  virtual const std::vector<file_entry_t> generate_file_list() = 0;

  virtual const std::string get_starting_message() = 0;

  virtual  const std::string get_failure_message() = 0;
};

bool ChooseImageDialog(int sx, int sy, const std::string& dir, int slot, file_list_generator_t* file_list_generator,
                       std::string& filename, bool& isdir, size_t& index_file);
