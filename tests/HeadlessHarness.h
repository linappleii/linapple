// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "test_fixtures.h"

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

  // There is no ambient configuration path: every harness is built from a
  // config the test declared.
  explicit HeadlessHarness_t(const TestFixtures::ScopedTestConfig_t& config);
  ~HeadlessHarness_t();

  // Non-copyable, non-movable (stack-scoped test fixture)
  HeadlessHarness_t(const HeadlessHarness_t&) = delete;
  auto operator=(const HeadlessHarness_t&) -> HeadlessHarness_t& = delete;
  HeadlessHarness_t(HeadlessHarness_t&&) = delete;
  auto operator=(HeadlessHarness_t&&) -> HeadlessHarness_t& = delete;

  // Control
  auto mount_disk(int slot, int drive, const std::string& path) -> void;
  auto mount_disk(int slot, int drive,
                  const TestFixtures::EphemeralDiskFixture_t& disk) -> void {
    mount_disk(slot, drive, disk.path());
  }
  auto boot() -> void;
  auto reset_soft() -> void;
  auto run_frames(uint32_t count) -> void;
  auto type_string(const std::string& text, uint32_t frames_per_stroke = 1)
      -> void;

  // Golden / Inspection
  auto get_frame_crc32() const -> uint32_t;
  auto get_text_row(int row, bool trim_trailing = true) const -> std::string;
  auto get_audio_sample_count() const -> size_t;
  auto assert_screen_matches(uint32_t golden_crc) const -> void;

  // Internal callback dispatchers
  auto handle_audio(const int16_t* samples, size_t num_samples) -> void;
};
