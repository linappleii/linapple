#pragma once
#include <SDL/SDL.h>

void JoyFrontend_Initialize();
void JoyFrontend_ShutDown();
void JoyFrontend_CheckExit();
void JoyFrontend_Update();
void JoyFrontend_UpdateTrimViaKey(SDLKey virtkey);
auto joy_frontend_process_key(SDLKey virtkey, bool extended, bool down,
                              bool autorep) -> bool;
auto JoyFrontend_IsMouseEmulationActive() -> bool;
auto JoyFrontend_ProcessMouseMotion(int x, int max_x, int y, int max_y) -> void;
auto JoyFrontend_ProcessMouseButton(int button, bool down) -> void;
