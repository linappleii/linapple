// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/services/ftp/FtpTypes.h"

#ifndef ENABLE_FTP
#define ENABLE_FTP 1
#endif

#if ENABLE_FTP
using CURL = void;
struct CurlDeleter_t {
  auto operator()(CURL* handle) const noexcept -> void;
};
using CurlHandlePtr_t = std::unique_ptr<CURL, CurlDeleter_t>;

struct CurlGlobalGuard_t {
  CurlGlobalGuard_t();
  ~CurlGlobalGuard_t();
  CurlGlobalGuard_t(const CurlGlobalGuard_t&) = delete;
  auto operator=(const CurlGlobalGuard_t&) -> CurlGlobalGuard_t& = delete;
  CurlGlobalGuard_t(CurlGlobalGuard_t&&) noexcept = default;
  auto operator=(CurlGlobalGuard_t&&) noexcept -> CurlGlobalGuard_t& = default;
};
#else
struct CurlGlobalGuard_t {
  CurlGlobalGuard_t() = default;
  ~CurlGlobalGuard_t() = default;
};
#endif

class FtpClient_t {
 public:
  FtpClient_t();
  ~FtpClient_t() = default;

  FtpClient_t(const FtpClient_t&) = delete;
  auto operator=(const FtpClient_t&) -> FtpClient_t& = delete;
  FtpClient_t(FtpClient_t&&) noexcept;
  auto operator=(FtpClient_t&&) noexcept -> FtpClient_t&;

  auto download_file(const std::string& remote_url,
                     const std::string& local_cache_dir,
                     const std::string& filename,
                     const std::string& user_pwd = "",
                     FtpProgressCallback_t progress_cb = nullptr,
                     void* user_data = nullptr) -> FtpStatus_t;

  auto fetch_directory_listing(const std::string& remote_dir_url,
                               std::vector<FtpFileEntry_t>& entries,
                               const std::string& user_pwd = "") -> FtpStatus_t;

 private:
#if ENABLE_FTP
  CurlHandlePtr_t curl_handle_;
#endif
};
