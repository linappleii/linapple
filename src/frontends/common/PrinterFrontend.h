// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>

struct PrinterFrontendSettings_t {
  std::string filename;
  std::string base_dir;
  bool append;
  bool eight_bit;
  int primary_slot;
};

auto printer_frontend_install(const PrinterFrontendSettings_t& settings)
    -> void;

auto printer_frontend_output_path(int slot) -> std::string;
