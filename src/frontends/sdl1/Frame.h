// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL/SDL_events.h>
#include <SDL/SDL_keysym.h>
#include <SDL/SDL_video.h>

#include <cstdint>

#include "frontends/sdl1/SdlPtr.h"

auto frontend_translate_key(SDLKey key, SDLMod mod) -> uint8_t;
auto frontend_handle_key_event(SDLKey key, bool is_down) -> bool;
auto sdl_handle_event(SDL_Event* event) -> void;

constexpr int btn_help = 0;
constexpr int btn_run = 1;
constexpr int btn_drive1 = 2;
constexpr int btn_drive2 = 3;
constexpr int btn_driveswap = 4;
constexpr int btn_fullscr = 5;
constexpr int btn_debug = 6;
constexpr int btn_setup = 7;
constexpr int btn_cycle = 8;
constexpr int btn_loadst = 9;
constexpr int btn_savest = 10;
constexpr int btn_quit = 11;

constexpr int screen_bpp = 8;
extern SDL_Surface* g_screen;
extern SdlSurfacePtr g_texture;

constexpr int show_cycles = 15;

extern bool g_window_resized;
extern int g_buttondown;

extern SDL_Rect g_orig_rect;
extern SDL_Rect g_new_rect;

auto init_sdl() -> int;

auto frame_create_window() -> int;
auto frame_destroy_window() -> void;
auto frame_refresh() -> void;
// Once a frame: repaints the status lamps when the hard disk reported
// activity since the last poll, or when a lit lamp's hold has run out.
auto frame_poll_activity() -> void;
// The lamp glyph as last composed: 0 drive 1, 1 drive 2, 2 the hard disk.
auto frame_status_led(int index) -> char;

auto draw_apple_content() -> void;
auto draw_frame_window() -> void;
auto draw_status_area(int drawflags) -> void;
auto process_button_click(int button, int mod) -> void;
auto frame_quick_state(int state, int mod) -> void;
auto is_modifier_key(SDLKey key) noexcept -> bool;

auto frame_on_resize(int width, int height) -> void;
auto frame_on_focus(bool gained) -> void;
auto frame_on_expose() -> void;
auto frame_show_help_screen(int sx, int sy) -> void;

auto set_fullscreen_mode() -> void;
auto set_normal_mode() -> void;

auto harddisk_ui_ftp_select(int drive) -> void;
auto harddisk_ui_select(int drive) -> void;
auto disk_ftp_select_image(int drive) -> void;
auto disk_select(int drive) -> void;
