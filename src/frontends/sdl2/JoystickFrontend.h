#pragma once
#include <SDL2/SDL.h>

void JoyFrontend_Initialize();
void JoyFrontend_ShutDown();
void JoyFrontend_CheckExit();
void JoyFrontend_Update();
void JoyFrontend_UpdateTrimViaKey(SDL_Keycode virtkey);
auto JoyFrontend_ProcessKey(SDL_Keycode virtkey, bool extended, bool down,
                            bool autorep) -> bool;
