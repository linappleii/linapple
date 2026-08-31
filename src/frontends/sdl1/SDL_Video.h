// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL/SDL.h>

#include "apple2/Video.h"

auto sdl_surface_to_video_surface(SDL_Surface* s) -> VideoSurface_t;
