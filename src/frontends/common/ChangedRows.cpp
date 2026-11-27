// SPDX-License-Identifier: GPL-2.0-only

#include "frontends/common/ChangedRows.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "frontends/common/VideoSurface.h"

namespace {

auto greatest_common_divisor(int a, int b) -> int {
  while (b != 0) {
    const int t = a % b;
    a = b;
    b = t;
  }
  return a;
}

}  // namespace

auto changed_rows_narrow(ChangedRows* rows, const uint32_t* output, int width,
                         int height, VideoRect* src, VideoRect* dst) -> bool {
  if (rows == nullptr || output == nullptr || src == nullptr ||
      dst == nullptr || width <= 0 || height <= 0) {
    return true;
  }
  if (src->x != 0 || src->y != 0 || src->w != width || src->h != height ||
      dst->h <= 0) {
    changed_rows_forget(rows);
    return true;
  }
  const size_t pixels = static_cast<size_t>(width) * height;
  if (rows->last_output.size() != pixels ||
      ++rows->frames_since_full_update >= changed_rows_full_update_interval) {
    rows->last_output.assign(output, output + pixels);
    rows->frames_since_full_update = 0;
    return true;
  }
  const size_t row_pixels = static_cast<size_t>(width);
  const size_t row_bytes = row_pixels * sizeof(uint32_t);
  const uint32_t* last = rows->last_output.data();
  auto row_same = [&](int y) -> bool {
    const size_t off = static_cast<size_t>(y) * row_pixels;
    return std::memcmp(output + off, last + off, row_bytes) == 0;
  };
  int top = 0;
  while (top < height && row_same(top)) {
    ++top;
  }
  if (top == height) {
    return false;
  }
  int bottom = height;
  while (bottom > top && row_same(bottom - 1)) {
    --bottom;
  }
  const size_t off = static_cast<size_t>(top) * row_pixels;
  std::memcpy(rows->last_output.data() + off, output + off,
              static_cast<size_t>(bottom - top) * row_bytes);

  // Every block source rows become exactly dst_block destination rows.
  const int divisor = greatest_common_divisor(dst->h, height);
  const int block = height / divisor;
  const int dst_block = dst->h / divisor;
  top -= top % block;
  if (bottom % block != 0) {
    bottom += block - (bottom % block);
  }
  src->y = top;
  src->h = bottom - top;
  dst->y += (top / block) * dst_block;
  dst->h = ((bottom - top) / block) * dst_block;
  return true;
}

auto changed_rows_forget(ChangedRows* rows) -> void {
  if (rows == nullptr) {
    return;
  }
  rows->last_output.clear();
  rows->frames_since_full_update = 0;
}

auto changed_rows_clip(const VideoRect& band, int width, int height)
    -> VideoRect {
  const int left = std::max(band.x, 0);
  const int top = std::max(band.y, 0);
  const int right = std::min(band.x + band.w, width);
  const int bottom = std::min(band.y + band.h, height);
  if (right <= left || bottom <= top) {
    return VideoRect{0, 0, 0, 0};
  }
  return VideoRect{left, top, right - left, bottom - top};
}
