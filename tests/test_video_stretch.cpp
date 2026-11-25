// SPDX-License-Identifier: GPL-2.0-only
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "core/Util_Crc32.h"
#include "doctest.h"
#include "frontends/common/VideoStretch.h"
#include "frontends/common/VideoSurface.h"
#include "test_fixtures.h"

namespace {

using UniqueSurface = std::unique_ptr<VideoSurface, void (*)(VideoSurface*)>;

auto make_surface(int width, int height, int bpp) -> UniqueSurface {
  return {video_create_surface(width, height, bpp), video_destroy_surface};
}

auto get_pixel32(const VideoSurface* s, int x, int y) -> uint32_t {
  if (x < 0 || x >= s->w || y < 0 || y >= s->h) {
    return 0;
  }
  const auto* row = reinterpret_cast<const uint32_t*>(
      s->pixels + (static_cast<std::ptrdiff_t>(y) * s->pitch));
  return row[x];
}

auto set_pixel32(VideoSurface* s, int x, int y, uint32_t val) -> void {
  if (x < 0 || x >= s->w || y < 0 || y >= s->h) {
    return;
  }
  auto* row = reinterpret_cast<uint32_t*>(
      s->pixels + (static_cast<std::ptrdiff_t>(y) * s->pitch));
  row[x] = val;
}

// Declared rather than inherited: the core is here for the video subsystem,
// and the slot fallbacks in peripheral_register_internal would build four
// cards nothing here touches.
using TestConfig = TestFixtures::ScopedTestConfig;

struct ScopedVideoFixture {
  ScopedVideoFixture() {
    machine.load();
    linapple_init();
    video_initialize();
  }

  ~ScopedVideoFixture() { linapple_shutdown(); }

  ScopedVideoFixture(const ScopedVideoFixture&) = delete;
  auto operator=(const ScopedVideoFixture&) -> ScopedVideoFixture& = delete;
  ScopedVideoFixture(ScopedVideoFixture&&) = delete;
  auto operator=(ScopedVideoFixture&&) -> ScopedVideoFixture& = delete;

  TestConfig machine{TestConfig::enhanced_2e_only()};
};

}  // namespace

TEST_CASE("VideoStretch - 1:1 RGB32 Soft Stretch") {
  UniqueSurface src = make_surface(560, 384, 4);
  UniqueSurface dst = make_surface(560, 384, 4);
  REQUIRE(src != nullptr);
  REQUIRE(dst != nullptr);

  set_pixel32(src.get(), 0, 0, 0x00112233);
  set_pixel32(src.get(), 559, 0, 0x00445566);
  set_pixel32(src.get(), 0, 383, 0x00778899);
  set_pixel32(src.get(), 559, 383, 0x00AABBCC);
  set_pixel32(src.get(), 280, 192, 0x00DDEEFF);

  int ret = video_soft_stretch(src.get(), nullptr, dst.get(), nullptr);
  CHECK(ret == 0);

  CHECK(get_pixel32(dst.get(), 0, 0) == 0x00112233);
  CHECK(get_pixel32(dst.get(), 559, 0) == 0x00445566);
  CHECK(get_pixel32(dst.get(), 0, 383) == 0x00778899);
  CHECK(get_pixel32(dst.get(), 559, 383) == 0x00AABBCC);
  CHECK(get_pixel32(dst.get(), 280, 192) == 0x00DDEEFF);
}

