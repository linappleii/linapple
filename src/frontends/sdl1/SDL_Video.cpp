#include "frontends/sdl1/SDL_Video.h"

#include <SDL/SDL.h>

#include <cstdio>
#include <mutex>

#include "apple2/Video.h"
#include "frontends/sdl1/Frame.h"

extern VideoSurface* g_hDebugScreen;
extern std::recursive_mutex g_video_draw_mutex;
extern SDL_Surface* screen;

void StretchBltMemToFrameDC() {
  const std::lock_guard<std::recursive_mutex> lock(g_video_draw_mutex);
  // In our new architecture, we just set frame ready and let the main loop draw
  // it.
  g_bFrameReady = true;
}

auto SDLSurfaceToVideoSurface(SDL_Surface* s) -> VideoSurface {
  VideoSurface vs{};
  vs.pixels = static_cast<uint8_t*>(s->pixels);
  vs.w = s->w;
  vs.h = s->h;
  vs.pitch = s->pitch;

  constexpr int BPP_RGBA32 = 4;
  constexpr int BPP_INDEX8 = 1;

  vs.bpp = BPP_RGBA32;  // Assuming RGB32
  if (s->format->BitsPerPixel == 8) {
    vs.bpp = BPP_INDEX8;
  }
  // Note: palette is not copied here, but VideoSurface has it
  return vs;
}
