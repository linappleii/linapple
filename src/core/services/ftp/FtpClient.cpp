// SPDX-License-Identifier: GPL-2.0-only
#include "core/services/ftp/FtpClient.h"

#if ENABLE_FTP
#include <curl/curl.h>
#include <curl/curlver.h>
#include <curl/easy.h>
#include <curl/system.h>
#include <unistd.h>
#endif

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "core/Util_Path.h"
#include "core/services/ftp/FtpParser.h"
#include "core/services/ftp/FtpTypes.h"

#if ENABLE_FTP

auto CurlDeleter::operator()(CURL* handle) const noexcept -> void {
  if (handle != nullptr) {
    curl_easy_cleanup(handle);
  }
}

CurlGlobalGuard::CurlGlobalGuard() { curl_global_init(CURL_GLOBAL_DEFAULT); }

CurlGlobalGuard::~CurlGlobalGuard() { curl_global_cleanup(); }

namespace {

constexpr long connect_timeout_seconds = 10;
constexpr long download_timeout_seconds = 60;
constexpr long listing_timeout_seconds = 30;
constexpr long no_signal_flag = 1;

struct ProgressContext {
  FtpProgressCallback callback{nullptr};
  void* user_data{nullptr};

  ProgressContext(FtpProgressCallback cb, void* ud)
      : callback(cb), user_data(ud) {}
};

struct StagingGuard {
  std::string path;
  bool armed = true;

  explicit StagingGuard(std::string p) : path(std::move(p)) {}
  ~StagingGuard() {
    if (armed) {
      std::remove(path.c_str());
    }
  }
  StagingGuard(const StagingGuard&) = delete;
  auto operator=(const StagingGuard&) -> StagingGuard& = delete;
  StagingGuard(StagingGuard&&) = delete;
  auto operator=(StagingGuard&&) -> StagingGuard& = delete;

  auto disarm() -> void { armed = false; }
};

auto curl_xfer_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                        curl_off_t /*unused*/, curl_off_t /*unused*/) -> int {
  auto* ctx = static_cast<ProgressContext*>(clientp);
  if (ctx == nullptr || ctx->callback == nullptr) {
    return 0;
  }
  const bool keep_going = ctx->callback(
      ctx->user_data, static_cast<uint64_t>(dlnow >= 0 ? dlnow : 0),
      static_cast<uint64_t>(dltotal >= 0 ? dltotal : 0));
  return keep_going ? 0 : 1;
}

auto write_string_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
    -> size_t {
  const size_t total = size * nmemb;
  auto* str = static_cast<std::string*>(userdata);
  if (str == nullptr || ptr == nullptr) {
    return 0;
  }
  str->append(ptr, total);
  return total;
}

auto map_curl_code(CURLcode code) noexcept -> FtpStatus {
  switch (code) {
    case CURLE_OK:
      return FtpStatus::ok;
    case CURLE_URL_MALFORMAT:
    case CURLE_BAD_FUNCTION_ARGUMENT:
      return FtpStatus::invalid_param;
    case CURLE_FAILED_INIT:
      return FtpStatus::failed_init;
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT:
      return FtpStatus::connect_error;
    case CURLE_REMOTE_FILE_NOT_FOUND:
      return FtpStatus::file_not_found;
    case CURLE_WRITE_ERROR:
      return FtpStatus::write_error;
    case CURLE_OPERATION_TIMEDOUT:
      return FtpStatus::timeout;
    default:
      return FtpStatus::transfer_failed;
  }
}

auto configure_ftp_protocols(CURL* curl) -> void {
#if LIBCURL_VERSION_NUM >= 0x075500
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "ftp,ftps");
#elif defined(CURLOPT_PROTOCOLS)
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
                   static_cast<long>(CURLPROTO_FTP | CURLPROTO_FTPS));
#endif
}

auto encode_ftp_url(CURL* curl, const std::string& raw_url) -> std::string {
  if (curl == nullptr || raw_url.empty()) {
    return raw_url;
  }

  const size_t scheme_pos = raw_url.find("://");
  if (scheme_pos == std::string::npos) {
    return raw_url;
  }

  const size_t path_start = raw_url.find('/', scheme_pos + 3);
  if (path_start == std::string::npos) {
    return raw_url;
  }

  std::string result = raw_url.substr(0, path_start);
  const std::string raw_path = raw_url.substr(path_start);

  size_t curr = 0;
  while (curr < raw_path.size()) {
    if (raw_path[curr] == '/') {
      result += '/';
      while (curr + 1 < raw_path.size() && raw_path[curr + 1] == '/') {
        ++curr;
      }
      ++curr;
      continue;
    }

    const size_t next_slash = raw_path.find('/', curr);
    const size_t seg_len = (next_slash == std::string::npos)
                               ? (raw_path.size() - curr)
                               : (next_slash - curr);
    const std::string segment = raw_path.substr(curr, seg_len);
    char* escaped = curl_easy_escape(curl, segment.c_str(),
                                     static_cast<int>(segment.size()));
    if (escaped != nullptr) {
      result += escaped;
      curl_free(escaped);
    } else {
      result += segment;
    }
    curr += seg_len;
  }

  return result;
}

}  // namespace

