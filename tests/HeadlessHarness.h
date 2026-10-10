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
struct HeadlessHarness {
  size_t total_audio_samples = 0;
  bool is_initialized = false;

  explicit HeadlessHarness(const TestFixtures::ScopedTestConfig& config);
  // The machine a user gets from the command line: the arguments are parsed as
  // the frontends parse them and the initial media loaded as they load it. The
  // declared configuration still names the machine, whatever the arguments say.
  HeadlessHarness(const TestFixtures::ScopedTestConfig& config, int argc,
                    char** argv);
  ~HeadlessHarness();

  // Non-copyable, non-movable (stack-scoped test fixture)
  HeadlessHarness(const HeadlessHarness&) = delete;
  auto operator=(const HeadlessHarness&) -> HeadlessHarness& = delete;
  HeadlessHarness(HeadlessHarness&&) = delete;
  auto operator=(HeadlessHarness&&) -> HeadlessHarness& = delete;

  // Control
  static auto mount_disk(int slot, int drive, const std::string& path) -> void;
  auto mount_disk(int slot, int drive,
                  const TestFixtures::EphemeralDiskFixture& disk) -> void {
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
  auto start(const TestFixtures::ScopedTestConfig& test_config,
             AppConfig* config) -> void;
};
