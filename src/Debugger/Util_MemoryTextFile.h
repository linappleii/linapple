// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>
#include <vector>

class MemoryTextFile_t {
  std::vector<char> buffer_;
  std::vector<char*> lines_;
  bool dirty_{false};

  auto GetLinePointers() -> void;

 public:
  MemoryTextFile_t() : dirty_(false) {
    buffer_.reserve(2048);
    lines_.reserve(128);
  }

  auto Read(const std::string& filename) -> bool;

  auto Reset() -> void {
    buffer_.clear();
    lines_.clear();
  }

  auto GetNumLines() -> int {
    if (dirty_) {
      GetLinePointers();
    }
    return static_cast<int>(lines_.size());
  }

  auto GetLine(int line_index) const -> char* {
    return lines_.at(static_cast<size_t>(line_index));
  }

  auto GetLine(int line_index, char* line_out, int max_chars) -> void;

  auto PushLine(char* line) -> void;
};
