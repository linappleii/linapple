// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "test_fixtures.h"

struct Configuration;
using AppConfig = Configuration;

/**
 * @brief Headless End-to-End Test Harness.
 *
 * Encapsulates headless emulator initialization, deterministic virtual frame
 * advancement, scripted keyboard typing, and output verification (framebuffer
 * CRC32, text row decoding, audio sampling).
 */
struct HeadlessHarness_t {
  size_t total_audio_samples = 0;
  bool is_initialized = false;

  explicit HeadlessHarness_t(const TestFixtures::ScopedTestConfig_t& config);
  // The machine a user gets from the command line: the arguments are parsed as
  // the frontends parse them and the initial media loaded as they load it. The
  // declared configuration still names the machine, whatever the arguments say.
  HeadlessHarness_t(const TestFixtures::ScopedTestConfig_t& config, int argc,
                    char** argv);
  ~HeadlessHarness_t();

  // Non-copyable, non-movable (stack-scoped test fixture)
  HeadlessHarness_t(const HeadlessHarness_t&) = delete;
  auto operator=(const HeadlessHarness_t&) -> HeadlessHarness_t& = delete;
  HeadlessHarness_t(HeadlessHarness_t&&) = delete;
  auto operator=(HeadlessHarness_t&&) -> HeadlessHarness_t& = delete;

  // Control
  static auto mount_disk(int slot, int drive, const std::string& path) -> void;
  auto mount_disk(int slot, int drive,
                  const TestFixtures::EphemeralDiskFixture_t& disk) -> void {
    mount_disk(slot, drive, disk.path());
  }
  static auto boot() -> void;
  static auto reset_soft() -> void;
  static auto run_frames(uint32_t count) -> void;
  static auto type_string(const std::string& text,
                          uint32_t frames_per_stroke = 1) -> void;

  // Golden / Inspection
  static auto get_frame_crc32() -> uint32_t;
  static auto get_text_row(int row, bool trim_trailing = true) -> std::string;
  auto get_audio_sample_count() const -> size_t;
  static auto assert_screen_matches(uint32_t golden_crc) -> void;

  // Internal callback dispatchers
  auto handle_audio(const int16_t* samples, size_t num_samples) -> void;

 private:
  auto start(const TestFixtures::ScopedTestConfig_t& test_config,
             AppConfig* config) -> void;
};
