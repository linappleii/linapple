// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_surface.h>

#include "VideoSurface.h"

auto sdl_surface_to_video_surface(SDL_Surface* s) -> VideoSurface_t;
