// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/sdl2/SDL_Video.h"

#include <SDL2/SDL_pixels.h>
#include <SDL2/SDL_surface.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "apple2/Video.h"
#include "frontends/common/VideoSurface.h"

extern std::recursive_mutex g_video_draw_mutex;

auto stretch_blt_mem_to_frame_dc() -> void {
  const std::lock_guard<std::recursive_mutex> lock(g_video_draw_mutex);
  video_set_frame_ready(true);
}

[[nodiscard]] auto sdl_surface_to_video_surface(SDL_Surface* surface)
    -> VideoSurface_t {
  constexpr int k_default_bpp = 4;

  VideoSurface_t video_surface{};
  if (surface == nullptr) {
    return video_surface;
  }

  video_surface.pixels = static_cast<uint8_t*>(surface->pixels);
  video_surface.w = surface->w;
  video_surface.h = surface->h;
  video_surface.pitch = surface->pitch;

  if (surface->format == nullptr) {
    video_surface.bpp = k_default_bpp;
    return video_surface;
  }

  video_surface.bpp = surface->format->BytesPerPixel;
  if (surface->format->palette != nullptr &&
      surface->format->palette->colors != nullptr) {
    const int ncolors =
        std::max(0, std::min(surface->format->palette->ncolors,
                             static_cast<int>(k_video_palette_size)));
    for (int i = 0; i < ncolors; ++i) {
      const auto idx = static_cast<size_t>(i);
      video_surface.palette.at(idx).r = surface->format->palette->colors[i].r;
      video_surface.palette.at(idx).g = surface->format->palette->colors[i].g;
      video_surface.palette.at(idx).b = surface->format->palette->colors[i].b;
      video_surface.palette.at(idx).a = surface->format->palette->colors[i].a;
    }
  }

  return video_surface;
}
