// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL/SDL.h>

#include "frontends/common/VideoSurface.h"

// Converts an SDL 1.2 surface to a VideoSurface_t view.
// Note: VideoSurface_t::pixels is a non-owning pointer directly referencing
// surface->pixels. The caller must ensure surface remains valid while in use.
[[nodiscard]] auto sdl_surface_to_video_surface(SDL_Surface* surface)
    -> VideoSurface_t;
