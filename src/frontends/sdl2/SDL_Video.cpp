#include "frontends/sdl2/SDL_Video.h"

#include <SDL_surface.h>
#include <SDL_pixels.h>

#include <cstdint>
#include <mutex>

#include "VideoSurface.h"
#include "apple2/Video.h"

extern VideoSurface_t* g_debug_screen;
extern std::recursive_mutex g_video_draw_mutex;
extern SDL_Surface* g_screen;

void StretchBltMemToFrameDC() {
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

  constexpr int bpp_rgba32 = 4;
  constexpr int bpp_index8 = 1;

  vs.bpp = bpp_rgba32;  // Assuming RGB32
  if (s->format->format == SDL_PIXELFORMAT_INDEX8) {
    vs.bpp = bpp_index8;
  }
  // Note: palette is not copied here, but VideoSurface_t has it
  return vs;
}
