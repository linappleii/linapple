// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/media/image_container/ImageContainer.h"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zip.h>
#include <zlib.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "core/Util_Path.h"
#include "core/Util_Text.h"

namespace macbinary {
namespace {
// MacBinary II (1987) and MacBinary III (1996) specifications.
constexpr size_t header_size = image_container_macbinary_header_len;
constexpr uint8_t old_version_offset = 0;
constexpr uint8_t name_len_offset = 1;
constexpr uint8_t zero_fill_offset_a = 74;
constexpr uint8_t zero_fill_offset_b = 82;
constexpr uint8_t writer_version_offset = 122;
constexpr uint8_t reader_version_offset = 123;
constexpr uint8_t crc_offset = 124;
constexpr uint8_t version_ii = 0x81;
constexpr uint8_t version_iii = 0x82;
constexpr uint8_t max_name_len = 63;

constexpr uint16_t crc16_polynomial = 0x1021;
constexpr uint16_t crc16_msb = 0x8000;
constexpr int bits_per_byte = 8;

auto crc16_xmodem(const uint8_t* data, size_t length) noexcept -> uint16_t {
  if (data == nullptr || length == 0) {
    return 0;
  }
  uint16_t crc = 0;
  for (size_t i = 0; i < length; ++i) {
    crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[i]) << 8));
    for (int bit = 0; bit < bits_per_byte; ++bit) {
      crc = ((crc & crc16_msb) != 0)
                ? static_cast<uint16_t>((crc << 1) ^ crc16_polynomial)
                : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}
}  // namespace
}  // namespace macbinary

