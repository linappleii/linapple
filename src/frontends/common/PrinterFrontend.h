// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>

// What the frontend's printer sink is told once per run, after the
// configuration has been read and the directories resolved.
struct PrinterFrontendSettings_t {
  // As configured: a leading "~/" is the user's home, and a relative path is
  // taken against base_dir.
  std::string filename;
  std::string base_dir;
  bool append;
  bool eight_bit;
  // The lowest slot the configuration puts a printer card in, or 0 when it
  // names none. Any printer sink in a higher slot writes its own file, named
  // with the slot, so two cards never interleave in one file.
  int primary_slot;
};

auto printer_frontend_install(const PrinterFrontendSettings_t& settings)
    -> void;

// The file the sink in this slot writes, after expansion and the slot rule.
auto printer_frontend_output_path(int slot) -> std::string;