TEST_CASE("VideoStretch - 1:2 and 1:3 Scaling to Window Resolutions") {
  UniqueSurface src = make_surface(560, 384, 4);
  UniqueSurface dst_2x = make_surface(1120, 768, 4);
  UniqueSurface dst_3x = make_surface(1680, 1152, 4);
  REQUIRE(src != nullptr);
  REQUIRE(dst_2x != nullptr);
  REQUIRE(dst_3x != nullptr);

  set_pixel32(src.get(), 0, 0, 0x00FF0000);
  set_pixel32(src.get(), 559, 0, 0x0000FF00);
  set_pixel32(src.get(), 0, 383, 0x000000FF);
  set_pixel32(src.get(), 559, 383, 0x00FFFFFF);
  set_pixel32(src.get(), 280, 192, 0x00FFFF00);

  // 1:2 scaling test
  VideoRect src_rect = {0, 0, 560, 384};
  VideoRect dst_rect_2x = {0, 0, 1120, 768};
  int ret2 =
      video_soft_stretch(src.get(), &src_rect, dst_2x.get(), &dst_rect_2x);
  CHECK(ret2 == 0);

  // Check 2x mapped pixels at extremities
  CHECK(get_pixel32(dst_2x.get(), 0, 0) == 0x00FF0000);
  CHECK(get_pixel32(dst_2x.get(), 1, 0) == 0x00FF0000);
  CHECK(get_pixel32(dst_2x.get(), 0, 1) == 0x00FF0000);
  CHECK(get_pixel32(dst_2x.get(), 1, 1) == 0x00FF0000);

  CHECK(get_pixel32(dst_2x.get(), 1118, 0) == 0x0000FF00);
  CHECK(get_pixel32(dst_2x.get(), 1119, 0) == 0x0000FF00);
  CHECK(get_pixel32(dst_2x.get(), 0, 766) == 0x000000FF);
  CHECK(get_pixel32(dst_2x.get(), 0, 767) == 0x000000FF);
  CHECK(get_pixel32(dst_2x.get(), 1118, 766) == 0x00FFFFFF);
  CHECK(get_pixel32(dst_2x.get(), 1119, 767) == 0x00FFFFFF);
  CHECK(get_pixel32(dst_2x.get(), 560, 384) == 0x00FFFF00);

  // 1:3 scaling test
  VideoRect dst_rect_3x = {0, 0, 1680, 1152};
  int ret3 =
      video_soft_stretch(src.get(), &src_rect, dst_3x.get(), &dst_rect_3x);
  CHECK(ret3 == 0);

  CHECK(get_pixel32(dst_3x.get(), 0, 0) == 0x00FF0000);
  CHECK(get_pixel32(dst_3x.get(), 1679, 0) == 0x0000FF00);
  CHECK(get_pixel32(dst_3x.get(), 0, 1151) == 0x000000FF);
  CHECK(get_pixel32(dst_3x.get(), 1679, 1151) == 0x00FFFFFF);
  CHECK(get_pixel32(dst_3x.get(), 841, 577) == 0x00FFFF00);
}

TEST_CASE("VideoStretch - Sub-Rectangle and Viewport Positioning") {
  UniqueSurface src = make_surface(560, 384, 4);
  UniqueSurface dst = make_surface(1120, 768, 4);
  REQUIRE(src != nullptr);
  REQUIRE(dst != nullptr);

  set_pixel32(src.get(), 0, 0, 0x00123456);

  // Target a centered letterbox viewport in the destination: (100, 50, 800,
  // 600)
  VideoRect src_rect = {0, 0, 560, 384};
  VideoRect dst_rect = {100, 50, 800, 600};
  int ret = video_soft_stretch(src.get(), &src_rect, dst.get(), &dst_rect);
  CHECK(ret == 0);

  // Outside viewport remains 0
  CHECK(get_pixel32(dst.get(), 0, 0) == 0);
  CHECK(get_pixel32(dst.get(), 99, 50) == 0);
  CHECK(get_pixel32(dst.get(), 100, 49) == 0);

  // Inside viewport start
  CHECK(get_pixel32(dst.get(), 100, 50) == 0x00123456);
}

TEST_CASE("VideoStretch - Boundary and Null Safety Checks") {
  UniqueSurface src = make_surface(100, 100, 4);
  UniqueSurface dst = make_surface(100, 100, 4);
  REQUIRE(src != nullptr);
  REQUIRE(dst != nullptr);

  // Null surface handling safely returns error
  CHECK(video_soft_stretch(nullptr, nullptr, dst.get(), nullptr) == -1);
  CHECK(video_soft_stretch(src.get(), nullptr, nullptr, nullptr) == -1);

  // Zero-sized rectangles safely return error
  VideoRect zero_rect = {0, 0, 0, 0};
  CHECK(video_soft_stretch(src.get(), &zero_rect, dst.get(), nullptr) == -1);

  // Partial out of bounds rectangles are clipped without crashing
  VideoRect out_of_bounds_src = {-10, -10, 200, 200};
  CHECK(video_soft_stretch(src.get(), &out_of_bounds_src, dst.get(), nullptr) ==
        0);

  VideoRect out_of_bounds_dst = {50, 50, 200, 200};
  CHECK(video_soft_stretch(src.get(), nullptr, dst.get(), &out_of_bounds_dst) ==
        0);
}

