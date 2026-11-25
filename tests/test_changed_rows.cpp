// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "doctest.h"
#include "frontends/common/ChangedRows.h"
#include "frontends/common/VideoStretch.h"
#include "frontends/common/VideoSurface.h"

namespace {

// The emulator's output, as apple2/Video.h sizes it, and the windows it is
// scaled into at 1.75x, 1.875x, 2x and 3x.
constexpr int out_w = 560;
constexpr int out_h = 384;
constexpr int w_175 = 980;
constexpr int h_175 = 672;
constexpr int w_1875 = 1050;
constexpr int h_1875 = 720;
constexpr int w_2x = 1120;
constexpr int h_2x = 768;
constexpr int h_3x = 1152;

// Spreads the pixel values so every one is distinct (Knuth's multiplicative
// hash), and flips a pixel's colour bits to change it.
constexpr uint32_t spread = 2654435761U;
constexpr uint32_t colour_bits = 0x00FFFFFFU;

auto whole(int w, int h) -> VideoRect { return VideoRect{0, 0, w, h}; }

auto same_rect(const VideoRect& a, const VideoRect& b) -> bool {
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

// A frame with every pixel distinct, so a changed row is never a copy of
// another by chance.
auto make_frame() -> std::vector<uint32_t> {
  std::vector<uint32_t> f(static_cast<size_t>(out_w) * out_h);
  for (size_t i = 0; i < f.size(); ++i) {
    f[i] = static_cast<uint32_t>(i) * spread;
  }
  return f;
}

auto change_row(std::vector<uint32_t>* f, int y) -> void {
  (*f)[(static_cast<size_t>(y) * out_w)] ^= colour_bits;
}

// Narrows a frame against the last one as the frontends do, starting from
// the whole frame scaled into dst_w x dst_h at (dst_x, dst_y).
struct Narrowed {
  bool draw;
  VideoRect src;
  VideoRect dst;
};

auto narrow(ChangedRows* rows, const std::vector<uint32_t>& f, int dst_x,
            int dst_y, int dst_w, int dst_h) -> Narrowed {
  Narrowed n{false, whole(out_w, out_h), VideoRect{dst_x, dst_y, dst_w, dst_h}};
  n.draw = changed_rows_narrow(rows, f.data(), out_w, out_h, &n.src, &n.dst);
  return n;
}

}  // namespace

TEST_CASE("Changed rows: a first frame, and one after forget, is drawn whole") {
  ChangedRows rows;
  const auto f = make_frame();
  const Narrowed first = narrow(&rows, f, 0, 0, w_175, h_175);
  CHECK(first.draw);
  CHECK(same_rect(first.src, whole(out_w, out_h)));
  CHECK(same_rect(first.dst, whole(w_175, h_175)));

  CHECK_FALSE(narrow(&rows, f, 0, 0, w_175, h_175).draw);

  changed_rows_forget(&rows);
  CHECK(rows.last_output.empty());
  const Narrowed again = narrow(&rows, f, 0, 0, w_175, h_175);
  CHECK(again.draw);
  CHECK(same_rect(again.src, whole(out_w, out_h)));
}

TEST_CASE("Changed rows: an unchanged frame needs no drawing") {
  constexpr int unchanged_frames = 10;
  ChangedRows rows;
  const auto f = make_frame();
  narrow(&rows, f, 0, 0, w_2x, h_2x);
  for (int i = 0; i < unchanged_frames; ++i) {
    CHECK_FALSE(narrow(&rows, f, 0, 0, w_2x, h_2x).draw);
  }
}

TEST_CASE("Changed rows: the whole frame is drawn once a second regardless") {
  ChangedRows rows;
  const auto f = make_frame();
  narrow(&rows, f, 0, 0, w_175, h_175);
  for (int i = 1; i < changed_rows_full_update_interval; ++i) {
    CHECK_FALSE(narrow(&rows, f, 0, 0, w_175, h_175).draw);
  }
  const Narrowed full = narrow(&rows, f, 0, 0, w_175, h_175);
  CHECK(full.draw);
  CHECK(same_rect(full.src, whole(out_w, out_h)));
  CHECK(same_rect(full.dst, whole(w_175, h_175)));
  CHECK_FALSE(narrow(&rows, f, 0, 0, w_175, h_175).draw);
}

TEST_CASE("Changed rows: the band is widened to blocks that scale exactly") {
  struct Case {
    int dst_h;
    int block;
    int dst_block;
  };
  // 384 rows into 384, 672 (1.75x), 720 (1.875x) and 768 (2x) lines.
  const Case cases[] = {
      {out_h, 1, 1}, {h_175, 4, 7}, {h_1875, 8, 15}, {h_2x, 1, 2}};
  // A run of changed rows that starts and ends inside blocks.
  constexpr int first_changed = 101;
  constexpr int last_changed = 130;
  for (const Case& c : cases) {
    CAPTURE(c.dst_h);
    ChangedRows rows;
    auto f = make_frame();
    narrow(&rows, f, 0, 0, out_w, c.dst_h);
    change_row(&f, first_changed);
    change_row(&f, last_changed);
    const Narrowed n = narrow(&rows, f, 0, 0, out_w, c.dst_h);
    REQUIRE(n.draw);
    const int top = first_changed - (first_changed % c.block);
    const int end = last_changed + 1;
    const int bottom = end + ((c.block - (end % c.block)) % c.block);
    CHECK(n.src.x == 0);
    CHECK(n.src.w == out_w);
    CHECK(n.src.y == top);
    CHECK(n.src.h == bottom - top);
    CHECK(n.dst.y == (top / c.block) * c.dst_block);
    CHECK(n.dst.h == ((bottom - top) / c.block) * c.dst_block);
  }
}

TEST_CASE("Changed rows: the band keeps the destination's offset") {
  // At 1.75x, 4 source rows become 7, so row 8 is in the third block.
  constexpr int offset_x = 150;
  constexpr int offset_y = 24;
  constexpr int changed = 8;
  constexpr int block_rows = 7;
  ChangedRows rows;
  auto f = make_frame();
  narrow(&rows, f, offset_x, offset_y, w_175, h_175);
  change_row(&f, changed);
  const Narrowed n = narrow(&rows, f, offset_x, offset_y, w_175, h_175);
  REQUIRE(n.draw);
  CHECK(n.dst.x == offset_x);
  CHECK(n.dst.w == w_175);
  CHECK(n.dst.y == offset_y + (2 * block_rows));
  CHECK(n.dst.h == block_rows);
}

TEST_CASE("Changed rows: changes in the first and last rows reach the edges") {
  ChangedRows rows;
  auto f = make_frame();
  narrow(&rows, f, 0, 0, w_1875, h_1875);
  change_row(&f, 0);
  change_row(&f, out_h - 1);
  const Narrowed n = narrow(&rows, f, 0, 0, w_1875, h_1875);
  REQUIRE(n.draw);
  CHECK(same_rect(n.src, whole(out_w, out_h)));
  CHECK(same_rect(n.dst, whole(w_1875, h_1875)));
}

TEST_CASE(
    "Changed rows: anything but the whole frame is drawn whole and forgotten") {
  ChangedRows rows;
  const auto f = make_frame();
  narrow(&rows, f, 0, 0, w_175, h_175);
  VideoRect src{0, 0, out_w / 2, out_h};
  VideoRect dst{0, 0, w_175 / 2, h_175};
  CHECK(changed_rows_narrow(&rows, f.data(), out_w, out_h, &src, &dst));
  CHECK(same_rect(src, VideoRect{0, 0, out_w / 2, out_h}));
  CHECK(rows.last_output.empty());
  CHECK(narrow(&rows, f, 0, 0, w_175, h_175).draw);
}

TEST_CASE("Changed rows: missing arguments mean drawing the whole frame") {
  ChangedRows rows;
  const auto f = make_frame();
  VideoRect src = whole(out_w, out_h);
  VideoRect dst = whole(w_175, h_175);
  CHECK(changed_rows_narrow(nullptr, f.data(), out_w, out_h, &src, &dst));
  CHECK(changed_rows_narrow(&rows, nullptr, out_w, out_h, &src, &dst));
  CHECK(changed_rows_narrow(&rows, f.data(), out_w, out_h, nullptr, &dst));
  CHECK(changed_rows_narrow(&rows, f.data(), out_w, out_h, &src, nullptr));
  CHECK(changed_rows_narrow(&rows, f.data(), 0, out_h, &src, &dst));
  CHECK(same_rect(src, whole(out_w, out_h)));
  CHECK(same_rect(dst, whole(w_175, h_175)));
  changed_rows_forget(nullptr);
}

TEST_CASE("Changed rows: clipping a band to a surface") {
  constexpr int band_h = 7;
  constexpr int overhang = 12;
  const VideoRect inside{0, 2 * band_h, w_175, band_h};
  CHECK(same_rect(changed_rows_clip(inside, w_175, h_175), inside));
  // Past the left edge and the bottom: only the part on the surface is left.
  const VideoRect over{-overhang, h_175 - overhang, w_175 + (2 * overhang),
                       2 * overhang};
  CHECK(same_rect(changed_rows_clip(over, w_175, h_175),
                  VideoRect{0, h_175 - overhang, w_175, overhang}));
  const VideoRect below{0, h_175 + band_h, w_175, band_h};
  CHECK(changed_rows_clip(below, w_175, h_175).h == 0);
  const VideoRect right{w_175 + overhang, 0, overhang, band_h};
  CHECK(changed_rows_clip(right, w_175, h_175).w == 0);
}

// What the frontends rely on: scaling only the band onto the last scaled
// frame gives the same pixels as scaling the whole new frame.
TEST_CASE("Changed rows: drawing only the band matches a full redraw") {
  const int heights[] = {out_h, h_175, h_1875, h_2x, h_3x};
  constexpr unsigned rng_seed = 20261125;
  constexpr int frames_per_scale = 40;
  constexpr int longest_run = 24;
  std::mt19937 rng(rng_seed);
  for (const int dst_h : heights) {
    const int dst_w = (out_w * dst_h) / out_h;
    CAPTURE(dst_h);
    const int pitch = dst_w * 4;
    std::vector<uint32_t> banded(static_cast<size_t>(dst_w) * dst_h);
    std::vector<uint32_t> full(banded.size());
    const VideoSurfaceView banded_view(banded.data(), dst_w, dst_h, pitch, 4);
    const VideoSurfaceView full_view(full.data(), dst_w, dst_h, pitch, 4);
    ChangedRows rows;
    auto f = make_frame();
    for (int frame = 0; frame < frames_per_scale; ++frame) {
      if (frame > 0) {
        // A run of changed rows somewhere, sometimes none.
        const int first = static_cast<int>(rng() % out_h);
        const int count = static_cast<int>(rng() % longest_run);
        for (int y = first; y < first + count && y < out_h; ++y) {
          change_row(&f, y);
        }
      }
      const VideoSurfaceView output(f.data(), out_w, out_h, out_w * 4, 4);
      VideoRect src = whole(out_w, out_h);
      VideoRect dst = whole(dst_w, dst_h);
      if (changed_rows_narrow(&rows, f.data(), out_w, out_h, &src, &dst)) {
        video_soft_stretch(output, &src, banded_view, &dst);
      }
      const VideoRect all_src = whole(out_w, out_h);
      const VideoRect all_dst = whole(dst_w, dst_h);
      video_soft_stretch(output, &all_src, full_view, &all_dst);
      CAPTURE(frame);
      REQUIRE(banded == full);
    }
  }
}
