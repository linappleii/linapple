// SPDX-License-Identifier: GPL-2.0-only

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "core/Util_Path.h"
#include "doctest.h"
#include "frontends/common/AudioDumper.h"
#include "test_fixtures.h"

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
  TestFixtures::ScopedTempFile_t temp_wav(".wav");

  {
    AudioDumper_t dumper;
    REQUIRE(dumper.initialize(temp_wav.c_str(), 44100, 2) == true);
    CHECK(dumper.is_active() == true);

    std::vector<int16_t> samples(1024 * 2, 0x1234);
    REQUIRE(dumper.put_samples(samples.data(), samples.size()) == true);
    dumper.finalize();
    CHECK(dumper.is_active() == false);
  }

  // Verify WAV header
  FilePtr_t f(fopen(temp_wav.c_str(), "rb"), fclose);
  REQUIRE(f != nullptr);
  uint8_t header[44] = {0};
  REQUIRE(fread(header, 1, sizeof(header), f.get()) == 44);

  CHECK(memcmp(header, "RIFF", 4) == 0);
  CHECK(memcmp(header + 8, "WAVEfmt ", 8) == 0);
  CHECK(read_u32_le(header + 16) == 16);         // Subchunk1Size (16 for PCM)
  CHECK(read_u16_le(header + 20) == 1);          // PCM format
  CHECK(read_u16_le(header + 22) == 2);          // 2 channels
  CHECK(read_u32_le(header + 24) == 44100);      // sample rate
  CHECK(read_u32_le(header + 28) == 44100 * 4);  // byte rate (44100 * 2 * 2)
  CHECK(read_u16_le(header + 32) == 4);          // block align (2 * 2)
  CHECK(read_u16_le(header + 34) == 16);         // 16 bits per sample
  CHECK(memcmp(header + 36, "data", 4) == 0);

  const uint32_t data_size = read_u32_le(header + 40);
  const uint32_t riff_size = read_u32_le(header + 4);
  CHECK(data_size == 1024 * 2 * sizeof(int16_t));
  CHECK(riff_size == data_size + 36);

  // Verify sample payload
  std::vector<int16_t> read_samples(1024 * 2);
  REQUIRE(fread(read_samples.data(), sizeof(int16_t), read_samples.size(),
                f.get()) == read_samples.size());
  CHECK(read_samples == std::vector<int16_t>(1024 * 2, 0x1234));
}

TEST_CASE(
    "AudioDumper: [AUD-2] RAII destruction automatically finalizes and patches "
    "WAV header") {
  TestFixtures::ScopedTempFile_t temp_wav(".wav");

  {
    AudioDumper_t dumper;
    REQUIRE(dumper.initialize(temp_wav.c_str(), 44100, 2) == true);
    std::vector<int16_t> samples(512 * 2, 0x0505);
    REQUIRE(dumper.put_samples(samples.data(), samples.size()) == true);
    // Destroy dumper without explicit finalize()
  }

  // Verify WAV header was patched on destruction
  FilePtr_t f(fopen(temp_wav.c_str(), "rb"), fclose);
  REQUIRE(f != nullptr);
  uint8_t header[44] = {0};
  REQUIRE(fread(header, 1, sizeof(header), f.get()) == 44);

  CHECK(memcmp(header, "RIFF", 4) == 0);
  CHECK(memcmp(header + 8, "WAVEfmt ", 8) == 0);
  CHECK(read_u32_le(header + 16) == 16);
  CHECK(read_u16_le(header + 20) == 1);          // PCM format
  CHECK(read_u16_le(header + 22) == 2);          // 2 channels
  CHECK(read_u32_le(header + 24) == 44100);      // sample rate
  CHECK(read_u32_le(header + 28) == 44100 * 4);  // byte rate
  CHECK(read_u16_le(header + 32) == 4);          // block align
  CHECK(read_u16_le(header + 34) == 16);         // 16 bits per sample
  CHECK(memcmp(header + 36, "data", 4) == 0);

  const uint32_t data_size = read_u32_le(header + 40);
  const uint32_t riff_size = read_u32_le(header + 4);
  CHECK(data_size == 512 * 2 * sizeof(int16_t));
  CHECK(riff_size == data_size + 36);

  std::vector<int16_t> read_samples(512 * 2);
  REQUIRE(fread(read_samples.data(), sizeof(int16_t), read_samples.size(),
                f.get()) == read_samples.size());
  CHECK(read_samples == std::vector<int16_t>(512 * 2, 0x0505));
}

