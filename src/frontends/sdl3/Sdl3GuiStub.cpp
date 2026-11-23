// SPDX-License-Identifier: GPL-2.0-only
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

#include "frontends/sdl3/Sdl3GuiBridge.h"

auto sdl3_gui_is_compiled() noexcept -> bool { return false; }

auto sdl3_gui_is_active() noexcept -> bool { return false; }

auto sdl3_gui_init(SDL_Window* win, SDL_Renderer* ren) -> bool {
  (void)win;
  (void)ren;
  return false;
}

auto sdl3_gui_shutdown() -> void {}

auto sdl3_gui_process_event(const SDL_Event* event) -> bool {
  (void)event;
  return false;
}

auto sdl3_gui_get_viewport_rect(int win_w, int win_h) noexcept -> SDL_FRect {
  return SDL_FRect{0.0F, 0.0F, static_cast<float>(win_w),
                   static_cast<float>(win_h)};
}

auto sdl3_gui_render(SDL_Renderer* ren) -> void { (void)ren; }

auto sdl3_gui_sync_cursor(bool mouse_captured, bool is_fullscreen) -> void {
  (void)mouse_captured;
  (void)is_fullscreen;
}
