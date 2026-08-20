#pragma once
#include <SDL/SDL.h>

void JoyFrontend_Initialize();
void JoyFrontend_ShutDown();
void JoyFrontend_CheckExit();
void JoyFrontend_Update();
void JoyFrontend_UpdateTrimViaKey(SDLKey virtkey);
auto joy_frontend_process_key(SDLKey virtkey, bool extended, bool down,
                              bool autorep) -> bool;
