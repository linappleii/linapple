#include "frontends/sdl3/SDL_Video.h"
#include "apple2/Video.h"
#include <SDL3/SDL.h>

VideoSurface SDLSurfaceToVideoSurface(SDL_Surface* s) {
    VideoSurface vs;
    vs.pixels = (uint8_t*)s->pixels;
    vs.w = s->w;
    vs.h = s->h;
    vs.pitch = s->pitch;
    vs.bpp = 4; // Assuming RGB32
    if (s->format == SDL_PIXELFORMAT_INDEX8) {
        vs.bpp = 1;
    }
    // Note: palette is not copied here, but VideoSurface has it
    return vs;
}