namespace {

constexpr size_t decompression_chunk_size = 16384;

constexpr const char* gzip_extension = "gz";
constexpr const char* zip_extension = "zip";
constexpr const char* macosx_sidecar_dir = "__MACOSX/";
constexpr const char* appledouble_prefix = "._";
constexpr const char* temp_template_suffix = "/linapple_XXXXXX";

auto copy_whole(char* dest, const char* src, size_t size) noexcept
    -> ImageContainerError_e {
  if (dest == nullptr || src == nullptr || size == 0) {
    return image_container_invalid_argument;
  }
  if (strlen(src) >= size) {
    dest[0] = '\0';
    return image_container_invalid_argument;
  }
  util_safe_strcpy(dest, src, size);
  return image_container_ok;
}

auto has_extension(const char* path, const char* extension) noexcept -> bool {
  if (path == nullptr || extension == nullptr) {
    return false;
  }
  const size_t name_len = strlen(path);
  const size_t suffix_len = strlen(extension) + 1;
  return name_len > suffix_len && path[name_len - suffix_len] == '.' &&
         strcasecmp(path + name_len - suffix_len + 1, extension) == 0;
}

auto get_file_size(const char* path) noexcept -> size_t {
  if (path == nullptr) {
    return 0;
  }
  struct stat st{};
  if (stat(path, &st) != 0 || st.st_size <= 0) {
    return 0;
  }
  return static_cast<size_t>(st.st_size);
}

constexpr auto output_exceeds_bound(size_t total_written,
                                    size_t compressed_size,
                                    size_t uncompressed_threshold) noexcept
    -> bool {
  return total_written > uncompressed_threshold &&
         (compressed_size == 0 ||
          total_written > compressed_size * image_container_ratio_limit);
}

auto map_zip_error_code(int zip_err) noexcept -> ImageContainerError_e {
  switch (zip_err) {
    case ZIP_ER_NOENT:
      return image_container_not_found;
    case ZIP_ER_OPEN:
    case ZIP_ER_READ:
    case ZIP_ER_SEEK:
    case ZIP_ER_TMPOPEN:
    case ZIP_ER_MEMORY:
      return image_container_io;
    default:
      return image_container_corrupt;
  }
}

auto map_zip_error(const zip_error_t* error) noexcept -> ImageContainerError_e {
  if (error == nullptr) {
    return image_container_io;
  }
  return map_zip_error_code(zip_error_code_zip(error));
}

auto open_zip(const char* path, zip** out_archive) noexcept
    -> ImageContainerError_e {
  if (path == nullptr || out_archive == nullptr) {
    return image_container_invalid_argument;
  }
  *out_archive = nullptr;

  int code = 0;
  *out_archive = zip_open(path, ZIP_RDONLY, &code);
  if (*out_archive != nullptr) {
    return image_container_ok;
  }
  return map_zip_error_code(code);
}

auto decompress_gzip(const char* compressed_path, FILE* output_file,
                     size_t uncompressed_threshold) noexcept
    -> ImageContainerError_e {
  if (compressed_path == nullptr || output_file == nullptr) {
    return image_container_invalid_argument;
  }
  errno = 0;
  const std::unique_ptr<gzFile_s, decltype(&gzclose)> compressed_file(
      gzopen(compressed_path, "rb"), gzclose);
  if (compressed_file == nullptr) {
    return errno == ENOENT ? image_container_not_found : image_container_io;
  }

  const size_t compressed_size = get_file_size(compressed_path);
  std::array<uint8_t, decompression_chunk_size> buffer{};
  size_t total_written = 0;
  int bytes_read = 0;
  int zlib_status = Z_OK;

  while ((bytes_read = gzread(compressed_file.get(), buffer.data(),
                              static_cast<unsigned int>(buffer.size()))) > 0) {
    total_written += static_cast<size_t>(bytes_read);
    if (output_exceeds_bound(total_written, compressed_size,
                             uncompressed_threshold)) {
      return image_container_too_large;
    }
    if (fwrite(buffer.data(), 1, static_cast<size_t>(bytes_read),
               output_file) != static_cast<size_t>(bytes_read)) {
      return image_container_io;
    }
    gzerror(compressed_file.get(), &zlib_status);
    if (zlib_status != Z_OK) {
      break;
    }
  }
  // In zlib 1.3+, truncated stream errors set during partial read can clear on
  // EOF.
  if (zlib_status == Z_OK) {
    gzerror(compressed_file.get(), &zlib_status);
  }
  if (bytes_read == 0 && zlib_status == Z_OK) {
    return image_container_ok;
  }
  return zlib_status == Z_ERRNO ? image_container_io : image_container_corrupt;
}

auto first_payload_entry(zip* archive) noexcept -> int64_t {
  if (archive == nullptr) {
    return -1;
  }
  const int64_t entry_count = zip_get_num_entries(archive, 0);
  for (int64_t index = 0; index < entry_count; ++index) {
    const char* name = zip_get_name(archive, static_cast<uint64_t>(index), 0);
    if (name == nullptr || name[0] == '\0') {
      continue;
    }
    const size_t name_len = strlen(name);
    if (name[name_len - 1] == '/' ||
        strncmp(name, macosx_sidecar_dir, strlen(macosx_sidecar_dir)) == 0) {
      continue;
    }
    const char* slash = strrchr(name, '/');
    const char* base = (slash != nullptr) ? (slash + 1) : name;
    if (strncmp(base, appledouble_prefix, strlen(appledouble_prefix)) == 0) {
      continue;
    }
    return index;
  }
  return -1;
}

auto decompress_zip(const char* compressed_path, FILE* output_file,
                    size_t uncompressed_threshold) noexcept
    -> ImageContainerError_e {
  if (compressed_path == nullptr || output_file == nullptr) {
    return image_container_invalid_argument;
  }
  zip* zip_archive = nullptr;
  const ImageContainerError_e opened = open_zip(compressed_path, &zip_archive);
  if (opened != image_container_ok) {
    return opened;
  }
  const std::unique_ptr<zip, int (*)(zip*)> zip_closer(zip_archive, zip_close);

  const int64_t payload_index = first_payload_entry(zip_archive);
  if (payload_index < 0) {
    return image_container_corrupt;
  }
  const auto entry = static_cast<uint64_t>(payload_index);

  zip_stat_t sb{};
  zip_stat_init(&sb);
  size_t compressed_entry_size = 0;
  if (zip_stat_index(zip_archive, entry, 0, &sb) == 0 &&
      (sb.valid & ZIP_STAT_COMP_SIZE) != 0 && sb.comp_size > 0) {
    compressed_entry_size = static_cast<size_t>(sb.comp_size);
  } else {
    compressed_entry_size = get_file_size(compressed_path);
  }

  zip_file* file_in_zip = zip_fopen_index(zip_archive, entry, 0);
  if (file_in_zip == nullptr) {
    return map_zip_error(zip_get_error(zip_archive));
  }
  const std::unique_ptr<zip_file, int (*)(zip_file*)> file_closer(file_in_zip,
                                                                  zip_fclose);

  std::array<uint8_t, decompression_chunk_size> buffer{};
  size_t total_written = 0;
  int64_t bytes_read = 0;

  while ((bytes_read = zip_fread(file_in_zip, buffer.data(), buffer.size())) >
         0) {
    total_written += static_cast<size_t>(bytes_read);
    if (output_exceeds_bound(total_written, compressed_entry_size,
                             uncompressed_threshold)) {
      return image_container_too_large;
    }
    if (fwrite(buffer.data(), 1, static_cast<size_t>(bytes_read),
               output_file) != static_cast<size_t>(bytes_read)) {
      return image_container_io;
    }
  }
  if (bytes_read == 0) {
    return image_container_ok;
  }
  return map_zip_error(zip_file_get_error(file_in_zip));
}

auto payload_name_from_zip(const char* image_path, char* out_name,
                           size_t max_name_len) noexcept
    -> ImageContainerError_e {
  zip* archive = nullptr;
  const ImageContainerError_e opened = open_zip(image_path, &archive);
  if (opened != image_container_ok) {
    return opened;
  }
  const std::unique_ptr<zip, int (*)(zip*)> closer(archive, zip_close);
  const int64_t payload_index = first_payload_entry(archive);
  if (payload_index < 0) {
    return image_container_corrupt;
  }
  const char* entry =
      zip_get_name(archive, static_cast<uint64_t>(payload_index), 0);
  if (entry == nullptr) {
    return image_container_corrupt;
  }
  const char* entry_slash = strrchr(entry, '/');
  return copy_whole(out_name,
                    (entry_slash != nullptr) ? (entry_slash + 1) : entry,
                    max_name_len);
}

auto payload_name_from_gzip(const char* basename, char* out_name,
                            size_t max_name_len) noexcept
    -> ImageContainerError_e {
  const size_t suffix_len = strlen(gzip_extension) + 1;
  const size_t stripped_len = strlen(basename) - suffix_len;
  if (stripped_len >= max_name_len) {
    out_name[0] = '\0';
    return image_container_invalid_argument;
  }
  memcpy(out_name, basename, stripped_len);
  out_name[stripped_len] = '\0';
  return image_container_ok;
}

}  // namespace

