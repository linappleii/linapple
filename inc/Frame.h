#pragma once

enum {
  NOT_ASCII = 0, ASCII
};

// 3D Border  (do not use for now? --bb)
#define  VIEWPORTX   5
#define  VIEWPORTY   5


// if you gonna change these values, consider changing some values in Video.cpp --bb
#define SCREEN_WIDTH  560
#define SCREEN_HEIGHT  384
#define SCREEN_BPP  8
extern SDL_Surface *screen;

#define SHOW_CYCLES  15

extern bool fullscreen;
extern bool g_WindowResized;

extern SDL_Rect origRect;
extern SDL_Rect newRect;

int InitSDL();

int FrameCreateWindow();

void FrameRefreshStatus(int);

void FrameRegisterClass();

void FrameReleaseDC();

void FrameReleaseVideoDC();

void DrawFrameWindow();  // draw it!
void FrameDispatchMessage(SDL_Event *e); // replacement for FrameWndProc.

void SetUsingCursor(bool);

void SetFullScreenMode();

void SetNormalMode();

extern bool g_bScrollLock_FullSpeed;

