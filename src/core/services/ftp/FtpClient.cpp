// SPDX-License-Identifier: GPL-2.0-only
#include "core/services/ftp/FtpClient.h"

#if ENABLE_FTP
#include <curl/curl.h>
#include <curl/curlver.h>
#include <curl/easy.h>
#include <curl/system.h>
#include <unistd.h>
#endif

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "core/Util_Path.h"
#include "core/services/ftp/FtpParser.h"
#include "core/services/ftp/FtpTypes.h"

#if ENABLE_FTP

auto CurlDeleter_t::operator()(CURL* handle) const noexcept -> void {
  if (handle != nullptr) {
    curl_easy_cleanup(handle);
  }
}

CurlGlobalGuard_t::CurlGlobalGuard_t() {
  curl_global_init(CURL_GLOBAL_DEFAULT);
}

CurlGlobalGuard_t::~CurlGlobalGuard_t() { curl_global_cleanup(); }

namespace {

struct ProgressContext_t {
  FtpProgressCallback_t callback{nullptr};
  void* user_data{nullptr};

  ProgressContext_t(FtpProgressCallback_t cb, void* ud)
      : callback(cb), user_data(ud) {}
};

auto curl_xfer_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                        curl_off_t, curl_off_t) -> int {
  auto* ctx = static_cast<ProgressContext_t*>(clientp);
  if (ctx != nullptr && ctx->callback != nullptr) {
    const bool keep_going = ctx->callback(
        ctx->user_data, static_cast<uint64_t>(dlnow >= 0 ? dlnow : 0),
        static_cast<uint64_t>(dltotal >= 0 ? dltotal : 0));
    return keep_going ? 0 : 1;
  }
  return 0;
}

auto write_string_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
    -> size_t {
  const size_t total = size * nmemb;
  auto* str = static_cast<std::string*>(userdata);
  if (str != nullptr && ptr != nullptr) {
    str->append(ptr, total);
  }
  return total;
}

auto map_curl_code(CURLcode code) -> FtpStatus_t {
  switch (code) {
    case CURLE_OK:
      return FtpStatus_t::ok;
    case CURLE_URL_MALFORMAT:
    case CURLE_BAD_FUNCTION_ARGUMENT:
      return FtpStatus_t::invalid_param;
    case CURLE_FAILED_INIT:
      return FtpStatus_t::failed_init;
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT:
      return FtpStatus_t::connect_error;
    case CURLE_REMOTE_FILE_NOT_FOUND:
      return FtpStatus_t::file_not_found;
    case CURLE_WRITE_ERROR:
      return FtpStatus_t::write_error;
    case CURLE_OPERATION_TIMEDOUT:
      return FtpStatus_t::timeout;
    default:
      return FtpStatus_t::transfer_failed;
  }
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
      result += raw_path[curr];
      while (curr + 1 < raw_path.size() && raw_path[curr + 1] == '/') {
        ++curr;
      }
      ++curr;
    } else {
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
  }

  return result;
}

}  // namespace

FtpClient_t::FtpClient_t() : curl_handle_(curl_easy_init(), CurlDeleter_t{}) {}

FtpClient_t::FtpClient_t(FtpClient_t&&) noexcept = default;
auto FtpClient_t::operator=(FtpClient_t&&) noexcept -> FtpClient_t& = default;

auto FtpClient_t::download_file(const std::string& remote_url,
                                const std::string& local_cache_dir,
                                const std::string& filename,
                                const std::string& user_pwd,
                                FtpProgressCallback_t progress_cb,
                                void* user_data) -> FtpStatus_t {
  if (remote_url.empty() || local_cache_dir.empty() || filename.empty()) {
    return FtpStatus_t::invalid_param;
  }

  if (filename.find("..") != std::string::npos ||
      filename.find('/') != std::string::npos ||
      filename.find('\\') != std::string::npos || filename.front() == '.' ||
      local_cache_dir.find("..") != std::string::npos) {
    return FtpStatus_t::path_traversal_rejected;
  }

  const std::string safe_name = Path::sanitize_filename(filename);
  if (safe_name.empty() || safe_name != filename) {
    return FtpStatus_t::path_traversal_rejected;
  }

  if (!curl_handle_) {
    return FtpStatus_t::failed_init;
  }

  const std::string target_path = local_cache_dir + "/" + safe_name;
  const std::string staging_path =
      target_path + ".part." + std::to_string(getpid());

  FilePtr_t stream(fopen(staging_path.c_str(), "wb"), fclose);
  if (!stream) {
    return FtpStatus_t::write_error;
  }

  bool download_succeeded = false;
  struct StagingGuard_t {
    const std::string& path;
    const bool& success;
    ~StagingGuard_t() {
      if (!success) {
        std::remove(path.c_str());
      }
    }
  } staging_guard{staging_path, download_succeeded};

  CURL* curl = curl_handle_.get();
  curl_easy_reset(curl);
  const std::string encoded_url = encode_ftp_url(curl, remote_url);
  curl_easy_setopt(curl, CURLOPT_URL, encoded_url.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "ftp,ftps");
#elif defined(CURLOPT_PROTOCOLS)
  // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
                   static_cast<long>(CURLPROTO_FTP | CURLPROTO_FTPS));
#endif
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, stream.get());
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  if (!user_pwd.empty()) {
    curl_easy_setopt(curl, CURLOPT_USERPWD, user_pwd.c_str());
  }

  ProgressContext_t prog_ctx{progress_cb, user_data};
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

  download_succeeded = true;
  if (std::rename(staging_path.c_str(), target_path.c_str()) != 0) {
    return FtpStatus_t::write_error;
  }

  return FtpStatus_t::ok;
}

auto FtpClient_t::fetch_directory_listing(const std::string& remote_dir_url,
                                          std::vector<FtpFileEntry_t>& entries,
                                          const std::string& user_pwd)
    -> FtpStatus_t {
  if (remote_dir_url.empty()) {
    return FtpStatus_t::invalid_param;
  }

  if (!curl_handle_) {
    return FtpStatus_t::failed_init;
  }

  entries.clear();
  std::string response_buffer;

  CURL* curl = curl_handle_.get();
  curl_easy_reset(curl);
  const std::string encoded_url = encode_ftp_url(curl, remote_dir_url);
  curl_easy_setopt(curl, CURLOPT_URL, encoded_url.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "ftp,ftps");
#elif defined(CURLOPT_PROTOCOLS)
  // NOLINTNEXTLINE(clang-diagnostic-deprecated-declarations)
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
                   static_cast<long>(CURLPROTO_FTP | CURLPROTO_FTPS));
#endif
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

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
    FtpFileEntry_t entry{};
    if (ftp_parse_line(response_buffer.data() + start, len, entry)) {
      entries.push_back(std::move(entry));
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }

  return FtpStatus_t::ok;
}

#else

FtpClient_t::FtpClient_t() = default;
FtpClient_t::FtpClient_t(FtpClient_t&&) noexcept = default;
auto FtpClient_t::operator=(FtpClient_t&&) noexcept -> FtpClient_t& = default;

auto FtpClient_t::download_file(const std::string&, const std::string&,
                                const std::string&, const std::string&,
                                FtpProgressCallback_t, void*) -> FtpStatus_t {
  return FtpStatus_t::disabled;
}

auto FtpClient_t::fetch_directory_listing(const std::string&,
                                          std::vector<FtpFileEntry_t>&,
                                          const std::string&) -> FtpStatus_t {
  return FtpStatus_t::disabled;
}

#endif
