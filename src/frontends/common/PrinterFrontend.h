// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <string>

#include "apple2/peripherals/Peripheral_Internal.h"

struct PrinterFrontendSettings_t {
  std::string filename;
  std::string base_dir;
  bool append;
  bool eight_bit;
  int primary_slot;
};

// Takes the settings for the run; the host sink that forwards printer slots
// here is installed separately, once, by the frontend.
auto printer_frontend_install(const PrinterFrontendSettings_t& settings)
    -> void;

// The printer's side of the frontend's byte sink, for the host sink to
// forward a printer slot's calls to.
auto printer_frontend_sink() -> const ByteSink_t&;

auto printer_frontend_output_path(int slot) -> std::string;
