// SPDX-License-Identifier: GPL-2.0-only

#include <cstdint>

#include "doctest.h"
#include "frontends/common/VideoStretch.h"
#include "frontends/common/VideoSurface.h"

static auto get_pixel32(const VideoSurface_t* s, int x, int y) -> uint32_t {
  if (x < 0 || x >= s->w || y < 0 || y >= s->h) return 0;
  const auto* row =
      reinterpret_cast<const uint32_t*>(s->pixels + (y * s->pitch));
  return row[x];
}

static void set_pixel32(VideoSurface_t* s, int x, int y, uint32_t val) {
  if (x < 0 || x >= s->w || y < 0 || y >= s->h) return;
  auto* row = reinterpret_cast<uint32_t*>(s->pixels + (y * s->pitch));
  row[x] = val;
}

TEST_CASE("VideoStretch - 1:1 RGB32 Soft Stretch") {
  VideoSurface_t* src = video_create_surface(560, 384, 4);
  VideoSurface_t* dst = video_create_surface(560, 384, 4);
  REQUIRE(src != nullptr);
  REQUIRE(dst != nullptr);

  set_pixel32(src, 0, 0, 0x00112233);
  set_pixel32(src, 559, 0, 0x00445566);
  set_pixel32(src, 0, 383, 0x00778899);
  set_pixel32(src, 559, 383, 0x00AABBCC);
  set_pixel32(src, 280, 192, 0x00DDEEFF);

  int ret = video_soft_stretch(src, nullptr, dst, nullptr);
  CHECK(ret == 0);

  CHECK(get_pixel32(dst, 0, 0) == 0x00112233);
  CHECK(get_pixel32(dst, 559, 0) == 0x00445566);
  CHECK(get_pixel32(dst, 0, 383) == 0x00778899);
  CHECK(get_pixel32(dst, 559, 383) == 0x00AABBCC);
  CHECK(get_pixel32(dst, 280, 192) == 0x00DDEEFF);

  video_destroy_surface(src);
  video_destroy_surface(dst);
}

TEST_CASE("VideoStretch - 1:2 and 1:3 Scaling to Window Resolutions") {
  VideoSurface_t* src = video_create_surface(560, 384, 4);
  VideoSurface_t* dst_2x = video_create_surface(1120, 768, 4);
  VideoSurface_t* dst_3x = video_create_surface(1680, 1152, 4);
  REQUIRE(src != nullptr);
  REQUIRE(dst_2x != nullptr);
  REQUIRE(dst_3x != nullptr);

  set_pixel32(src, 0, 0, 0x00FF0000);
  set_pixel32(src, 559, 0, 0x0000FF00);
  set_pixel32(src, 0, 383, 0x000000FF);
  set_pixel32(src, 559, 383, 0x00FFFFFF);
  set_pixel32(src, 280, 192, 0x00FFFF00);

  // 1:2 scaling test
  VideoRect_t src_rect = {0, 0, 560, 384};
  VideoRect_t dst_rect_2x = {0, 0, 1120, 768};
  int ret2 = video_soft_stretch(src, &src_rect, dst_2x, &dst_rect_2x);
  CHECK(ret2 == 0);

  // Check 2x mapped pixels at extremities
  CHECK(get_pixel32(dst_2x, 0, 0) == 0x00FF0000);
  CHECK(get_pixel32(dst_2x, 1, 0) == 0x00FF0000);
  CHECK(get_pixel32(dst_2x, 0, 1) == 0x00FF0000);
  CHECK(get_pixel32(dst_2x, 1, 1) == 0x00FF0000);

  CHECK(get_pixel32(dst_2x, 1118, 0) == 0x0000FF00);
  CHECK(get_pixel32(dst_2x, 1119, 0) == 0x0000FF00);
  CHECK(get_pixel32(dst_2x, 0, 766) == 0x000000FF);
  CHECK(get_pixel32(dst_2x, 0, 767) == 0x000000FF);
  CHECK(get_pixel32(dst_2x, 1118, 766) == 0x00FFFFFF);
  CHECK(get_pixel32(dst_2x, 1119, 767) == 0x00FFFFFF);
  CHECK(get_pixel32(dst_2x, 560, 384) == 0x00FFFF00);

  // 1:3 scaling test
  VideoRect_t dst_rect_3x = {0, 0, 1680, 1152};
  int ret3 = video_soft_stretch(src, &src_rect, dst_3x, &dst_rect_3x);
  CHECK(ret3 == 0);

  CHECK(get_pixel32(dst_3x, 0, 0) == 0x00FF0000);
  CHECK(get_pixel32(dst_3x, 1679, 0) == 0x0000FF00);
  CHECK(get_pixel32(dst_3x, 0, 1151) == 0x000000FF);
  CHECK(get_pixel32(dst_3x, 1679, 1151) == 0x00FFFFFF);
  CHECK(get_pixel32(dst_3x, 841, 577) == 0x00FFFF00);

  video_destroy_surface(src);
  video_destroy_surface(dst_2x);
  video_destroy_surface(dst_3x);
}

TEST_CASE("VideoStretch - Sub-Rectangle and Viewport Positioning") {
  VideoSurface_t* src = video_create_surface(560, 384, 4);
  VideoSurface_t* dst = video_create_surface(1120, 768, 4);
  REQUIRE(src != nullptr);
  REQUIRE(dst != nullptr);

  set_pixel32(src, 0, 0, 0x00123456);

  // Target a centered letterbox viewport in the destination: (100, 50, 800, 600)
  VideoRect_t src_rect = {0, 0, 560, 384};
  VideoRect_t dst_rect = {100, 50, 800, 600};
  int ret = video_soft_stretch(src, &src_rect, dst, &dst_rect);
  CHECK(ret == 0);

  // Outside viewport remains 0
  CHECK(get_pixel32(dst, 0, 0) == 0);
  CHECK(get_pixel32(dst, 99, 50) == 0);
  CHECK(get_pixel32(dst, 100, 49) == 0);

  // Inside viewport start
  CHECK(get_pixel32(dst, 100, 50) == 0x00123456);

  video_destroy_surface(src);
  video_destroy_surface(dst);
}

TEST_CASE("VideoStretch - Boundary and Null Safety Checks") {
  VideoSurface_t* src = video_create_surface(100, 100, 4);
  VideoSurface_t* dst = video_create_surface(100, 100, 4);
  REQUIRE(src != nullptr);
  REQUIRE(dst != nullptr);

  // Null surface handling safely returns error
  CHECK(video_soft_stretch(nullptr, nullptr, dst, nullptr) == -1);
  CHECK(video_soft_stretch(src, nullptr, nullptr, nullptr) == -1);

  // Zero-sized rectangles safely return error
  VideoRect_t zero_rect = {0, 0, 0, 0};
  CHECK(video_soft_stretch(src, &zero_rect, dst, nullptr) == -1);

  // Partial out of bounds rectangles are clipped without crashing
  VideoRect_t out_of_bounds_src = {-10, -10, 200, 200};
  CHECK(video_soft_stretch(src, &out_of_bounds_src, dst, nullptr) == 0);

  VideoRect_t out_of_bounds_dst = {50, 50, 200, 200};
  CHECK(video_soft_stretch(src, nullptr, dst, &out_of_bounds_dst) == 0);

  video_destroy_surface(src);
  video_destroy_surface(dst);
}
