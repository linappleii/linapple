#include "stdafx.h"
#include "Common.h"
#include "Keyboard.h"
#include "AppleWin.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <iostream>
#include <vector>

// Mock for Linapple Core dispatcher
static int last_apple_key = -1;
static bool last_apple_down = false;

void Linapple_SetAppleKey(int apple_key, bool bDown) {
    last_apple_key = apple_key;
    last_apple_down = bDown;
}

// Minimal mocks for other dependencies
void FrameRefreshStatus(int) {}

struct KeyTest {
    SDL_Keycode sdl_key;
    SDL_Keymod sdl_mod;
    uint8_t expected_apple;
    const char* description;
};

void test_key(const KeyTest& t) {
    uint8_t result = Frontend_TranslateKey(t.sdl_key, t.sdl_mod);
    if (result != t.expected_apple) {
        std::cerr << "Test Failed: " << t.description << std::endl;
        std::cerr << "  SDL Key: 0x" << std::hex << t.sdl_key << ", Mod: 0x" << t.sdl_mod << std::endl;
        std::cerr << "  Expected: 0x" << (int)t.expected_apple << ", Got: 0x" << (int)result << std::dec << std::endl;
        exit(1);
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    g_Apple2Type = A2TYPE_APPLE2EENHANCED;

    // Apple IIe defaults to Caps Lock ON, so toggle it OFF for our lowercase tests
    if (KeybGetCapsStatus()) KeybToggleCapsLock();

    std::vector<KeyTest> tests = {
        // Basic Alpha
        {SDLK_A, SDL_KMOD_NONE, 'a', "a"},
        {SDLK_A, SDL_KMOD_SHIFT, 'A', "A (Shift)"},
        {SDLK_A, SDL_KMOD_LCTRL, 0x01, "Ctrl-A"},

        // Numbers
        {SDLK_1, SDL_KMOD_NONE, '1', "1"},
        {SDLK_1, SDL_KMOD_SHIFT, '!', "1 (Shift) -> !"},

        // Special Keys (Apple IIe mode)
        {SDLK_RETURN,    SDL_KMOD_NONE, 0x0D, "Return"},
        {SDLK_ESCAPE,    SDL_KMOD_NONE, 0x1B, "Escape"},
        {SDLK_BACKSPACE, SDL_KMOD_NONE, 0x08, "Backspace"},
        {SDLK_TAB,       SDL_KMOD_NONE, 0x09, "Tab"},
        {SDLK_SPACE,     SDL_KMOD_NONE, 0x20, "Space"},

        // Arrows (IIe mode)
        {SDLK_LEFT,  SDL_KMOD_NONE, 0x08, "Left"},
        {SDLK_RIGHT, SDL_KMOD_NONE, 0x15, "Right"},
        {SDLK_UP,    SDL_KMOD_NONE, 0x0B, "Up (IIe)"},
        {SDLK_DOWN,  SDL_KMOD_NONE, 0x0A, "Down (IIe)"},
        {SDLK_DELETE,SDL_KMOD_NONE, 0x7F, "Delete (IIe)"}
    };

    std::cout << "Running keyboard translation tests..." << std::endl;
    for (const auto& t : tests) {
        test_key(t);
    }

    // --- Apple Key Mapping Tests ---
    std::cout << "Running Apple Key (Alt/Command) mapping tests..." << std::endl;
    
    // Test Open Apple (Left Alt)
    last_apple_key = -1;
    Frontend_HandleKeyEvent(SDLK_LALT, true);
    assert(last_apple_key == 0);
    assert(last_apple_down == true);
    Frontend_HandleKeyEvent(SDLK_LALT, false);
    assert(last_apple_down == false);

    // Test Open Apple (Left Command/GUI)
    last_apple_key = -1;
    Frontend_HandleKeyEvent(SDLK_LGUI, true);
    assert(last_apple_key == 0);
    assert(last_apple_down == true);

    // Test Closed Apple (Right Alt)
    last_apple_key = -1;
    Frontend_HandleKeyEvent(SDLK_RALT, true);
    assert(last_apple_key == 1);
    assert(last_apple_down == true);

    // Test Closed Apple (Right Command/GUI)
    last_apple_key = -1;
    Frontend_HandleKeyEvent(SDLK_RGUI, true);
    assert(last_apple_key == 1);
    assert(last_apple_down == true);

    std::cout << "All keyboard and Apple Key tests passed!" << std::endl;
    return 0;
}
