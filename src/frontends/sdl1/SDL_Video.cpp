// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/sdl1/SDL_Video.h"

#include <SDL/SDL_video.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "apple2/Video.h"
#include "frontends/common/VideoSurface.h"

auto stretch_blt_mem_to_frame_dc() -> void {
  const std::lock_guard<std::recursive_mutex> lock(g_video_draw_mutex);
  video_set_frame_ready(true);
}

[[nodiscard]] auto sdl_surface_to_video_surface(SDL_Surface* surface)
    -> VideoSurface_t {
  constexpr int k_default_bpp = 4;
  constexpr uint8_t k_alpha_opaque = 255;

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
      video_surface.palette.at(idx).a = k_alpha_opaque;
    }
  }

  return video_surface;
}
