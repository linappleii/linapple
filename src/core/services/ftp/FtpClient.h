// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/services/ftp/FtpTypes.h"

#if ENABLE_FTP
using CURL = void;
struct CurlDeleter {
  auto operator()(CURL* handle) const noexcept -> void;
};
using CurlHandlePtr = std::unique_ptr<CURL, CurlDeleter>;

struct CurlGlobalGuard {
  CurlGlobalGuard();
  ~CurlGlobalGuard();
  CurlGlobalGuard(const CurlGlobalGuard&) = delete;
  auto operator=(const CurlGlobalGuard&) -> CurlGlobalGuard& = delete;
  CurlGlobalGuard(CurlGlobalGuard&&) noexcept = default;
  auto operator=(CurlGlobalGuard&&) noexcept -> CurlGlobalGuard& = default;
};
#else
struct CurlGlobalGuard {
  CurlGlobalGuard() = default;
  ~CurlGlobalGuard() = default;
};
#endif

using CurlGlobalGuard_t = CurlGlobalGuard;

class FtpClient {
 public:
  FtpClient();
  ~FtpClient() = default;

  FtpClient(const FtpClient&) = delete;
  auto operator=(const FtpClient&) -> FtpClient& = delete;
  FtpClient(FtpClient&&) noexcept;
  auto operator=(FtpClient&&) noexcept -> FtpClient&;

  auto download_file(const std::string& remote_url,
                     const std::string& local_cache_dir,
                     const std::string& filename,
                     const std::string& user_pwd = "",
                     FtpProgressCallback progress_cb = nullptr,
                     void* user_data = nullptr) -> FtpStatus;

  auto fetch_directory_listing(const std::string& remote_dir_url,
                               std::vector<FtpFileEntry>& entries,
                               const std::string& user_pwd = "") -> FtpStatus;

 private:
#if ENABLE_FTP
  CurlHandlePtr curl_handle;
#endif
};

auto ftp_status_to_string(FtpStatus status) noexcept -> const char*;
