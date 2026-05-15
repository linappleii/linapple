#ifndef SDL_VIDEO_FRONTEND_H
#define SDL_VIDEO_FRONTEND_H

#include <SDL2/SDL.h>

#include "apple2/Video.h"

auto SDLSurfaceToVideoSurface(SDL_Surface* s) -> VideoSurface;

#endif
