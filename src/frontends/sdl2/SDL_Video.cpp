#include "frontends/sdl2/SDL_Video.h"

#include <SDL_pixels.h>
#include <SDL_surface.h>

#include <cstdint>
#include <mutex>

#include "apple2/Video.h"
#include "frontends/common/VideoSurface.h"

extern VideoSurface_t* g_debug_screen;
extern std::recursive_mutex g_video_draw_mutex;
extern SDL_Surface* g_screen;

void stretch_blt_mem_to_frame_dc() {
  const std::lock_guard<std::recursive_mutex> lock(g_video_draw_mutex);
  // In our new architecture, we just set frame ready and let the main loop draw
  // it.
  g_frame_ready = true;
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
  vs.bpp = (s->format != nullptr) ? s->format->BytesPerPixel : 4;
  if (s->format != nullptr && s->format->palette != nullptr) {
    int ncolors =
        (s->format->palette->ncolors < 256) ? s->format->palette->ncolors : 256;
    for (int i = 0; i < ncolors; ++i) {
      vs.palette[i].r = s->format->palette->colors[i].r;
      vs.palette[i].g = s->format->palette->colors[i].g;
      vs.palette[i].b = s->format->palette->colors[i].b;
      vs.palette[i].a = s->format->palette->colors[i].a;
    }
  }
  return vs;
}
