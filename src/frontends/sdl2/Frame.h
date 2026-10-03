// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_rect.h>

#include <cstdint>

#include "frontends/sdl2/SdlPtr.h"

auto frontend_translate_key(SDL_Keycode key, SDL_Keymod mod) -> uint8_t;
auto frontend_handle_key_event(SDL_Keycode key, bool is_down) -> bool;

constexpr int k_btn_help = 0;
constexpr int k_btn_run = 1;
constexpr int k_btn_drive1 = 2;
constexpr int k_btn_drive2 = 3;
constexpr int k_btn_driveswap = 4;
constexpr int k_btn_fullscr = 5;
constexpr int k_btn_debug = 6;
constexpr int k_btn_setup = 7;
constexpr int k_btn_cycle = 8;
constexpr int k_btn_loadst = 9;
constexpr int k_btn_savest = 10;
constexpr int k_btn_quit = 11;

constexpr int k_screen_bpp = 8;
constexpr int k_show_cycles = 15;

extern SdlSurfacePtr_t g_screen;
extern SdlWindowPtr_t g_window;
extern SdlRendererPtr_t g_renderer;
extern SdlTexturePtr_t g_texture;

extern bool g_window_resized;
extern SDL_Rect g_orig_rect;
extern SDL_Rect g_new_rect;
extern int g_buttondown;
extern bool g_usingcursor;

auto init_sdl() -> int;

auto frame_create_window() -> int;
auto frame_destroy_window() -> void;

auto frame_refresh() -> void;
auto frame_refresh_status(int drawflags) -> void;
auto draw_status_area(int drawflags) -> void;
auto process_button_click(int button, int mod) -> void;
auto frame_quick_state(int state, int mod) -> void;
auto sdl_handle_event(SDL_Event* event) -> void;
auto is_modifier_key(SDL_Keycode key) noexcept -> bool;

auto draw_apple_content() -> void;
auto draw_frame_window() -> void;
auto frame_on_resize(int width, int height) -> void;
auto frame_on_focus(bool gained) -> void;
auto frame_on_expose() -> void;
auto frame_show_help_screen(int width, int height) -> void;

auto set_using_cursor(bool enable) -> void;
auto set_fullscreen_mode() -> void;
auto set_normal_mode() -> void;

auto harddisk_ui_ftp_select(int drive) -> void;
auto harddisk_ui_select(int drive) -> void;
auto disk_ftp_select_image(int drive) -> void;
auto disk_select(int drive) -> void;
