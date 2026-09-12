// SPDX-License-Identifier: GPL-2.0-only
#include <zlib.h>

#include <cstdint>
#include <memory>

#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "core/LinAppleCore.h"
#include "doctest.h"
#include "frontends/common/VideoStretch.h"
#include "frontends/common/VideoSurface.h"

namespace {

using UniqueSurface_t =
    std::unique_ptr<VideoSurface_t, void (*)(VideoSurface_t*)>;

auto make_surface(int width, int height, int bpp) -> UniqueSurface_t {
  return UniqueSurface_t(video_create_surface(width, height, bpp),
                         video_destroy_surface);
}

auto get_pixel32(const VideoSurface_t* s, int x, int y) -> uint32_t {
  if (x < 0 || x >= s->w || y < 0 || y >= s->h) {
    return 0;
  }
  const auto* row =
      reinterpret_cast<const uint32_t*>(s->pixels + (y * s->pitch));
  return row[x];
}

auto set_pixel32(VideoSurface_t* s, int x, int y, uint32_t val) -> void {
  if (x < 0 || x >= s->w || y < 0 || y >= s->h) {
    return;
  }
  auto* row = reinterpret_cast<uint32_t*>(s->pixels + (y * s->pitch));
  row[x] = val;
}

struct ScopedVideoFixture_t {
  ScopedVideoFixture_t() {
    linapple_init();
    video_initialize();
  }

  ~ScopedVideoFixture_t() { linapple_shutdown(); }

  ScopedVideoFixture_t(const ScopedVideoFixture_t&) = delete;
  auto operator=(const ScopedVideoFixture_t&) -> ScopedVideoFixture_t& = delete;
  ScopedVideoFixture_t(ScopedVideoFixture_t&&) = delete;
  auto operator=(ScopedVideoFixture_t&&) -> ScopedVideoFixture_t& = delete;
};

}  // namespace

TEST_CASE("VideoStretch - 1:1 RGB32 Soft Stretch") {
  UniqueSurface_t src = make_surface(560, 384, 4);
  UniqueSurface_t dst = make_surface(560, 384, 4);
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
  UniqueSurface_t src = make_surface(560, 384, 4);
  UniqueSurface_t dst_2x = make_surface(1120, 768, 4);
  UniqueSurface_t dst_3x = make_surface(1680, 1152, 4);
  REQUIRE(src != nullptr);
  REQUIRE(dst_2x != nullptr);
  REQUIRE(dst_3x != nullptr);

  set_pixel32(src.get(), 0, 0, 0x00FF0000);
  set_pixel32(src.get(), 559, 0, 0x0000FF00);
  set_pixel32(src.get(), 0, 383, 0x000000FF);
  set_pixel32(src.get(), 559, 383, 0x00FFFFFF);
  set_pixel32(src.get(), 280, 192, 0x00FFFF00);

  // 1:2 scaling test
  VideoRect_t src_rect = {0, 0, 560, 384};
  VideoRect_t dst_rect_2x = {0, 0, 1120, 768};
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
  VideoRect_t dst_rect_3x = {0, 0, 1680, 1152};
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
  UniqueSurface_t src = make_surface(560, 384, 4);
  UniqueSurface_t dst = make_surface(1120, 768, 4);
  REQUIRE(src != nullptr);
  REQUIRE(dst != nullptr);

  set_pixel32(src.get(), 0, 0, 0x00123456);

  // Target a centered letterbox viewport in the destination: (100, 50, 800,
  // 600)
  VideoRect_t src_rect = {0, 0, 560, 384};
  VideoRect_t dst_rect = {100, 50, 800, 600};
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
  UniqueSurface_t src = make_surface(100, 100, 4);
  UniqueSurface_t dst = make_surface(100, 100, 4);
  REQUIRE(src != nullptr);
  REQUIRE(dst != nullptr);

  // Null surface handling safely returns error
  CHECK(video_soft_stretch(nullptr, nullptr, dst.get(), nullptr) == -1);
  CHECK(video_soft_stretch(src.get(), nullptr, nullptr, nullptr) == -1);

  // Zero-sized rectangles safely return error
  VideoRect_t zero_rect = {0, 0, 0, 0};
  CHECK(video_soft_stretch(src.get(), &zero_rect, dst.get(), nullptr) == -1);

  // Partial out of bounds rectangles are clipped without crashing
  VideoRect_t out_of_bounds_src = {-10, -10, 200, 200};
  CHECK(video_soft_stretch(src.get(), &out_of_bounds_src, dst.get(), nullptr) ==
        0);

  VideoRect_t out_of_bounds_dst = {50, 50, 200, 200};
  CHECK(video_soft_stretch(src.get(), nullptr, dst.get(), &out_of_bounds_dst) ==
        0);
}

TEST_CASE("Video - Mode Switch Preserves Drawn Screen") {
  ScopedVideoFixture_t fixture;

  // Write character 'A' (0xC1 in Apple II text memory) at (0,0) -> 0x400
  *mem_get_main_ptr(0x0400) = 0xC1;

  // Refresh screen to render 'A' into output buffer
  video_refresh_screen();

  const uint32_t* buf = video_get_output_buffer();
  REQUIRE(buf != nullptr);

  unsigned long crc_before = crc32(0L, nullptr, 0);
  crc_before = crc32(crc_before, reinterpret_cast<const unsigned char*>(buf),
                     static_cast<unsigned int>(SCREEN_WIDTH * SCREEN_HEIGHT *
                                               sizeof(uint32_t)));

  constexpr uint32_t expected_crc = 0x16D61EDC;
  CHECK(static_cast<uint32_t>(crc_before) == expected_crc);

  // Switch video mode (F9 behavior)
  g_videotype = VT_COLOR_TEXT_OPTIMIZED;
  video_reinitialize();
  video_refresh_screen();

  unsigned long crc_after = crc32(0L, nullptr, 0);
  crc_after = crc32(crc_after, reinterpret_cast<const unsigned char*>(buf),
                    static_cast<unsigned int>(SCREEN_WIDTH * SCREEN_HEIGHT *
                                              sizeof(uint32_t)));

  CHECK(static_cast<uint32_t>(crc_after) == expected_crc);
}
