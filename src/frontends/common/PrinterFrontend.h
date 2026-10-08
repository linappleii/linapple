// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>

#include "apple2/peripherals/Peripheral_Internal.h"

struct PrinterFrontendSettings {
  std::string filename;
  std::string base_dir;
  bool append;
  bool eight_bit;
  int primary_slot;
};

auto printer_frontend_install(const PrinterFrontendSettings& settings) -> void;

auto printer_frontend_sink() -> const ByteSink_t&;

auto printer_frontend_output_path(int slot) -> std::string;
