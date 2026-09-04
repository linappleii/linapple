// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <SDL2/SDL.h>

#include <cstdint>

#include "frontends/sdl2/SdlPtr.h"

// Frontend-specific keyboard helpers
auto frontend_translate_key(SDL_Keycode key, SDL_Keymod mod) -> uint8_t;
auto frontend_handle_key_event(SDL_Keycode key, bool is_down) -> bool;

enum { NOT_ASCII = 0, ASCII };

// Function Keys F1 - F12
constexpr int btn_help = 0;
constexpr int btn_run = 1;
constexpr int btn_drive1 = 2;
constexpr int btn_drive2 = 3;
constexpr int btn_driveswap = 4;
constexpr int btn_fullscr = 5;
constexpr int btn_debug = 6;
constexpr int btn_setup = 7;
constexpr int btn_cycle = 8;
constexpr int btn_quit = 11;
// btn_savest and btn_loadst
constexpr int btn_savest = 10;
constexpr int btn_loadst = 9;

// if you gonna change these values, consider changing some values in Video.cpp
#define SCREEN_BPP 8
extern SdlSurfacePtr_t g_screen;
extern SdlWindowPtr_t g_window;
extern SdlRendererPtr_t g_renderer;
extern SdlTexturePtr_t g_texture;

#define SHOW_CYCLES 15

extern bool g_window_resized;

extern SDL_Rect g_orig_rect;
extern SDL_Rect g_new_rect;

auto init_sdl() -> int;

auto frame_create_window() -> int;
void frame_destroy_window();

void frame_refresh_status(int);

void frame_register_class();

void frame_release_dc();

void frame_release_video_dc();

void draw_apple_content();
void draw_frame_window();
void frame_on_resize(int width, int height);
void frame_on_focus(bool gained);
void frame_on_expose();
void frame_show_help_screen(int sx, int sy);

void set_using_cursor(bool);

void set_fullscreen_mode();

void set_normal_mode();

void harddisk_ui_ftp_select(int drive);
void harddisk_ui_select(int drive);

extern bool g_scroll_lock_full_speed;