TEST_CASE("Video - Mode Switch Preserves Drawn Screen") {
  ScopedVideoFixture fixture;

  // Write character 'A' (0xC1 in Apple II text memory) at (0,0) -> 0x400
  *mem_get_main_ptr(0x0400) = 0xC1;

  // Refresh screen to render 'A' into output buffer
  video_refresh_screen();

  const uint32_t* buf = video_get_output_buffer();
  REQUIRE(buf != nullptr);

  const uint32_t crc_before =
      crc32_compute(buf, static_cast<size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT *
                             sizeof(uint32_t));

  constexpr uint32_t expected_crc = 0x16D61EDC;
  CHECK(crc_before == expected_crc);

  // Switch video mode (F9 behavior)
  videotype = VT_COLOR_TEXT_OPTIMIZED;
  video_reinitialize();
  video_refresh_screen();

  const uint32_t crc_after =
      crc32_compute(buf, static_cast<size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT *
                             sizeof(uint32_t));

  CHECK(crc_after == expected_crc);
}

TEST_CASE("Video: Frame ready state transitions") {
  video_set_frame_ready(true);
  CHECK(video_is_frame_ready() == true);
  video_set_frame_ready(false);
  CHECK(video_is_frame_ready() == false);
  video_set_frame_ready(true);
  video_clear_frame_ready();
  CHECK(video_is_frame_ready() == false);
}

TEST_CASE("Video: Incremental dirty-row conversion matches full redraw") {
  ScopedVideoFixture fixture;

  const size_t output_bytes =
      static_cast<size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT * sizeof(uint32_t);
  const uint32_t* buf = video_get_output_buffer();
  REQUIRE(buf != nullptr);

  // Initial full draw.
  video_redraw_screen();
  const uint32_t initial_crc = crc32_compute(buf, output_bytes);

  // Quiet refresh with no memory changes keeps output identical.
  video_refresh_screen();
  CHECK(crc32_compute(buf, output_bytes) == initial_crc);

  // Modify top row (row 0, col 0 -> $0400) and verify against golden CRC.
  *mem_get_main_ptr(0x0400) = 0xC1;  // 'A'
  video_refresh_screen();
  constexpr uint32_t expected_a_crc = 0x16D61EDC;
  CHECK(crc32_compute(buf, output_bytes) == expected_a_crc);

  video_redraw_screen();
  CHECK(crc32_compute(buf, output_bytes) == expected_a_crc);

  // Dirty two non-adjacent rows spanning both the y<20 and y<24 loops in one
  // refresh (row 1 col 0 -> $0480, row 22 col 39 -> $0777).
  *mem_get_main_ptr(0x0480) = 0xC2;  // 'B'
  *mem_get_main_ptr(0x0777) = 0xDA;  // 'Z'
  video_refresh_screen();
  const uint32_t multi_row_crc = crc32_compute(buf, output_bytes);
  video_redraw_screen();
  CHECK(crc32_compute(buf, output_bytes) == multi_row_crc);

  // Status panel overlay followed by expiry must restore underlying pixels on
  // a quiet refresh with no Apple II memory changes.
  REQUIRE(status_surface != nullptr);
  std::memset(status_surface->pixels, WHITE,
              static_cast<size_t>(status_surface->pitch) * status_surface->h);
  const bool prev_show_leds = show_leds;
  show_leds = true;
  status_cycle = 2;
  video_refresh_screen();  // decrements to 1 -> panel visible
  const uint32_t with_panel_crc = crc32_compute(buf, output_bytes);
  CHECK(with_panel_crc != multi_row_crc);
  video_refresh_screen();  // decrements to 0 -> panel erased
  CHECK(crc32_compute(buf, output_bytes) == multi_row_crc);
  show_leds = prev_show_leds;

  // VT_COLOR_TVEMU hires vertical mixing writes 2 scanlines above and below
  // each modified HGR scanline; incremental refresh must match full redraw.
  videotype = VT_COLOR_TVEMU;
  video_reinitialize();
  video_set_mode(0, 0x50, 0, 0, 0);  // Graphics mode
  video_set_mode(0, 0x52, 0, 0, 0);  // Full-screen (non-mixed)
  video_set_mode(0, 0x57, 0, 0, 0);  // Hi-Res mode
  video_redraw_screen();

  // Modify top scanline (yoffset=0) of cell row 1 ($2080) and bottom scanline
  // (yoffset=0x1C00) of cell row 1 ($3C80) so mixing spills into rows 0 & 2.
  *mem_get_main_ptr(0x2080) = 0x7F;
  *mem_get_main_ptr(0x3C80) = 0x7F;
  video_refresh_screen();
  const uint32_t tvemu_inc_crc = crc32_compute(buf, output_bytes);
  video_redraw_screen();
  CHECK(crc32_compute(buf, output_bytes) == tvemu_inc_crc);
}
