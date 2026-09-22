// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "apple2/media/image_container/ImageContainer.h"

extern "C" const char* __asan_default_options() { return "detect_leaks=1"; }

namespace {

// Small enough that the ratio, not the threshold, is the gate for almost every
// input, so one execution can write at most 100 x max_len of temporary file.
constexpr size_t fuzz_threshold = 64 * 1024;
constexpr size_t path_len = 512;

// A well-formed archive has to be written under its own suffix to reach its
// extractor, and the archive bytes themselves cannot choose it (gzip's first
// byte is fixed), so the first input byte does.
auto suffix_for(uint8_t selector) -> const char* {
  switch (selector % 3) {
    case 0:
      return ".gz";
    case 1:
      return ".zip";
    default:
      return ".dsk";
  }
}

auto temp_dir() -> std::string {
  const char* env = getenv("TMPDIR");
  return (env != nullptr && env[0] != '\0') ? env : "/tmp";
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 1) {
    return 0;
  }
  const char* suffix = suffix_for(data[0]);
  const uint8_t* payload = data + 1;
  const size_t payload_size = size - 1;

  const std::string input_template =
      temp_dir() + "/linapple_fuzz_ic_XXXXXX" + suffix;
  std::array<char, path_len> input_path{};
  if (input_template.size() >= input_path.size()) {
    return 0;
  }
  memcpy(input_path.data(), input_template.c_str(), input_template.size() + 1);

  const int fd = mkstemps(input_path.data(), static_cast<int>(strlen(suffix)));
  if (fd < 0) {
    return 0;
  }
  const ssize_t written = write(fd, payload, payload_size);
  close(fd);
  if (written != static_cast<ssize_t>(payload_size)) {
    unlink(input_path.data());
    return 0;
  }

  std::array<char, path_len> load_path{};
  bool is_temporary = false;
  const ImageContainerError_e prepared =
      image_container_prepare_compressed_path(
          input_path.data(), load_path.data(), load_path.size(), fuzz_threshold,
          &is_temporary);

  std::array<char, path_len> name{};
  image_container_payload_name(input_path.data(), name.data(), name.size());

  image_container_detect_macbinary(payload, payload_size,
                                   static_cast<uint32_t>(payload_size));
  image_container_supported_extensions();

  if (prepared == image_container_ok && is_temporary) {
    unlink(load_path.data());
  }
  unlink(input_path.data());
  return 0;
}
// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)