FtpClient::FtpClient() : curl_handle(curl_easy_init(), CurlDeleter{}) {}

FtpClient::FtpClient(FtpClient&&) noexcept = default;
auto FtpClient::operator=(FtpClient&&) noexcept -> FtpClient& = default;

auto FtpClient::download_file(const std::string& remote_url,
                              const std::string& local_cache_dir,
                              const std::string& filename,
                              const std::string& user_pwd,
                              FtpProgressCallback progress_cb, void* user_data)
    -> FtpStatus {
  if (remote_url.empty() || local_cache_dir.empty() || filename.empty()) {
    return FtpStatus::invalid_param;
  }

  if (filename.find("..") != std::string::npos ||
      filename.find('/') != std::string::npos ||
      filename.find('\\') != std::string::npos || filename.front() == '.' ||
      local_cache_dir.find("..") != std::string::npos) {
    return FtpStatus::path_traversal_rejected;
  }

  const std::string safe_name = Path::sanitize_filename(filename);
  if (safe_name.empty() || safe_name != filename) {
    return FtpStatus::path_traversal_rejected;
  }

  if (!curl_handle) {
    return FtpStatus::failed_init;
  }

  const std::string target_path = local_cache_dir + "/" + safe_name;
  const std::string staging_path =
      target_path + ".part." + std::to_string(getpid());

  FilePtr stream(fopen(staging_path.c_str(), "wb"), fclose);
  if (!stream) {
    return FtpStatus::write_error;
  }

  StagingGuard staging_guard{staging_path};

  CURL* curl = curl_handle.get();
  curl_easy_reset(curl);
  const std::string encoded_url = encode_ftp_url(curl, remote_url);
  curl_easy_setopt(curl, CURLOPT_URL, encoded_url.c_str());
  configure_ftp_protocols(curl);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, stream.get());
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout_seconds);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, download_timeout_seconds);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, no_signal_flag);

  if (!user_pwd.empty()) {
    curl_easy_setopt(curl, CURLOPT_USERPWD, user_pwd.c_str());
  }

  ProgressContext prog_ctx{progress_cb, user_data};
  if (progress_cb != nullptr) {
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_xfer_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog_ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  } else {
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
  }

  const CURLcode res = curl_easy_perform(curl);
  stream.reset();

  if (res != CURLE_OK) {
    return map_curl_code(res);
  }

  if (std::rename(staging_path.c_str(), target_path.c_str()) != 0) {
    return FtpStatus::write_error;
  }
  staging_guard.disarm();

  return FtpStatus::ok;
}

auto FtpClient::fetch_directory_listing(const std::string& remote_dir_url,
                                        std::vector<FtpFileEntry>& entries,
                                        const std::string& user_pwd)
    -> FtpStatus {
  if (remote_dir_url.empty()) {
    return FtpStatus::invalid_param;
  }

  if (!curl_handle) {
    return FtpStatus::failed_init;
  }

  entries.clear();
  std::string response_buffer;

  CURL* curl = curl_handle.get();
  curl_easy_reset(curl);
  const std::string encoded_url = encode_ftp_url(curl, remote_dir_url);
  curl_easy_setopt(curl, CURLOPT_URL, encoded_url.c_str());
  configure_ftp_protocols(curl);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout_seconds);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, listing_timeout_seconds);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, no_signal_flag);

  if (!user_pwd.empty()) {
    curl_easy_setopt(curl, CURLOPT_USERPWD, user_pwd.c_str());
  }

  const CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    return map_curl_code(res);
  }

  size_t start = 0;
  while (start < response_buffer.size()) {
    const size_t end = response_buffer.find('\n', start);
    const size_t len = (end == std::string::npos)
                           ? (response_buffer.size() - start)
                           : (end - start);
    FtpFileEntry entry{};
    if (ftp_parse_line(response_buffer.data() + start, len, entry)) {
      entries.push_back(std::move(entry));
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }

  return FtpStatus::ok;
}

#else

FtpClient::FtpClient() = default;
FtpClient::FtpClient(FtpClient&&) noexcept = default;
auto FtpClient::operator=(FtpClient&&) noexcept -> FtpClient& = default;

auto FtpClient::download_file(const std::string&, const std::string&,
                              const std::string&, const std::string&,
                              FtpProgressCallback, void*) -> FtpStatus {
  return FtpStatus::disabled;
}

auto FtpClient::fetch_directory_listing(const std::string&,
                                        std::vector<FtpFileEntry>&,
                                        const std::string&) -> FtpStatus {
  return FtpStatus::disabled;
}

#endif

auto ftp_status_to_string(FtpStatus status) noexcept -> const char* {
  constexpr std::array<const char*, 10> status_strings = {
      {
          "OK",
          "Invalid parameter",
          "Failed initialization",
          "Connection error",
          "File not found",
          "Write error",
          "Path traversal rejected",
          "Transfer failed",
          "Operation timed out",
          "FTP support disabled",
      },
  };

  static_assert(
      status_strings.size() == static_cast<size_t>(FtpStatus::disabled) + 1,
      "status_strings size must match FtpStatus count");

  auto idx = static_cast<size_t>(status);
  return (idx < status_strings.size()) ? status_strings[idx] : "Unknown error";
}
