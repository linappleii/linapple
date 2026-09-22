// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/media/image_container/ImageContainer.h"

#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zip.h>
#include <zlib.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "core/Util_Path.h"
#include "core/Util_Text.h"

// Justification: the detect signature is the shared probe shape, so its size
// parameters sit side by side. Pointer arithmetic and array decay come from
// indexing the 128-byte MacBinary header, walking path suffixes and the zlib
// and libzip C ABIs.
// NOLINTBEGIN(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay)

namespace macbinary {
namespace {
// Header layout per the MacBinary II standard (1987) and the MacBinary III
// standard (1996). Byte 1 is the filename length, which the standard bounds
// at 1 to 63. The zero-fill bytes are the ones the standard says a reader
// must check; 122 carries the writer's version (129 = II, 130 = III) and 123
// the minimum version a reader needs (129 for both), and 124-125 hold the
// CRC-16 of bytes 0-123.
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

// CRC-16 as the standard specifies it: polynomial 0x1021, initial value 0, no
// reflection, no final XOR (the XMODEM variant).
constexpr uint16_t crc16_polynomial = 0x1021;
constexpr uint16_t crc16_msb = 0x8000;
constexpr int bits_per_byte = 8;

auto crc16_xmodem(const uint8_t* data, size_t length) -> uint16_t {
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
const char* const supported_extensions[] = {gzip_extension, zip_extension,
                                            nullptr};
constexpr const char* macosx_sidecar_dir = "__MACOSX/";
constexpr const char* appledouble_prefix = "._";
constexpr const char* temp_template_suffix = "/linapple_XXXXXX";

auto copy_whole(char* dest, const char* src, size_t size)
    -> ImageContainerError_e {
  if (strlen(src) >= size) {
    dest[0] = '\0';
    return image_container_invalid_argument;
  }
  util_safe_strcpy(dest, src, size);
  return image_container_ok;
}

auto has_extension(const char* path, const char* extension) -> bool {
  const size_t name_len = strlen(path);
  const size_t suffix_len = strlen(extension) + 1;
  return name_len > suffix_len && path[name_len - suffix_len] == '.' &&
         strcasecmp(path + name_len - suffix_len + 1, extension) == 0;
}

auto get_file_size(const char* path) -> size_t {
  struct stat st{};
  if (stat(path, &st) == 0 && st.st_size > 0) {
    return static_cast<size_t>(st.st_size);
  }
  return 0;
}

auto output_exceeds_bound(size_t total_written, size_t compressed_size,
                          size_t uncompressed_threshold) -> bool {
  return total_written > uncompressed_threshold &&
         (compressed_size == 0 ||
          total_written > compressed_size * image_container_ratio_limit);
}

// libzip reports a missing archive, a host read failure and a damaged archive
// through one error object; the caller needs them told apart.
auto map_zip_error(const zip_error_t* error) -> ImageContainerError_e {
  switch (zip_error_code_zip(error)) {
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

auto open_zip(const char* path, zip** out_archive) -> ImageContainerError_e {
  int code = 0;
  *out_archive = zip_open(path, ZIP_RDONLY, &code);
  if (*out_archive != nullptr) {
    return image_container_ok;
  }
  zip_error_t error;
  zip_error_init_with_code(&error, code);
  const ImageContainerError_e mapped = map_zip_error(&error);
  zip_error_fini(&error);
  return mapped;
}

auto decompress_gzip(const char* compressed_path, FILE* output_file,
                     size_t uncompressed_threshold) -> ImageContainerError_e {
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
  }
  // A stream cut short is reported as an ordinary end of file by gzread and
  // only gzerror tells it apart from a complete one, so the status is asked
  // for even when the read loop ended quietly.
  int zlib_status = Z_OK;
  gzerror(compressed_file.get(), &zlib_status);
  if (bytes_read == 0 && zlib_status == Z_OK) {
    return image_container_ok;
  }
  return zlib_status == Z_ERRNO ? image_container_io : image_container_corrupt;
}

// The image is rarely entry 0. Many archivers store a directory entry
// before the files it holds, and macOS adds a __MACOSX/._name AppleDouble
// sidecar per file, often first. The payload is the first entry that is
// neither. Returns -1 when the archive holds no file at all.
auto first_payload_entry(zip* archive) -> int64_t {
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
                    size_t uncompressed_threshold) -> ImageContainerError_e {
  zip* zip_archive = nullptr;
  const ImageContainerError_e opened = open_zip(compressed_path, &zip_archive);
  if (opened != image_container_ok) {
    return opened;
  }
  std::unique_ptr<zip, int (*)(zip*)> zip_closer(zip_archive, zip_close);

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
  std::unique_ptr<zip_file, int (*)(zip_file*)> file_closer(file_in_zip,
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

}  // namespace

// Images that travelled through a Macintosh often still wear the 128-byte
// MacBinary header that carried their resource fork and Finder info. The
// version bytes alone are a heuristic; the standard's own test is the CRC over
// the header, so both are required. MacBinary I has no CRC and is not
// recognised: its only marks (zero at 0, 74 and 82, the test the MacBinary II
// standard gives for reading a MacBinary I file) occur in ordinary images.
extern "C" auto image_container_detect_macbinary(const uint8_t* header_data,
                                                 size_t header_len,
                                                 uint32_t file_size)
    -> uint32_t {
  if (header_data == nullptr || header_len < macbinary::header_size ||
      file_size <= macbinary::header_size) {
    return 0;
  }

  const uint8_t name_len = header_data[macbinary::name_len_offset];
  const uint8_t writer_version = header_data[macbinary::writer_version_offset];
  if (header_data[macbinary::old_version_offset] != 0 || name_len == 0 ||
      name_len > macbinary::max_name_len ||
      header_data[macbinary::zero_fill_offset_a] != 0 ||
      header_data[macbinary::zero_fill_offset_b] != 0 ||
      (writer_version != macbinary::version_ii &&
       writer_version != macbinary::version_iii) ||
      header_data[macbinary::reader_version_offset] != macbinary::version_ii) {
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

  const char* slash = strrchr(image_path, '/');
  const char* basename = (slash != nullptr) ? (slash + 1) : image_path;

  const bool is_gz = has_extension(image_path, gzip_extension);
  const bool is_zip = has_extension(image_path, zip_extension);
  if (!is_gz && !is_zip) {
    return copy_whole(out_name, basename, max_name_len);
  }

  if (is_zip) {
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
    const char* entry_slash = strrchr(entry, '/');
    return copy_whole(out_name,
                      (entry_slash != nullptr) ? (entry_slash + 1) : entry,
                      max_name_len);
  }

  // gzip's FNAME field is optional, and the gzFile API used here never
  // surfaces it; only a raw inflate with inflateGetHeader would, a second pass
  // over the stream for a field the writer may have left out.
  const std::string stripped(basename,
                             strlen(basename) - strlen(gzip_extension) - 1);
  return copy_whole(out_name, stripped.c_str(), max_name_len);
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

  // mkstemp is POSIX, declared by the <stdlib.h> behind <cstdlib>, which
  // include-cleaner does not credit.
  // NOLINTNEXTLINE(misc-include-cleaner)
  const int fd = mkstemp(out_load_path);
  if (fd == -1) {
    out_load_path[0] = '\0';
    return image_container_io;
  }

  // fdopen is POSIX, declared by the <stdio.h> behind <cstdio>, which
  // include-cleaner does not credit.
  // NOLINTNEXTLINE(misc-include-cleaner)
  FilePtr_t temp_stream(fdopen(fd, "wb"), fclose);
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

  // The last chunk may still sit in the stdio buffer, so a full disk or a
  // failing device surfaces only when the stream is closed. A temporary that
  // did not close cleanly is short and must not reach a driver.
  if (result == image_container_ok && fclose(temp_stream.release()) != 0) {
    result = image_container_io;
  }
  if (result != image_container_ok) {
    unlink(out_load_path);
    out_load_path[0] = '\0';
    return result;
  }

  *out_is_temporary = true;
  return image_container_ok;
}

extern "C" auto image_container_supported_extensions(void) -> const
    char* const* {
  return supported_extensions;
}

// NOLINTEND(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay)
