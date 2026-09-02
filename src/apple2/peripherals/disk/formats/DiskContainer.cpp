// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/DiskContainer.h"

#include <sys/stat.h>
#include <unistd.h>
#include <zip.h>
#include <zlib.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "core/Log.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

// cppcoreguidelines-pro-bounds-array-to-pointer-decay) Justification:
// Domain-specific container detection requires parameters mandated by the
// shared format probing signatures. Pointer arithmetic and array decay are
// required for physical bitstream inspection and decompression library ABIs.
// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)

namespace macbinary {
namespace {
constexpr uint8_t version_offset = 0;
constexpr uint8_t secondary_offset = 122;
constexpr uint8_t name_len_offset = 1;
constexpr uint8_t min_version = 0;
constexpr uint8_t secondary_zero = 0;
constexpr uint8_t max_name_len = 63;
}  // namespace
}  // namespace macbinary

namespace {

constexpr size_t decompression_chunk_size = 16384;

auto get_file_size(const char* path) -> size_t {
  struct stat st{};
  if (stat(path, &st) == 0 && st.st_size > 0) {
    return static_cast<size_t>(st.st_size);
  }
  return 0;
}

auto decompress_gzip(const char* compressed_path, FILE* output_file,
                     size_t uncompressed_threshold) -> bool {
  gzFile compressed_file = gzopen(compressed_path, "rb");
  if (compressed_file == nullptr) {
    return false;
  }

  auto closer = [](void* f) {
    if (f != nullptr) {
      gzclose(static_cast<gzFile>(f));
    }
  };
  std::unique_ptr<void, void (*)(void*)> guard(compressed_file, closer);

  const size_t compressed_size = get_file_size(compressed_path);
  std::array<uint8_t, decompression_chunk_size> buffer{};
  size_t total_written = 0;
  int bytes_read = 0;

  while ((bytes_read = gzread(compressed_file, buffer.data(),
                              static_cast<unsigned int>(buffer.size()))) > 0) {
    total_written += static_cast<size_t>(bytes_read);

    if (total_written > uncompressed_threshold) {
      if (compressed_size == 0 ||
          total_written >
              compressed_size * disk_container::compression_ratio_limit) {
        Logger::error(
            "Decompression aborted for '%s': Uncompressed data (%zu bytes) "
            "exceeded "
            "the %zu MB threshold and violated the 100:1 compression ratio "
            "safety limit "
            "(compressed size: %zu bytes). Blocked to protect against "
            "potential zip-bomb exhaustion. "
            "If you are sure this is a valid disk image, please uncompress the "
            "file first before loading.",
            compressed_path, total_written,
            uncompressed_threshold / (1024 * 1024), compressed_size);
        return false;
      }
    }

    if (fwrite(buffer.data(), 1, static_cast<size_t>(bytes_read),
               output_file) != static_cast<size_t>(bytes_read)) {
      return false;
    }
  }

  return bytes_read == 0;
}

auto decompress_zip(const char* compressed_path, FILE* output_file,
                    size_t uncompressed_threshold) -> bool {
  int err = 0;
  zip* zip_archive = zip_open(compressed_path, ZIP_RDONLY, &err);
  if (zip_archive == nullptr) {
    return false;
  }
  std::unique_ptr<zip, int (*)(zip*)> zip_closer(zip_archive, zip_close);

  if (zip_get_num_entries(zip_archive, 0) <= 0) {
    return false;
  }

  zip_stat_t sb{};
  zip_stat_init(&sb);
  size_t compressed_entry_size = 0;
  if (zip_stat_index(zip_archive, 0, 0, &sb) == 0 &&
      (sb.valid & ZIP_STAT_COMP_SIZE) != 0 && sb.comp_size > 0) {
    compressed_entry_size = static_cast<size_t>(sb.comp_size);
  } else {
    compressed_entry_size = get_file_size(compressed_path);
  }

  zip_file* file_in_zip = zip_fopen_index(zip_archive, 0, 0);
  if (file_in_zip == nullptr) {
    return false;
  }
  std::unique_ptr<zip_file, int (*)(zip_file*)> file_closer(file_in_zip,
                                                            zip_fclose);

  std::array<uint8_t, decompression_chunk_size> buffer{};
  size_t total_written = 0;
  zip_int64_t bytes_read = 0;

  while ((bytes_read = zip_fread(file_in_zip, buffer.data(), buffer.size())) >
         0) {
    total_written += static_cast<size_t>(bytes_read);

    if (total_written > uncompressed_threshold) {
      if (compressed_entry_size == 0 ||
          total_written >
              compressed_entry_size * disk_container::compression_ratio_limit) {
        Logger::error(
            "Decompression aborted for '%s': Uncompressed data (%zu bytes) "
            "exceeded "
            "the %zu MB threshold and violated the 100:1 compression ratio "
            "safety limit "
            "(compressed size: %zu bytes). Blocked to protect against "
            "potential zip-bomb exhaustion. "
            "If you are sure this is a valid disk image, please uncompress the "
            "file first before loading.",
            compressed_path, total_written,
            uncompressed_threshold / (1024 * 1024), compressed_entry_size);
        return false;
      }
    }

    if (fwrite(buffer.data(), 1, static_cast<size_t>(bytes_read),
               output_file) != static_cast<size_t>(bytes_read)) {
      return false;
    }
  }

  return bytes_read == 0;
}

}  // namespace

