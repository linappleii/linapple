#pragma once
#include <SDL/SDL.h>

void joy_frontend_initialize();
void joy_frontend_shutdown();
void joy_frontend_check_exit();
void joy_frontend_update();
void joy_frontend_update_trim_via_key(SDLKey virtkey);
auto joy_frontend_process_key(SDLKey virtkey, bool extended, bool down,
                              bool autorep) -> bool;
auto joy_frontend_is_mouse_emulation_active() -> bool;
auto joy_frontend_process_mouse_motion(int x, int max_x, int y, int max_y)
    -> void;
auto joy_frontend_process_mouse_button(int button, bool down) -> void;
