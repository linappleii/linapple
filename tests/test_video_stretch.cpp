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

TEST_CASE("VideoStretch - Direct 16-bit RGB565 Output") {
  auto get_pixel16 = [](const VideoSurface* s, int x, int y) -> uint16_t {
    const auto* row = reinterpret_cast<const uint16_t*>(
        s->pixels + (static_cast<std::ptrdiff_t>(y) * s->pitch));
    return row[x];
  };
  auto fill16 = [](VideoSurface* s, uint16_t val) -> void {
    for (int y = 0; y < s->h; ++y) {
      auto* row = reinterpret_cast<uint16_t*>(
          s->pixels + (static_cast<std::ptrdiff_t>(y) * s->pitch));
      for (int x = 0; x < s->w; ++x) {
        row[x] = val;
      }
    }
  };

  SUBCASE("4->2 1:1, alpha ignore, and 2x RGB565 packing") {
    UniqueSurface src = make_surface(4, 2, 4);
    UniqueSurface dst1x = make_surface(4, 2, 2);
    UniqueSurface dst2x = make_surface(8, 4, 2);
    REQUIRE(src != nullptr);
    REQUIRE(dst1x != nullptr);
    REQUIRE(dst2x != nullptr);

    set_pixel32(src.get(), 0, 0, 0xAAFF0000);  // Red (non-zero alpha) -> 0xF800
    set_pixel32(src.get(), 1, 0, 0x0000FF00);  // Green -> 0x07E0
    set_pixel32(src.get(), 2, 0, 0x000000FF);  // Blue  -> 0x001F
    set_pixel32(src.get(), 3, 0, 0x00FFFFFF);  // White -> 0xFFFF
    set_pixel32(src.get(), 0, 1, 0x00808080);  // Mid   -> 0x8410

    CHECK(video_soft_stretch(src.get(), nullptr, dst1x.get(), nullptr) == 0);
    CHECK(get_pixel16(dst1x.get(), 0, 0) == 0xF800);
    CHECK(get_pixel16(dst1x.get(), 1, 0) == 0x07E0);
    CHECK(get_pixel16(dst1x.get(), 2, 0) == 0x001F);
    CHECK(get_pixel16(dst1x.get(), 3, 0) == 0xFFFF);
    CHECK(get_pixel16(dst1x.get(), 0, 1) == 0x8410);

    CHECK(video_soft_stretch(src.get(), nullptr, dst2x.get(), nullptr) == 0);
    CHECK(get_pixel16(dst2x.get(), 0, 0) == 0xF800);
    CHECK(get_pixel16(dst2x.get(), 1, 0) == 0xF800);
    CHECK(get_pixel16(dst2x.get(), 0, 1) == 0xF800);
    CHECK(get_pixel16(dst2x.get(), 2, 0) == 0x07E0);
    CHECK(get_pixel16(dst2x.get(), 6, 1) == 0xFFFF);
    CHECK(get_pixel16(dst2x.get(), 1, 3) == 0x8410);
  }

  SUBCASE("4->2 fractional 1.75x, left/right clipping, and off-screen rect") {
    UniqueSurface src = make_surface(4, 2, 4);
    UniqueSurface dst = make_surface(7, 4, 2);
    REQUIRE(src != nullptr);
    REQUIRE(dst != nullptr);

    set_pixel32(src.get(), 0, 0, 0x00FF0000);
    set_pixel32(src.get(), 1, 0, 0x0000FF00);
    set_pixel32(src.get(), 2, 0, 0x000000FF);
    set_pixel32(src.get(), 3, 0, 0x00FFFFFF);
    set_pixel32(src.get(), 3, 1, 0x000000FF);

    // 4 -> 7 mapping: floor(i * 4 / 7) = [0, 0, 1, 1, 2, 2, 3]
    fill16(dst.get(), 0xAAAA);
    CHECK(video_soft_stretch(src.get(), nullptr, dst.get(), nullptr) == 0);
    CHECK(get_pixel16(dst.get(), 0, 0) == 0xF800);
    CHECK(get_pixel16(dst.get(), 1, 0) == 0xF800);
    CHECK(get_pixel16(dst.get(), 2, 0) == 0x07E0);
    CHECK(get_pixel16(dst.get(), 4, 0) == 0x001F);
    CHECK(get_pixel16(dst.get(), 6, 0) == 0xFFFF);
    CHECK(get_pixel16(dst.get(), 6, 3) == 0x001F);

    // Left-clipped 2x fallback (dst_x = -1 shifts output left by 1)
    fill16(dst.get(), 0xAAAA);
    VideoRect left_clipped = {-1, 0, 8, 4};
    CHECK(video_soft_stretch(src.get(), nullptr, dst.get(), &left_clipped) ==
          0);
    CHECK(get_pixel16(dst.get(), 0, 0) == 0xF800);  // i=1 -> src 0
    CHECK(get_pixel16(dst.get(), 1, 0) == 0x07E0);  // i=2 -> src 1
    CHECK(get_pixel16(dst.get(), 3, 0) == 0x001F);  // i=4 -> src 2
    CHECK(get_pixel16(dst.get(), 5, 0) == 0xFFFF);  // i=6 -> src 3
    CHECK(get_pixel16(dst.get(), 6, 3) == 0x001F);  // i=7 -> src 3

    // Right-clipped 2x fallback (dst_x = 1, dst_w = 8 > max_w = 7)
    fill16(dst.get(), 0xAAAA);
    VideoRect right_clipped = {1, 0, 8, 4};
    CHECK(video_soft_stretch(src.get(), nullptr, dst.get(), &right_clipped) ==
          0);
    CHECK(get_pixel16(dst.get(), 0, 0) == 0xAAAA);  // untouched left column
    CHECK(get_pixel16(dst.get(), 1, 0) == 0xF800);  // i=0 -> src 0
    CHECK(get_pixel16(dst.get(), 3, 0) == 0x07E0);  // i=2 -> src 1
    CHECK(get_pixel16(dst.get(), 5, 0) == 0x001F);  // i=4 -> src 2

    // Completely off-screen destination leaves surface untouched
    fill16(dst.get(), 0x5555);
    VideoRect offscreen = {10, 0, 8, 4};
    CHECK(video_soft_stretch(src.get(), nullptr, dst.get(), &offscreen) == 0);
    CHECK(get_pixel16(dst.get(), 0, 0) == 0x5555);
  }

  SUBCASE("1->2 indexed palette (1:1, 2x, fractional, and palette swap)") {
    UniqueSurface src_a = make_surface(2, 1, 1);
    UniqueSurface src_b = make_surface(2, 1, 1);
    UniqueSurface dst1x = make_surface(2, 1, 2);
    UniqueSurface dst2x = make_surface(4, 2, 2);
    UniqueSurface dst3x = make_surface(3, 1, 2);
    REQUIRE(src_a != nullptr);
    REQUIRE(src_b != nullptr);
    REQUIRE(dst1x != nullptr);
    REQUIRE(dst2x != nullptr);
    REQUIRE(dst3x != nullptr);

    src_a->palette[1] = VideoColor{0xFF, 0x00, 0x00, 0xFF};
    src_a->palette[2] = VideoColor{0x00, 0xFF, 0x00, 0xFF};
    src_a->pixels[0] = 1;
    src_a->pixels[1] = 2;

    CHECK(video_soft_stretch(src_a.get(), nullptr, dst1x.get(), nullptr) == 0);
    CHECK(get_pixel16(dst1x.get(), 0, 0) == 0xF800);
    CHECK(get_pixel16(dst1x.get(), 1, 0) == 0x07E0);

    CHECK(video_soft_stretch(src_a.get(), nullptr, dst2x.get(), nullptr) == 0);
    CHECK(get_pixel16(dst2x.get(), 0, 0) == 0xF800);
    CHECK(get_pixel16(dst2x.get(), 1, 1) == 0xF800);
    CHECK(get_pixel16(dst2x.get(), 2, 0) == 0x07E0);
    CHECK(get_pixel16(dst2x.get(), 3, 1) == 0x07E0);

    // 2 -> 3 fractional: floor(i * 2 / 3) = [0, 0, 1]
    CHECK(video_soft_stretch(src_a.get(), nullptr, dst3x.get(), nullptr) == 0);
    CHECK(get_pixel16(dst3x.get(), 0, 0) == 0xF800);
    CHECK(get_pixel16(dst3x.get(), 1, 0) == 0xF800);
    CHECK(get_pixel16(dst3x.get(), 2, 0) == 0x07E0);

    // Switching to a different palette pointer must refresh palette_lut16
    src_b->palette[1] = VideoColor{0x00, 0x00, 0xFF, 0xFF};
    src_b->palette[2] = VideoColor{0xFF, 0xFF, 0xFF, 0xFF};
    src_b->pixels[0] = 1;
    src_b->pixels[1] = 2;
    CHECK(video_soft_stretch(src_b.get(), nullptr, dst1x.get(), nullptr) == 0);
    CHECK(get_pixel16(dst1x.get(), 0, 0) == 0x001F);
    CHECK(get_pixel16(dst1x.get(), 1, 0) == 0xFFFF);
  }

  SUBCASE("3->2, 2->2, and 2->4 RGB565 conversions") {
    UniqueSurface src3 = make_surface(2, 1, 3);
    UniqueSurface dst2 = make_surface(2, 1, 2);
    UniqueSurface dst2_copy = make_surface(2, 1, 2);
    UniqueSurface dst4 = make_surface(2, 1, 4);
    REQUIRE(src3 != nullptr);
    REQUIRE(dst2 != nullptr);
    REQUIRE(dst2_copy != nullptr);
    REQUIRE(dst4 != nullptr);

    src3->pixels[0] = 0xFF;
    src3->pixels[1] = 0x00;
    src3->pixels[2] = 0x00;
    src3->pixels[3] = 0x00;
    src3->pixels[4] = 0xFF;
    src3->pixels[5] = 0x00;

    CHECK(video_soft_stretch(src3.get(), nullptr, dst2.get(), nullptr) == 0);
    CHECK(get_pixel16(dst2.get(), 0, 0) == 0xF800);
    CHECK(get_pixel16(dst2.get(), 1, 0) == 0x07E0);

    CHECK(video_soft_stretch(dst2.get(), nullptr, dst2_copy.get(), nullptr) ==
          0);
    CHECK(get_pixel16(dst2_copy.get(), 0, 0) == 0xF800);
    CHECK(get_pixel16(dst2_copy.get(), 1, 0) == 0x07E0);

    CHECK(video_soft_stretch(dst2.get(), nullptr, dst4.get(), nullptr) == 0);
    CHECK(get_pixel32(dst4.get(), 0, 0) == 0x00FF0000);
    CHECK(get_pixel32(dst4.get(), 1, 0) == 0x0000FF00);
  }

  SUBCASE("video_soft_stretch_or into 16-bit RGB565 (1->2, 3->2, 4->2)") {
    UniqueSurface src1 = make_surface(2, 1, 1);
    UniqueSurface src3 = make_surface(2, 1, 3);
    UniqueSurface src4 = make_surface(2, 1, 4);
    UniqueSurface dst = make_surface(2, 1, 2);
    REQUIRE(src1 != nullptr);
    REQUIRE(src3 != nullptr);
    REQUIRE(src4 != nullptr);
    REQUIRE(dst != nullptr);

    src1->palette[1] = VideoColor{0xFF, 0x00, 0x00, 0xFF};
    src1->pixels[0] = 1;
    src1->pixels[1] = 0;

    src3->pixels[0] = 0x00;
    src3->pixels[1] = 0xFF;
    src3->pixels[2] = 0x00;
    src3->pixels[3] = 0x00;
    src3->pixels[4] = 0x00;
    src3->pixels[5] = 0xFF;

    set_pixel32(src4.get(), 0, 0, 0x000000FF);
    set_pixel32(src4.get(), 1, 0, 0x00FF0000);

    fill16(dst.get(), 0x0000);
    CHECK(video_soft_stretch_or(src1.get(), nullptr, dst.get(), nullptr) == 0);
    CHECK(get_pixel16(dst.get(), 0, 0) == 0xF800);
    CHECK(get_pixel16(dst.get(), 1, 0) == 0x0000);

    CHECK(video_soft_stretch_or(src3.get(), nullptr, dst.get(), nullptr) == 0);
    CHECK(get_pixel16(dst.get(), 0, 0) == (0xF800 | 0x07E0));
    CHECK(get_pixel16(dst.get(), 1, 0) == 0x001F);

    CHECK(video_soft_stretch_or(src4.get(), nullptr, dst.get(), nullptr) == 0);
    CHECK(get_pixel16(dst.get(), 0, 0) == 0xFFFF);
    CHECK(get_pixel16(dst.get(), 1, 0) == (0x001F | 0xF800));
  }
}