// Why: Implements physical bitstream inspection to detect MacBinary II/III
// wrappers, a legacy container format used to store Apple II disk images
// with Macintosh-specific resource forks.
extern "C" auto disk_container_detect_macbinary(const uint8_t* header_data,
                                                size_t header_size,
                                                uint32_t file_size)
    -> uint32_t {
  if (header_data == nullptr || header_size < macbinary::header_size ||
      file_size <= macbinary::header_size) {
    return 0;
  }

  if (header_data[macbinary::version_offset] == macbinary::min_version &&
      header_data[macbinary::secondary_offset] == macbinary::secondary_zero) {
    const uint8_t name_len = header_data[macbinary::name_len_offset];
    if (name_len > 0 && name_len <= macbinary::max_name_len) {
      return static_cast<uint32_t>(macbinary::header_size);
    }
  }

  return 0;
}

// Why: Extracts compressed images (.gz and .zip) to a secure temporary path
// with threshold-based ratio checks to prevent decompression exhaustion bombs.
extern "C" auto disk_container_prepare_compressed_path(
    const char* image_path, char* out_load_path, size_t max_path_len,
    size_t uncompressed_threshold, bool* out_is_temporary) -> bool {
  if (image_path == nullptr || out_load_path == nullptr ||
      out_is_temporary == nullptr || max_path_len == 0) {
    return false;
  }

  const size_t name_len = strlen(image_path);
  constexpr size_t gz_ext_len = 3;
  constexpr size_t zip_ext_len = 4;

  const bool is_gz =
      (name_len > gz_ext_len &&
       strcasecmp(image_path + name_len - gz_ext_len, ".gz") == 0);
  const bool is_zip =
      (name_len > zip_ext_len &&
       strcasecmp(image_path + name_len - zip_ext_len, ".zip") == 0);

  if (!is_gz && !is_zip) {
    Util_SafeStrCpy(out_load_path, image_path, max_path_len);
    *out_is_temporary = false;
    return true;
  }

  const char* tmp_dir = getenv("TMPDIR");
  if (tmp_dir == nullptr || tmp_dir[0] == '\0') {
    tmp_dir = "/tmp";
  }

  std::string temp_template = std::string(tmp_dir) + "/linapple_XXXXXX";
  if (temp_template.size() >= max_path_len) {
    return false;
  }
  Util_SafeStrCpy(out_load_path, temp_template.c_str(), max_path_len);

  int fd = mkstemp(out_load_path);
  if (fd == -1) {
    return false;
  }

  FilePtr_t temp_stream(fdopen(fd, "wb"), fclose);
  if (temp_stream == nullptr) {
    close(fd);
    unlink(out_load_path);
    return false;
  }

  const bool success = is_gz ? decompress_gzip(image_path, temp_stream.get(),
                                               uncompressed_threshold)
                             : decompress_zip(image_path, temp_stream.get(),
                                              uncompressed_threshold);

  if (!success) {
    temp_stream.reset();
    unlink(out_load_path);
    return false;
  }

  *out_is_temporary = true;
  return true;
}

// NOLINTEND(bugprone-easily-swappable-parameters,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)
