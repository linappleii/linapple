// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

#include "frontends/common/VideoSurface.h"

constexpr int font_size_x = 6;
constexpr int font_size_y = 8;
constexpr int chars_in_row = 45;

extern VideoSurface* font_sfc;

auto video_soft_stretch(VideoSurfaceView src, const VideoRect* srcrect,
                        VideoSurfaceView dst, const VideoRect* dstrect) -> int;
auto video_soft_stretch_or(VideoSurfaceView src, const VideoRect* srcrect,
                           VideoSurfaceView dst, const VideoRect* dstrect)
    -> int;
auto video_soft_stretch_mono8(VideoSurfaceView src, const VideoRect* srcrect,
                              VideoSurfaceView dst, const VideoRect* dstrect,
                              uint32_t fgbrush, uint32_t bgbrush) -> int;

auto fonts_initialization() -> bool;
auto fonts_termination() -> void;
auto font_print(int x, int y, const char* text, VideoSurfaceView surface,
                double kx, double ky) -> void;
auto font_print_right(int x, int y, const char* text, VideoSurfaceView surface,
                      double kx, double ky) -> void;
auto font_print_centered(int x, int y, const char* text,
                         VideoSurfaceView surface, double kx, double ky)
    -> void;

auto surface_fader(VideoSurface* surface, float r_factor, float g_factor,
                   float b_factor, float a_factor, const VideoRect* r) -> void;
auto putpixel(VideoSurfaceView surface, int x, int y, uint32_t pixel) -> void;
auto rectangle(VideoSurfaceView surface, int x, int y, int w, int h,
               uint32_t pixel) -> void;
auto fill_rectangle(VideoSurfaceView surface, int x, int y, int w, int h,
                    uint32_t pixel) -> void;
