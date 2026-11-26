// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

// Thin glue between the SDL3-ImGui frame/input loop and the backend-agnostic
// Dear ImGui layer in src/frontends/common/imgui/.
auto sdl3_gui_is_active() noexcept -> bool;
auto sdl3_gui_init(SDL_Window* win, SDL_Renderer* ren) -> bool;
auto sdl3_gui_shutdown() -> void;
auto sdl3_gui_process_event(const SDL_Event* event) -> bool;
auto sdl3_gui_get_viewport_rect(int win_w, int win_h) noexcept -> SDL_FRect;
auto sdl3_gui_scale_viewport_rect(const SDL_FRect& logical_rect, int win_w,
                                  int win_h, int out_w, int out_h) noexcept
    -> SDL_FRect;
auto sdl3_gui_render(SDL_Renderer* ren) -> void;
auto sdl3_gui_sync_cursor(bool mouse_captured, bool is_fullscreen) -> void;
