// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <string>

enum class FtpStatus_t : uint8_t {
  ok = 0,
  invalid_param,
  failed_init,
  connect_error,
  file_not_found,
  write_error,
  path_traversal_rejected,
  transfer_failed,
  timeout,
  disabled
};

enum class FtpEntryType_t : uint8_t { file, directory, symlink, unknown };

struct FtpFileEntry_t {
  std::string name;
  FtpEntryType_t type{FtpEntryType_t::unknown};
  uint64_t size{0};
  int64_t mtime{0};
  bool can_cwd{false};
  bool can_retr{false};
};

using FtpProgressCallback_t = auto (*)(void* user_data, uint64_t dl_now,
                                       uint64_t dl_total) -> bool;
