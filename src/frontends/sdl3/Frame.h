#include <SDL3/SDL.h>

#include <cstdint>
#pragma once

// Frontend-specific keyboard helpers
auto frontend_translate_key(SDL_Keycode key, SDL_Keymod mod) -> uint8_t;
auto frontend_handle_event(SDL_Keycode key, bool bDown) -> bool;

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
extern SDL_Surface* g_screen;
extern SDL_Window* g_window;
extern SDL_Renderer* g_renderer;
extern SDL_Texture* g_texture;

#define SHOW_CYCLES 15

extern bool g_window_resized;

extern SDL_Rect g_orig_rect;
extern SDL_Rect g_new_rect;

auto init_sdl() -> int;

auto frame_create_window() -> int;

void frame_refresh_status(int);

void FrameRegisterClass();

void FrameReleaseDC();

void FrameReleaseVideoDC();

void DrawAppleContent();
void DrawFrameWindow();
void Frame_OnResize(int width, int height);
void Frame_OnFocus(bool gained);
void Frame_OnExpose();

void set_using_cursor(bool);

void SetFullScreenMode();

void SetNormalMode();

void HarddiskUI_FTPSelect(int nDrive);
void HarddiskUI_Select(int nDrive);

extern bool g_scroll_lock_full_speed;
