// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/disk/formats/DiskContainer.h"

#include <strings.h>
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

#include "core/Util_Path.h"
#include "core/Util_Text.h"

// Justification: Domain-specific container detection requires parameters
// mandated by the shared format probing signatures. Pointer arithmetic and
// array decay are required for physical bitstream inspection and decompression
// library ABIs.
// NOLINTBEGIN(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay)

namespace macbinary {
namespace {
// Header layout per the MacBinary II standard (1987) and the MacBinary III
// standard (1996). The zero-fill bytes are the ones the standard says a reader
// must check; 122 carries the
// writer's version (129 = II, 130 = III) and 123 the minimum version a
// reader needs (129 for both), and 124-125 hold the CRC-16 of bytes 0-123.
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

constexpr const char* k_gzip_extension = "gz";
constexpr const char* k_zip_extension = "zip";
const char* const k_supported_extensions[] = {k_gzip_extension, k_zip_extension,
                                              nullptr};

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
  int64_t bytes_read = 0;

  while ((bytes_read = zip_fread(file_in_zip, buffer.data(), buffer.size())) >
         0) {
    total_written += static_cast<size_t>(bytes_read);

    if (total_written > uncompressed_threshold) {
      if (compressed_entry_size == 0 ||
          total_written >
              compressed_entry_size * disk_container::compression_ratio_limit) {
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

// Why: Images that travelled through a Macintosh often still wear the 128-byte
// MacBinary header that carried their resource fork and Finder info. The
// version bytes alone are a heuristic; the standard's own test is the CRC over
// the header, so both are required. MacBinary I has no CRC and is not
// recognised: its only marks (zero at 0, 74 and 82) occur in ordinary images.
extern "C" auto disk_container_detect_macbinary(const uint8_t* header_data,
                                                size_t header_size,
                                                uint32_t file_size)
    -> uint32_t {
  if (header_data == nullptr || header_size < macbinary::header_size ||
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

// Why: A probe that sees only "game.dsk.gz" asks every driver about a ".gz",
// which none of them handle. The extension that decides the format is the
// payload's, so the container is the only layer that can supply it.
extern "C" auto disk_container_payload_name(const char* image_path,
                                            char* out_name, size_t max_name_len)
    -> bool {
  if (image_path == nullptr || out_name == nullptr || max_name_len == 0) {
    return false;
  }

  const char* slash = strrchr(image_path, '/');
  const char* basename = (slash != nullptr) ? (slash + 1) : image_path;

  const bool is_gz = has_extension(image_path, k_gzip_extension);
  const bool is_zip = has_extension(image_path, k_zip_extension);
  if (!is_gz && !is_zip) {
    util_safe_strcpy(out_name, basename, max_name_len);
    return true;
  }

  if (is_zip) {
    int err = 0;
    zip* archive = zip_open(image_path, ZIP_RDONLY, &err);
    if (archive != nullptr) {
      const std::unique_ptr<zip, int (*)(zip*)> closer(archive, zip_close);
      const char* entry = zip_get_name(archive, 0, 0);
      if (entry != nullptr && entry[0] != '\0') {
        const char* entry_slash = strrchr(entry, '/');
        util_safe_strcpy(out_name,
                         (entry_slash != nullptr) ? (entry_slash + 1) : entry,
                         max_name_len);
        return true;
      }
    }
  }

  // Dropping the archive suffix is all that is left: gzip's own FNAME field is
  // optional and zlib does not expose it.
  const std::string stripped(
      basename, strlen(basename) -
                    strlen(is_gz ? k_gzip_extension : k_zip_extension) - 1);
  util_safe_strcpy(out_name, stripped.c_str(), max_name_len);
  return true;
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

  const bool is_gz = has_extension(image_path, k_gzip_extension);
  const bool is_zip = has_extension(image_path, k_zip_extension);

  if (!is_gz && !is_zip) {
    util_safe_strcpy(out_load_path, image_path, max_path_len);
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
  util_safe_strcpy(out_load_path, temp_template.c_str(), max_path_len);

  // NOLINTNEXTLINE(misc-include-cleaner)
  int fd = mkstemp(out_load_path);
  if (fd == -1) {
    return false;
  }

  // NOLINTNEXTLINE(misc-include-cleaner)
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

extern "C" auto disk_container_supported_extensions(void) -> const
    char* const* {
  return k_supported_extensions;
}

// NOLINTEND(bugprone-easily-swappable-parameters, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay)
