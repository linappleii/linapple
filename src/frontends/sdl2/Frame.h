#include <SDL2/SDL.h>

#include <cstdint>
#pragma once

// Frontend-specific keyboard helpers
auto Frontend_TranslateKey(SDL_Keycode key, SDL_Keymod mod) -> uint8_t;
auto Frontend_HandleKeyEvent(SDL_Keycode key, bool bDown) -> bool;

enum { NOT_ASCII = 0, ASCII };

// Function Keys F1 - F12
constexpr int BTN_HELP = 0;
constexpr int BTN_RUN = 1;
constexpr int BTN_DRIVE1 = 2;
constexpr int BTN_DRIVE2 = 3;
constexpr int BTN_DRIVESWAP = 4;
constexpr int BTN_FULLSCR = 5;
constexpr int BTN_DEBUG = 6;
constexpr int BTN_SETUP = 7;
constexpr int BTN_CYCLE = 8;
constexpr int BTN_QUIT = 11;
// BTN_SAVEST and BTN_LOADST
constexpr int BTN_SAVEST = 10;
constexpr int BTN_LOADST = 9;

// if you gonna change these values, consider changing some values in Video.cpp
#define SCREEN_BPP 8
extern SDL_Surface* screen;
extern SDL_Window* g_window;
extern SDL_Renderer* g_renderer;
extern SDL_Texture* g_texture;

#define SHOW_CYCLES 15

extern bool g_WindowResized;

extern SDL_Rect origRect;
extern SDL_Rect newRect;

auto InitSDL() -> int;

auto FrameCreateWindow() -> int;

void frame_refresh_status(int);

void FrameRegisterClass();

void FrameReleaseDC();

void FrameReleaseVideoDC();

void DrawAppleContent();
void DrawFrameWindow();
void Frame_OnResize(int width, int height);
void Frame_OnFocus(bool gained);
void Frame_OnExpose();

void SetUsingCursor(bool);

void SetFullScreenMode();

void SetNormalMode();

void HarddiskUI_FTPSelect(int nDrive);
void HarddiskUI_Select(int nDrive);

extern bool g_bScrollLock_FullSpeed;
