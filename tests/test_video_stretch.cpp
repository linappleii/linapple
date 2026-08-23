// SPDX-License-Identifier: GPL-2.0-only

#include "doctest.h"
#include "frontends/common/VideoStretch.h"
#include "frontends/common/VideoSurface.h"

TEST_CASE("VideoStretch Bounds Safety and Clipping") {
  // Create a 1bpp font surface and a 4bpp screen surface
  VideoSurface_t* font = video_create_surface(270, 64, 1);
  REQUIRE(font != nullptr);
  font_sfc = font;

  // Screen surface at 560x384
  VideoSurface_t* dst = video_create_surface(560, 384, 4);
  REQUIRE(dst != nullptr);

  SUBCASE("font_print with vertical out-of-bounds (y >= surface height)") {
    // Calling font_print at y=632 with scale 2x on a 384-height surface
    // (exact scenario of issue #337)
    font_print(32, 632, "Numpad +/-/* - Increase/Decrease/Normal speed", dst,
               2.0, 2.0);
    // Also test partially off the bottom edge
    font_print(32, 380, "Partially off bottom", dst, 2.0, 2.0);
  }

  SUBCASE("font_print with horizontal out-of-bounds") {
    // Text starting near right edge and extending beyond dst->w
    font_print(550, 100, "This is a long string that extends far past width",
               dst, 2.0, 2.0);
  }

  SUBCASE("font_print with negative coordinates") {
    font_print(-20, -10, "Negative origin text", dst, 2.0, 2.0);
    font_print_centered(
        10, 50, "Very long centered string with negative left coordinate", dst,
        2.0, 2.0);
    font_print_right(20, 50,
                     "Very long right-aligned string extending to negative x",
                     dst, 2.0, 2.0);
  }

  SUBCASE("video_soft_stretch_or with out-of-bounds destination rect") {
    VideoRect_t srect = {0, 0, 16, 16};
    VideoRect_t drect_bottom = {0, 375, 32, 32};  // Extends past h=384
    int res1 = video_soft_stretch_or(font, &srect, dst, &drect_bottom);
    (void)res1;

    VideoRect_t drect_right = {550, 100, 32, 32};  // Extends past w=560
    int res2 = video_soft_stretch_or(font, &srect, dst, &drect_right);
    (void)res2;

    VideoRect_t drect_neg = {-10, -10, 32, 32};  // Negative origin
    int res3 = video_soft_stretch_or(font, &srect, dst, &drect_neg);
    (void)res3;
  }

  SUBCASE("rectangle bounds safety") {
    // Rectangle extending beyond surface dimensions
    rectangle(dst, -10, -10, 600, 500, 0xFFFFFFFF);
  }

  video_destroy_surface(dst);
  video_destroy_surface(font);
  font_sfc = nullptr;
}
