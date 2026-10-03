// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL2/SDL_surface.h>

#include "frontends/common/VideoSurface.h"

auto stretch_blt_mem_to_frame_dc() -> void;

auto sdl_surface_to_video_surface(SDL_Surface* surface) -> VideoSurface_t;