TEST_CASE(
    "AudioDumper: [AUD-3] Thread safety during concurrent write and finalize") {
  TestFixtures::ScopedTempFile_t temp_wav(".wav");

  AudioDumper_t dumper;
  REQUIRE(dumper.initialize(temp_wav.c_str(), 44100, 2) == true);

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

  // Verify file integrity after concurrent termination
  FilePtr_t f(fopen(temp_wav.c_str(), "rb"), fclose);
  REQUIRE(f != nullptr);
  uint8_t header[44] = {0};
  REQUIRE(fread(header, 1, sizeof(header), f.get()) == 44);

  CHECK(memcmp(header, "RIFF", 4) == 0);
  CHECK(memcmp(header + 8, "WAVEfmt ", 8) == 0);
  CHECK(read_u16_le(header + 20) == 1);
  CHECK(read_u16_le(header + 22) == 2);
  CHECK(read_u32_le(header + 24) == 44100);
  CHECK(read_u16_le(header + 34) == 16);
  CHECK(memcmp(header + 36, "data", 4) == 0);

  const uint32_t data_size = read_u32_le(header + 40);
  const uint32_t riff_size = read_u32_le(header + 4);
  CHECK(riff_size == data_size + 36);
  CHECK(data_size % (2 * sizeof(int16_t)) == 0);
}

TEST_CASE(
    "AudioDumper: [AUD-4] Invalid initialization parameters and boundary "
    "conditions") {
  AudioDumper_t dumper;
  CHECK(dumper.is_active() == false);

  int16_t dummy_sample = 0;
  // Attempt to put samples on uninitialized dumper
  CHECK(dumper.put_samples(&dummy_sample, 1) == false);
  CHECK(dumper.put_samples(nullptr, 1) == false);
  CHECK(dumper.put_samples(&dummy_sample, 0) == false);

  // Attempt to initialize with invalid parameters
  TestFixtures::ScopedTempFile_t temp_wav(".wav");
  CHECK(dumper.initialize(nullptr, 44100, 2) == false);
  CHECK(dumper.initialize(temp_wav.c_str(), 0, 2) == false);
  CHECK(dumper.initialize(temp_wav.c_str(), 44100, 0) == false);
  CHECK(dumper.is_active() == false);
}

TEST_CASE("AudioDumper: [AUD-5] C API wrappers lifecycle and error handling") {
  TestFixtures::ScopedTempFile_t temp_wav(".wav");
  int16_t dummy_sample = 0x1111;

  // Null pointer checks
  CHECK(audio_dumper_initialize(nullptr, temp_wav.c_str(), 44100, 2) == 1);
  CHECK(audio_dumper_put_samples(nullptr, &dummy_sample, 1) == 1);
  CHECK(audio_dumper_finalize(nullptr) == 1);

  // Normal lifecycle via C API
  AudioDumper_t dumper;
  CHECK(audio_dumper_initialize(&dumper, temp_wav.c_str(), 22050, 1) == 0);
  CHECK(dumper.is_active() == true);
  CHECK(audio_dumper_put_samples(&dumper, &dummy_sample, 1) == 0);
  CHECK(audio_dumper_finalize(&dumper) == 0);
  CHECK(dumper.is_active() == false);

  // Verify WAV header for 22050 Hz mono
  FilePtr_t f(fopen(temp_wav.c_str(), "rb"), fclose);
  REQUIRE(f != nullptr);
  uint8_t header[44] = {0};
  REQUIRE(fread(header, 1, sizeof(header), f.get()) == 44);

  CHECK(memcmp(header, "RIFF", 4) == 0);
  CHECK(memcmp(header + 8, "WAVEfmt ", 8) == 0);
  CHECK(read_u16_le(header + 20) == 1);
  CHECK(read_u16_le(header + 22) == 1);          // 1 channel
  CHECK(read_u32_le(header + 24) == 22050);      // 22050 Hz
  CHECK(read_u32_le(header + 28) == 22050 * 2);  // 22050 * 1 * 2
  CHECK(read_u16_le(header + 32) == 2);          // block align (1 * 2)
  CHECK(read_u16_le(header + 34) == 16);
  CHECK(memcmp(header + 36, "data", 4) == 0);
  CHECK(read_u32_le(header + 40) == sizeof(int16_t));
}

TEST_CASE("AudioDumper: [AUD-6] Move semantics transfer active state") {
  TestFixtures::ScopedTempFile_t temp_wav(".wav");

  AudioDumper_t dumper1;
  REQUIRE(dumper1.initialize(temp_wav.c_str(), 44100, 2) == true);
  CHECK(dumper1.is_active() == true);

  // Move construct
  AudioDumper_t dumper2(std::move(dumper1));
  CHECK(dumper2.is_active() == true);

  std::vector<int16_t> samples(256 * 2, 0x0202);
  REQUIRE(dumper2.put_samples(samples.data(), samples.size()) == true);

  // Move assign
  AudioDumper_t dumper3;
  dumper3 = std::move(dumper2);
  CHECK(dumper3.is_active() == true);
  dumper3.finalize();
  CHECK(dumper3.is_active() == false);

  FilePtr_t f(fopen(temp_wav.c_str(), "rb"), fclose);
  REQUIRE(f != nullptr);
  uint8_t header[44] = {0};
  REQUIRE(fread(header, 1, sizeof(header), f.get()) == 44);
  CHECK(read_u32_le(header + 40) == 256 * 2 * sizeof(int16_t));
}
