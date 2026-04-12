#include "apple2/stretch.h"
#include "frontends/sdl3/SDL_Video.h"
#include "apple2/Video.h"
#include <SDL3/SDL.h>

#include <mutex>
#include "frontends/sdl3/Frame.h"

extern VideoSurface *g_hDebugScreen;
extern std::recursive_mutex g_video_draw_mutex;
extern SDL_Surface* screen;

void StretchBltMemToFrameDC(void)
{
	VideoRect drect, srect;

	g_video_draw_mutex.lock();

  if (!g_hDebugScreen) {
    g_video_draw_mutex.unlock();
    return;
  }

	drect.x = drect.y = srect.x = srect.y = 0;
	drect.w = 560; // Assuming screen resolution
	drect.h = 384;
	srect.w = g_hDebugScreen->w;
	srect.h = g_hDebugScreen->h;

	VideoSurface vs_screen = SDLSurfaceToVideoSurface(screen);
	VideoSoftStretch(g_hDebugScreen, &srect, g_origscreen, &drect);
	VideoSoftStretch(g_origscreen, NULL, &vs_screen, NULL);

	g_video_draw_mutex.unlock();
}

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
