// SPDX-License-Identifier: GPL-2.0-only
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "frontends/common/AudioDumper.h"

namespace {

auto read_u16_le(const uint8_t* p) -> uint16_t {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

auto read_u32_le(const uint8_t* p) -> uint32_t {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

TEST_CASE("AudioDumper: [AUD-1] Explicit lifecycle generates valid WAV file") {
  const char* wav_path = "test_dump_explicit.wav";
  unlink(wav_path);

  {
    AudioDumper_t dumper;
    REQUIRE(dumper.initialize(wav_path, 44100, 2) == true);
    CHECK(dumper.is_active() == true);

    std::vector<int16_t> samples(1024 * 2, 0x1234);
    REQUIRE(dumper.put_samples(samples.data(), samples.size()) == true);
    dumper.finalize();
    CHECK(dumper.is_active() == false);
  }

  // Verify WAV header
  FILE* f = fopen(wav_path, "rb");
  REQUIRE(f != nullptr);
  uint8_t header[44] = {0};
  REQUIRE(fread(header, 1, sizeof(header), f) == 44);

  CHECK(memcmp(header, "RIFF", 4) == 0);
  CHECK(memcmp(header + 8, "WAVEfmt ", 8) == 0);
  CHECK(read_u16_le(header + 20) == 1);      // PCM format
  CHECK(read_u16_le(header + 22) == 2);      // 2 channels
  CHECK(read_u32_le(header + 24) == 44100);  // sample rate
  CHECK(read_u16_le(header + 34) == 16);     // 16 bits per sample
  CHECK(memcmp(header + 36, "data", 4) == 0);

  const uint32_t data_size = read_u32_le(header + 40);
  const uint32_t riff_size = read_u32_le(header + 4);
  CHECK(data_size == 1024 * 2 * sizeof(int16_t));
  CHECK(riff_size == data_size + 36);

  fclose(f);
  unlink(wav_path);
}

TEST_CASE(
    "AudioDumper: [AUD-2] RAII destruction automatically finalizes and patches "
    "WAV header") {
  const char* wav_path = "test_dump_raii.wav";
  unlink(wav_path);

  {
    AudioDumper_t dumper;
    REQUIRE(dumper.initialize(wav_path, 44100, 2) == true);
    std::vector<int16_t> samples(512 * 2, 0x0505);
    REQUIRE(dumper.put_samples(samples.data(), samples.size()) == true);
    // Destroy dumper without explicit finalize()
  }

  // Verify WAV header was patched on destruction
  FILE* f = fopen(wav_path, "rb");
  REQUIRE(f != nullptr);
  uint8_t header[44] = {0};
  REQUIRE(fread(header, 1, sizeof(header), f) == 44);

  const uint32_t data_size = read_u32_le(header + 40);
  const uint32_t riff_size = read_u32_le(header + 4);
  CHECK(data_size == 512 * 2 * sizeof(int16_t));
  CHECK(riff_size == data_size + 36);

  fclose(f);
  unlink(wav_path);
}

TEST_CASE(
    "AudioDumper: [AUD-3] Thread safety during concurrent write and finalize") {
  const char* wav_path = "test_dump_concurrent.wav";
  unlink(wav_path);

  AudioDumper_t dumper;
  REQUIRE(dumper.initialize(wav_path, 44100, 2) == true);

  std::thread writer([&dumper]() {
    std::vector<int16_t> samples(256 * 2, 0x0101);
    for (int i = 0; i < 100; ++i) {
      dumper.put_samples(samples.data(), samples.size());
      std::this_thread::yield();
    }
  });

  std::thread finalizer([&dumper]() {
    std::this_thread::yield();
    dumper.finalize();
  });

  writer.join();
  finalizer.join();

  CHECK(dumper.is_active() == false);
  unlink(wav_path);
}
