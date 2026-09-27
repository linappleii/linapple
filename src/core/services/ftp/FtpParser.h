// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <string>

#include "core/services/ftp/FtpTypes.h"

[[nodiscard]] auto ftp_parse_line(const char* line, size_t length,
                                  FtpFileEntry_t& out_entry) -> bool;

[[nodiscard]] inline auto ftp_parse_line(const std::string& line,
                                         FtpFileEntry_t& out_entry) -> bool {
  return ftp_parse_line(line.data(), line.length(), out_entry);
}
