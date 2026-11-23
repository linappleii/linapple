// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

// Thin bridge between the shared SDL3 frame/input loop and an optional GUI
// layer. Regular FRONTEND=sdl3 links Sdl3GuiStub.cpp (zero external UI
// dependencies); FRONTEND=sdl3-imgui links Sdl3GuiImGui.cpp.
auto sdl3_gui_is_compiled() noexcept -> bool;
auto sdl3_gui_is_active() noexcept -> bool;
auto sdl3_gui_init(SDL_Window* win, SDL_Renderer* ren) -> bool;
auto sdl3_gui_shutdown() -> void;
auto sdl3_gui_process_event(const SDL_Event* event) -> bool;
auto sdl3_gui_get_viewport_rect(int win_w, int win_h) noexcept -> SDL_FRect;
auto sdl3_gui_render(SDL_Renderer* ren) -> void;
auto sdl3_gui_sync_cursor(bool mouse_captured, bool is_fullscreen) -> void;
