#include "stdafx.h"
#include "Common.h"
#include "Keyboard.h"
#include "Frame.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <iostream>
#include <vector>

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
        {SDLK_0, SDL_KMOD_NONE, '0', "0"},
        {SDLK_0, SDL_KMOD_SHIFT, ')', "0 (Shift) -> )"},

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
        {SDLK_DELETE,SDL_KMOD_NONE, 0x7F, "Delete (IIe)"},

        // Symbols
        {SDLK_GRAVE, SDL_KMOD_NONE, '`', "`"},
        {SDLK_GRAVE, SDL_KMOD_SHIFT, '~', "~"},
        {SDLK_MINUS,     SDL_KMOD_NONE, '-', "-"},
        {SDLK_MINUS,     SDL_KMOD_SHIFT, '_', "_"},
        {SDLK_EQUALS,    SDL_KMOD_NONE, '=', "="},
        {SDLK_EQUALS,    SDL_KMOD_SHIFT, '+', "+"},
        {SDLK_LEFTBRACKET, SDL_KMOD_NONE, '[', "["},
        {SDLK_LEFTBRACKET, SDL_KMOD_SHIFT, '{', "{"},
        {SDLK_RIGHTBRACKET, SDL_KMOD_NONE, ']', "]"},
        {SDLK_RIGHTBRACKET, SDL_KMOD_SHIFT, '}', "}"},
        {SDLK_BACKSLASH,  SDL_KMOD_NONE, '\\', "\\"},
        {SDLK_BACKSLASH,  SDL_KMOD_SHIFT, '|', "|"},
        {SDLK_SEMICOLON,  SDL_KMOD_NONE, ';', ";"},
        {SDLK_SEMICOLON,  SDL_KMOD_SHIFT, ':', ":"},
        {SDLK_APOSTROPHE,  SDL_KMOD_NONE, '\'', "'"},
        {SDLK_APOSTROPHE,  SDL_KMOD_SHIFT, '\"', "\""},
        {SDLK_COMMA,      SDL_KMOD_NONE, ',', ","},
        {SDLK_COMMA,      SDL_KMOD_SHIFT, '<', "<"},
        {SDLK_PERIOD,     SDL_KMOD_NONE, '.', "."},
        {SDLK_PERIOD,     SDL_KMOD_SHIFT, '>', ">"},
        {SDLK_SLASH,      SDL_KMOD_NONE, '/', "/"},
        {SDLK_SLASH,      SDL_KMOD_SHIFT, '?', "?"}
    };

    std::cout << "Running " << tests.size() << " keyboard translation tests..." << std::endl;
    for (const auto& t : tests) {
        test_key(t);
    }

    // Test Caps Lock
    if (!KeybGetCapsStatus()) KeybToggleCapsLock();
    assert(KeybGetCapsStatus() == true);
    test_key({SDLK_Z, SDL_KMOD_NONE, 'Z', "z (Caps On)"});

    KeybToggleCapsLock();
    assert(KeybGetCapsStatus() == false);
    test_key({SDLK_Z, SDL_KMOD_NONE, 'z', "z (Caps Off)"});

    // Test Apple II (Original) mode
    g_Apple2Type = A2TYPE_APPLE2;
    std::cout << "Running Apple II (Original) specific tests..." << std::endl;
    test_key({SDLK_UP,    SDL_KMOD_NONE, 0x0D, "Up (II)"});
    test_key({SDLK_DOWN,  SDL_KMOD_NONE, 0x2F, "Down (II)"});
    test_key({SDLK_DELETE,SDL_KMOD_NONE, 0x00, "Delete (II)"});

    // Test AnyKeyDown and Strobe clearing
    std::cout << "Running AnyKeyDown and Strobe clearing tests..." << std::endl;
    g_Apple2Type = A2TYPE_APPLE2EENHANCED;
    KeybReset();
    assert(KeybGetAnyKeyDownStatus() == false);
    
    // Simulate key down
    KeybSetAnyKeyDownStatus(true);
    assert(KeybGetAnyKeyDownStatus() == true);
    
    // Check strobe and AKD bit in C010
    KeybPushAppleKey(0x41); // Push 'A'
    // Read C000 (Data)
    unsigned char data = KeybReadData(0, 0xC000, 0, 0, 0);
    assert((data & 0x80) != 0); // Strobe set
    
    // Read C010 (Flag)
    unsigned char flag = KeybReadFlag(0, 0xC010, 0, 0, 0);
    assert((flag & 0x80) != 0); // AKD bit set
    
    // Side effect: C010 should clear strobe
    data = KeybReadData(0, 0xC000, 0, 0, 0);
    assert((data & 0x80) == 0); // Strobe cleared
    
    // Simulate key up
    KeybSetAnyKeyDownStatus(false);
    assert(KeybGetAnyKeyDownStatus() == false);
    flag = KeybReadFlag(0, 0xC010, 0, 0, 0);
    assert((flag & 0x80) == 0); // AKD bit clear

    std::cout << "All keyboard tests passed!" << std::endl;
    return 0;
}
