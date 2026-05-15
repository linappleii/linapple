#pragma once
#include <SDL/SDL.h>

void JoyFrontend_Initialize();
void JoyFrontend_ShutDown();
void JoyFrontend_CheckExit();
void JoyFrontend_Update();
void JoyFrontend_UpdateTrimViaKey(SDLKey virtkey);
auto JoyFrontend_ProcessKey(SDLKey virtkey, bool extended, bool down,
                            bool autorep) -> bool;
