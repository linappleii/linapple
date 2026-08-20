// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

#include "frontends/common/VideoSurface.h"

constexpr int font_size_x = 6;
constexpr int font_size_y = 8;
constexpr int chars_in_row = 45;

#define FONT_SIZE_X font_size_x
#define FONT_SIZE_Y font_size_y
#define CHARS_IN_ROW chars_in_row

extern VideoSurface_t* font_sfc;

auto video_soft_stretch(VideoSurface_t* src, VideoRect_t* srcrect,
                        VideoSurface_t* dst, VideoRect_t* dstrect) -> int;
auto video_soft_stretch_or(VideoSurface_t* src, VideoRect_t* srcrect,
                           VideoSurface_t* dst, VideoRect_t* dstrect) -> int;
auto video_soft_stretch_mono8(VideoSurface_t* src, VideoRect_t* srcrect,
                              VideoSurface_t* dst, VideoRect_t* dstrect,
                              uint32_t fgbrush, uint32_t bgbrush) -> int;

auto fonts_initialization() -> bool;
auto fonts_termination() -> void;
auto font_print(int x, int y, const char* text, VideoSurface_t* surface,
                double kx, double ky) -> void;
auto font_print_right(int x, int y, const char* text, VideoSurface_t* surface,
                      double kx, double ky) -> void;
auto font_print_centered(int x, int y, const char* text,
                         VideoSurface_t* surface, double kx, double ky) -> void;

auto surface_fader(VideoSurface_t* surface, float r_factor, float g_factor,
                   float b_factor, float a_factor, VideoRect_t* r) -> void;
auto putpixel(VideoSurface_t* surface, int x, int y, uint32_t pixel) -> void;
auto rectangle(VideoSurface_t* surface, int x, int y, int w, int h,
               uint32_t pixel) -> void;
