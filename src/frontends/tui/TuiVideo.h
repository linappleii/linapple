// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

#include "core/Registry.h"

auto tui_video_initialize() -> void;
auto tui_video_shutdown() -> void;

auto tui_video_set_render_mode(TuiRenderMode_t mode) -> void;
auto tui_video_toggle_render_mode() -> void;
auto tui_video_get_render_mode() -> TuiRenderMode_t;

auto tui_video_render_frame(const uint32_t* pixels, int width, int height,
                            int pitch) -> void;

auto tui_video_on_resize() -> void;

auto tui_video_toggle_help() -> void;
auto tui_video_is_help_visible() -> bool;
auto tui_video_close_help() -> void;

auto tui_video_toggle_fullscreen() -> void;
auto tui_video_is_fullscreen() -> bool;

auto tui_video_save_screenshot() -> void;
