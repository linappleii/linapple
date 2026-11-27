// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <vector>

#include "frontends/common/VideoSurface.h"

// Without GPU help, scaling the frame and sending it to the display is most of
// the cost of a frame, and from one frame to the next most rows of the
// emulator's output usually stay the same. A frontend that keeps the last
// scaled frame on its target can scale and present just the band of rows that
// changed, and the result is pixel for pixel what a full redraw would give.
struct ChangedRows {
  // The output last drawn whole or in part into the target, so the next frame
  // can be compared with it. Empty when the target no longer holds that frame.
  std::vector<uint32_t> last_output;
  int frames_since_full_update = 0;
};

// The whole frame is drawn about once a second at 60 fps regardless, so
// anything else that disturbed the target is put right within a second.
constexpr int changed_rows_full_update_interval = 60;

// Narrows *src and *dst to the band of rows of output (width x height pixels)
// that changed since the last call, and remembers the frame. Returns false if
// no row changed, so there is nothing to draw. The band is widened to whole
// blocks of rows that scale exactly (8 source rows to 15 when 384 become 720,
// 1 to 2 at 2x), so its pixels match a full redraw. Leaves *src and *dst as
// they are, to draw the whole frame, the first time, once a second, and for
// anything other than the whole frame scaled to some height.
auto changed_rows_narrow(ChangedRows* rows, const uint32_t* output, int width,
                         int height, VideoRect* src, VideoRect* dst) -> bool;

// The target no longer holds the last frame (it was resized, cleared or drawn
// over), so the next frame is drawn whole.
auto changed_rows_forget(ChangedRows* rows) -> void;

// The part of band that lies on a surface of width x height, which may be
// empty (w or h 0).
auto changed_rows_clip(const VideoRect& band, int width, int height)
    -> VideoRect;
