#include <cstdint>
#include <SDL3/SDL.h>
#pragma once

// Frontend-specific keyboard helpers
uint8_t Frontend_TranslateKey(SDL_Keycode key, SDL_Keymod mod);
bool Frontend_HandleKeyEvent(SDL_Keycode key, bool bDown);

enum {
  NOT_ASCII = 0, ASCII
};

#define  VIEWPORTX   5
#define  VIEWPORTY   5


// if you gonna change these values, consider changing some values in Video.cpp
#define SCREEN_WIDTH  560
#define SCREEN_HEIGHT  384
#define SCREEN_BPP  8
extern SDL_Surface *screen;
extern SDL_Window *g_window;
extern SDL_Renderer *g_renderer;
extern SDL_Texture *g_texture;

#define SHOW_CYCLES  15

extern bool g_WindowResized;

extern SDL_Rect origRect;
extern SDL_Rect newRect;

int InitSDL();

int FrameCreateWindow();

void FrameRefreshStatus(int);

void FrameRegisterClass();

void FrameReleaseDC();

void FrameReleaseVideoDC();

void DrawAppleContent();
void DrawFrameWindow();
void FrameDispatchMessage(SDL_Event *e);

void SetUsingCursor(bool);

void SetFullScreenMode();

void SetNormalMode();

extern bool g_bScrollLock_FullSpeed;

