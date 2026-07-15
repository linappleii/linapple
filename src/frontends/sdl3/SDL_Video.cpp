#include "frontends/sdl3/SDL_Video.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <mutex>

#include "apple2/Video.h"
#include "frontends/sdl3/Frame.h"

extern VideoSurface* g_debug_screen;
extern std::recursive_mutex g_video_draw_mutex;
extern SDL_Surface* screen;

void StretchBltMemToFrameDC() {
  g_video_draw_mutex.lock();
  // In our new architecture, we just set frame ready and let the main loop draw
  // it.
  g_bFrameReady = true;
  g_video_draw_mutex.unlock();
}

auto sdl_surface_to_video_surface(SDL_Surface* s) -> VideoSurface {
  VideoSurface vs{};
  vs.pixels = static_cast<uint8_t*>(s->pixels);
  vs.w = s->w;
  vs.h = s->h;
  vs.pitch = s->pitch;
  vs.bpp = 4;  // Assuming RGB32
  if (s->format == SDL_PIXELFORMAT_INDEX8) {
    vs.bpp = 1;
  }
  // Note: palette is not copied here, but VideoSurface has it
  return vs;
}
