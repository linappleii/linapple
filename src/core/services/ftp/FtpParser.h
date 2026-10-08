// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <string>

#include "core/services/ftp/FtpTypes.h"

auto ftp_parse_line(const char* line, size_t length, FtpFileEntry& out_entry)
    -> bool;

inline auto ftp_parse_line(const std::string& line, FtpFileEntry& out_entry)
    -> bool {
  return ftp_parse_line(line.data(), line.length(), out_entry);
}
