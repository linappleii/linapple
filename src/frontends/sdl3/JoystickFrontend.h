#pragma once
#include <SDL3/SDL.h>

void JoyFrontend_Initialize();
void JoyFrontend_ShutDown();
void JoyFrontend_CheckExit();
void JoyFrontend_Update();
void JoyFrontend_UpdateTrimViaKey(SDL_Keycode virtkey);
auto joy_frontend_process_key(SDL_Keycode virtkey, bool extended, bool down,
                              bool autorep) -> bool;
