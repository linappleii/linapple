// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>

#include "core/services/ftp/FtpTypes.h"

// Vendored public-domain ftpparse code by D. J. Bernstein (exempt from _t
// naming rule)
// NOLINTBEGIN(readability-identifier-naming)
struct ftpparse {
  char* name; /* not necessarily 0-terminated */
  int namelen;
  int flagtrycwd;  /* 0 if cwd is definitely pointless, 1 otherwise */
  int flagtryretr; /* 0 if retr is definitely pointless, 1 otherwise */
  int sizetype;
  int64_t size; /* number of octets */
  int mtimetype;
  time_t mtime; /* modification time */
  int idtype;
  char* id; /* not necessarily 0-terminated */
  int idlen;
};
// NOLINTEND(readability-identifier-naming)

constexpr int FTPPARSE_SIZE_UNKNOWN = 0;
constexpr int FTPPARSE_SIZE_BINARY = 1;
constexpr int FTPPARSE_SIZE_ASCII = 2;

constexpr int FTPPARSE_MTIME_UNKNOWN = 0;
constexpr int FTPPARSE_MTIME_LOCAL = 1;
constexpr int FTPPARSE_MTIME_REMOTEMINUTE = 2;
constexpr int FTPPARSE_MTIME_REMOTEDAY = 3;

constexpr int FTPPARSE_ID_UNKNOWN = 0;
constexpr int FTPPARSE_ID_FULL = 1;

auto ftpparse(struct ftpparse* fp, char* buf, int len) -> int;

auto ftp_parse_line(const char* line, size_t length, FtpFileEntry_t& out_entry)
    -> bool;
