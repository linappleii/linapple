#ifndef SDL_VIDEO_FRONTEND_H
#define SDL_VIDEO_FRONTEND_H

#include <SDL3/SDL.h>
#include "apple2/Video.h"

VideoSurface SDLSurfaceToVideoSurface(SDL_Surface* s);

#endif
