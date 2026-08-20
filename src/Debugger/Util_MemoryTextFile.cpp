// SPDX-License-Identifier: GPL-2.0-only
#include "Util_MemoryTextFile.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

namespace {
constexpr int eol_null = 0;
}

auto MemoryTextFile_t::Read(const std::string& filename) -> bool {
  FilePtr_t file_handle(fopen(filename.c_str(), "rb"), fclose);
  if (!file_handle) {
    return false;
  }

  fseek(file_handle.get(), 0, SEEK_END);
  long file_size = ftell(file_handle.get());
  fseek(file_handle.get(), 0, SEEK_SET);

  if (file_size < 0) {
    return false;
  }

  buffer_.reserve(static_cast<size_t>(file_size) + 1);
  buffer_.insert(buffer_.begin(), static_cast<size_t>(file_size) + 1, 0);

  char* buf_ptr = &buffer_.at(0);
  size_t read_bytes =
      fread(reinterpret_cast<void*>(buf_ptr), static_cast<size_t>(file_size), 1,
            file_handle.get());
  if (read_bytes == 0 && file_size > 0) {
    return false;
  }

  dirty_ = true;
  GetLinePointers();
  return true;
}

auto MemoryTextFile_t::GetLine(const int line_index, char* line_out,
                               const int max_chars) -> void {
  if (dirty_) {
    GetLinePointers();
  }

  if (line_out == nullptr || max_chars <= 0) {
    return;
  }

  memset(line_out, 0, static_cast<size_t>(max_chars));
  Util_SafeStrCpy(line_out, lines_[static_cast<size_t>(line_index)],
                  max_chars - 1);
}

// cr/new lines are converted into null, string terminators
auto MemoryTextFile_t::GetLinePointers() -> void {
  if (!dirty_) {
    return;
  }

  lines_.clear();
  if (buffer_.empty()) {
    dirty_ = false;
    return;
  }

  char* begin_ptr = &buffer_[0];
  char* last_ptr = &buffer_[buffer_.size() - 1];

  while (begin_ptr <= last_ptr) {
    if (*begin_ptr != 0) {  // Only keep non-empty lines
      lines_.push_back(begin_ptr);
    }

    char* end_ptr = const_cast<char*>(skip_until_eol(begin_ptr));
    char* start_next_line = nullptr;

    if (*end_ptr == eol_null) {
      start_next_line = end_ptr + 1;
    } else {
      start_next_line = const_cast<char*>(eat_eol(end_ptr));
      int eol_len = static_cast<int>(start_next_line - end_ptr);
      while (eol_len-- > 1) {
        *end_ptr++ = ' ';
      }
      *end_ptr = eol_null;
    }
    begin_ptr = start_next_line;
  }

  dirty_ = false;
}

auto MemoryTextFile_t::PushLine(char* line) -> void {
  char* src_ptr = line;
  while (src_ptr != nullptr && *src_ptr != 0) {
    if (*src_ptr == CHAR_CR || *src_ptr == CHAR_LF) {
      buffer_.push_back(eol_null);
    } else {
      buffer_.push_back(*src_ptr);
    }
    src_ptr++;
  }
  buffer_.push_back(eol_null);
  dirty_ = true;
}
