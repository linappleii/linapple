#pragma once

#include <vector>
#include <string>
#include "file_entry.h"

bool ChooseAnImage(int sx, int sy, const std::string& incoming_dir, int slot,
                   std::string& filename, bool& isdir, size_t& index_file);

struct file_list_generator_t {
  virtual const std::vector<file_entry_t> generate_file_list() = 0;

  virtual const std::string get_starting_message() = 0;

  virtual  const std::string get_failure_message() = 0;
};

bool ChooseImageDialog(int sx, int sy, const std::string& dir, int slot, file_list_generator_t* file_list_generator,
                       std::string& filename, bool& isdir, size_t& index_file);
