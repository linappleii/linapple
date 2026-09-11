// SPDX-License-Identifier: GPL-2.0-only
#include "HeadlessHarness.h"

#include <zlib.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Registry.h"
#include "core/Util_Text.h"
#include "doctest.h"
#include "frontends/common/AppConfig.h"
#include "frontends/common/AppController.h"
#include "frontends/common/AppEnvironment.h"

namespace {

static HeadlessHarness_t* s_active_harness = nullptr;

auto on_audio(const int16_t* samples, size_t num_samples) -> void {
  if (s_active_harness != nullptr) {
    s_active_harness->handle_audio(samples, num_samples);
  }
}

}  // namespace

HeadlessHarness_t::HeadlessHarness_t() {
  s_active_harness = this;

  AppConfig_t config = {};
  app_config_default(&config);
  app_env_resolve_paths(&config);

  app_controller_initialize(&config);

  video_set_rendering_enabled(false);
  linapple_set_audio_callback(on_audio);

  is_initialized = true;
}

HeadlessHarness_t::~HeadlessHarness_t() {
  if (is_initialized) {
    video_set_rendering_enabled(true);
    linapple_set_audio_callback(nullptr);
    app_controller_shutdown();
    is_initialized = false;
  }
  if (s_active_harness == this) {
    s_active_harness = nullptr;
  }
}

auto HeadlessHarness_t::mount_disk(int slot, int drive, const std::string& path)
    -> void {
  const char* reg_key =
      (drive == 0) ? REGVALUE_DISK_IMAGE1 : REGVALUE_DISK_IMAGE2;
  Configuration_t::instance().set_string("Slots", reg_key, path);

  DiskInsertCmd_t cmd{};
  cmd.drive = (drive == 0) ? disk_drive_0 : disk_drive_1;
  util_safe_strcpy(cmd.path, path.c_str(), disk_insert_path_max);
  cmd.write_protected = 0;
  cmd.create_if_necessary = 0;
  peripheral_command(slot, disk_cmd_insert, &cmd, sizeof(cmd));
  peripheral_manager_think(100);
}

auto HeadlessHarness_t::boot() -> void {
  linapple_reset_hard();
  g_state.mode = MODE_RUNNING;
}

auto HeadlessHarness_t::reset_soft() -> void {
  linapple_reset_soft();
  g_state.mode = MODE_RUNNING;
}

auto HeadlessHarness_t::run_frames(uint32_t count) -> void {
  constexpr int apple2_frame_cycles = 17030;
  for (uint32_t i = 0; i < count; ++i) {
    linapple_run_frame(apple2_frame_cycles);
  }
}

auto HeadlessHarness_t::type_string(const std::string& text,
                                    uint32_t frames_per_stroke) -> void {
  for (char c : text) {
    uint8_t key = static_cast<uint8_t>(c);
    if (c == '\n' || c == '\r') {
      key = linapple_key_return;
    }
    linapple_set_key_state(key, true);
    run_frames(frames_per_stroke);
    linapple_set_key_state(key, false);
    run_frames(frames_per_stroke);
  }
}

auto HeadlessHarness_t::get_frame_crc32() const -> uint32_t {
  const uint32_t* pixels = nullptr;
  size_t pixel_count = 560 * 384;
  if (!last_frame.empty()) {
    pixels = last_frame.data();
    pixel_count = last_frame.size();
  } else {
    video_redraw_screen();
    pixels = video_get_output_buffer();
  }
  if (pixels == nullptr) {
    return 0;
  }
  unsigned long crc = crc32(0L, nullptr, 0);
  crc = crc32(crc, reinterpret_cast<const unsigned char*>(pixels),
              static_cast<unsigned int>(pixel_count * sizeof(uint32_t)));
  return static_cast<uint32_t>(crc);
}

auto HeadlessHarness_t::get_text_row(int row, bool trim_trailing) const
    -> std::string {
  if (row < 0 || row >= 24 || mem == nullptr) {
    return "";
  }
  static const std::array<uint16_t, 24> row_offsets = {
      0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,
      0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,
      0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0};

  std::string row_str;
  row_str.reserve(40);
  for (int col = 0; col < 40; ++col) {
    uint16_t addr = 0x0400 + row_offsets.at(static_cast<size_t>(row)) +
                    static_cast<uint16_t>(col);
    uint8_t code = mem[addr];
    char ch = static_cast<char>(code & 0x7F);
    if (ch < 0x20) {
      ch = (ch == 0) ? '@' : static_cast<char>('A' + ch - 1);
    }
    row_str.push_back(ch);
  }
  if (trim_trailing) {
    while (!row_str.empty() &&
           (row_str.back() == ' ' ||
            static_cast<unsigned char>(row_str.back()) == 0x7F)) {
      row_str.pop_back();
    }
  }
  return row_str;
}

auto HeadlessHarness_t::get_audio_sample_count() const -> size_t {
  return total_audio_samples;
}

auto HeadlessHarness_t::assert_screen_matches(uint32_t golden_crc) const
    -> void {
  CHECK(get_frame_crc32() == golden_crc);
}

auto HeadlessHarness_t::handle_video(const uint32_t* pixels, int width,
                                     int height, int pitch) -> void {
  (void)pitch;
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return;
  }
  size_t total_pixels = static_cast<size_t>(width * height);
  if (last_frame.size() != total_pixels) {
    last_frame.resize(total_pixels);
  }
  std::memcpy(last_frame.data(), pixels, total_pixels * sizeof(uint32_t));
}

auto HeadlessHarness_t::handle_audio(const int16_t* samples, size_t num_samples)
    -> void {
  (void)samples;
  total_audio_samples += num_samples;
}
