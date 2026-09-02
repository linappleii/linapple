#include "frontends/sdl3/SDL_Video.h"

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

#include <cstdint>
#include <mutex>

#include "apple2/Video.h"
#include "frontends/common/VideoSurface.h"

extern VideoSurface_t* g_debug_screen;
extern std::recursive_mutex g_video_draw_mutex;
extern SDL_Surface* g_screen;

void stretch_blt_mem_to_frame_dc() {
  g_video_draw_mutex.lock();
  // In our new architecture, we just set frame ready and let the main loop draw
  // it.
  g_frame_ready = true;
  g_video_draw_mutex.unlock();
}

auto sdl_surface_to_video_surface(SDL_Surface* s) -> VideoSurface_t {
  VideoSurface_t vs{};
  if (s == nullptr) {
    return vs;
  }
  vs.pixels = static_cast<uint8_t*>(s->pixels);
  vs.w = s->w;
  vs.h = s->h;
  vs.pitch = s->pitch;
  vs.bpp = SDL_BYTESPERPIXEL(s->format);
  SDL_Palette* pal = SDL_GetSurfacePalette(s);
  if (pal != nullptr) {
    int ncolors = (pal->ncolors < 256) ? pal->ncolors : 256;
    for (int i = 0; i < ncolors; ++i) {
      vs.palette[i].r = pal->colors[i].r;
      vs.palette[i].g = pal->colors[i].g;
      vs.palette[i].b = pal->colors[i].b;
      vs.palette[i].a = pal->colors[i].a;
    }
  }
  return vs;
}