extern "C" auto image_container_detect_macbinary(const uint8_t* header_data,
                                                 size_t header_len,
                                                 uint32_t file_size)
    -> uint32_t {
  if (header_data == nullptr || header_len < macbinary::header_size ||
      file_size <= macbinary::header_size) {
    return 0;
  }

  if (header_data[macbinary::old_version_offset] != 0) {
    return 0;
  }

  const uint8_t name_len = header_data[macbinary::name_len_offset];
  if (name_len == 0 || name_len > macbinary::max_name_len) {
    return 0;
  }

  if (header_data[macbinary::zero_fill_offset_a] != 0 ||
      header_data[macbinary::zero_fill_offset_b] != 0) {
    return 0;
  }

  const uint8_t writer_version = header_data[macbinary::writer_version_offset];
  if (writer_version != macbinary::version_ii &&
      writer_version != macbinary::version_iii) {
    return 0;
  }

  if (header_data[macbinary::reader_version_offset] != macbinary::version_ii) {
    return 0;
  }

  const auto stored_crc =
      static_cast<uint16_t>((header_data[macbinary::crc_offset] << 8) |
                            header_data[macbinary::crc_offset + 1]);
  if (macbinary::crc16_xmodem(header_data, macbinary::crc_offset) !=
      stored_crc) {
    return 0;
  }

  return static_cast<uint32_t>(macbinary::header_size);
}

extern "C" auto image_container_payload_name(const char* image_path,
                                             char* out_name,
                                             size_t max_name_len)
    -> ImageContainerError_e {
  if (image_path == nullptr || out_name == nullptr || max_name_len == 0) {
    return image_container_invalid_argument;
  }
  out_name[0] = '\0';

  if (has_extension(image_path, zip_extension)) {
    return payload_name_from_zip(image_path, out_name, max_name_len);
  }

  const char* slash = strrchr(image_path, '/');
  const char* basename = (slash != nullptr) ? (slash + 1) : image_path;

  if (has_extension(image_path, gzip_extension)) {
    return payload_name_from_gzip(basename, out_name, max_name_len);
  }

  return copy_whole(out_name, basename, max_name_len);
}

extern "C" auto image_container_prepare_compressed_path(
    const char* image_path, char* out_load_path, size_t max_path_len,
    size_t uncompressed_threshold, bool* out_is_temporary)
    -> ImageContainerError_e {
  if (image_path == nullptr || out_load_path == nullptr ||
      out_is_temporary == nullptr) {
    return image_container_invalid_argument;
  }
  *out_is_temporary = false;
  if (max_path_len == 0) {
    return image_container_invalid_argument;
  }
  out_load_path[0] = '\0';

  const bool is_gz = has_extension(image_path, gzip_extension);
  const bool is_zip = has_extension(image_path, zip_extension);

  if (!is_gz && !is_zip) {
    return copy_whole(out_load_path, image_path, max_path_len);
  }

  const char* tmp_dir = getenv("TMPDIR");
  if (tmp_dir == nullptr || tmp_dir[0] == '\0') {
    tmp_dir = "/tmp";
  }

  const std::string temp_template = std::string(tmp_dir) + temp_template_suffix;
  if (temp_template.size() >= max_path_len) {
    return image_container_invalid_argument;
  }
  util_safe_strcpy(out_load_path, temp_template.c_str(), max_path_len);

  const int fd = mkstemp(out_load_path);
  if (fd == -1) {
    out_load_path[0] = '\0';
    return image_container_io;
  }

  FilePtr temp_stream(fdopen(fd, "wb"), fclose);
  if (temp_stream == nullptr) {
    close(fd);
    unlink(out_load_path);
    out_load_path[0] = '\0';
    return image_container_io;
  }

  ImageContainerError_e result =
      is_gz ? decompress_gzip(image_path, temp_stream.get(),
                              uncompressed_threshold)
            : decompress_zip(image_path, temp_stream.get(),
                             uncompressed_threshold);

  if (result == image_container_ok && fclose(temp_stream.release()) != 0) {
    result = image_container_io;
  }
  if (result != image_container_ok) {
    temp_stream.reset();
    unlink(out_load_path);
    out_load_path[0] = '\0';
    return result;
  }

  *out_is_temporary = true;
  return image_container_ok;
}

extern "C" auto image_container_supported_extensions(void) -> const
    char* const* {
  static constexpr const char* const supported_extensions[] = {
      gzip_extension,
      zip_extension,
      nullptr,
  };
  return supported_extensions;
}
