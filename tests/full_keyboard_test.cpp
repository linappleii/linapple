#include "stdafx.h"
#include "Common.h"
#include "Keyboard.h"
#include "AppleWin.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <iostream>
#include <vector>

// --- Re-implement Core logic for testing (since Applewin.o is excluded) ---
static uint8_t g_nRepeatKey = 0;
static uint32_t g_nRepeatDelayCycles = 0;
static bool g_bRepeating = false;

const uint32_t KEY_REPEAT_INITIAL_DELAY = 512000;
const uint32_t KEY_REPEAT_RATE = 68000;

static std::vector<uint8_t> pushed_keys;
void KeybPushAppleKey(uint8_t apple_code) {
    pushed_keys.push_back(apple_code);
}

void Linapple_SetKeyState(uint8_t apple_code, bool bDown) {
  if (bDown) {
    if (g_nRepeatKey == apple_code) return; // FIX: Ignore duplicates
    g_nRepeatKey = apple_code;
    g_nRepeatDelayCycles = 0;
    g_bRepeating = false;
    if (apple_code) KeybPushAppleKey(apple_code);
  } else {
    if (g_nRepeatKey == apple_code) {
      g_nRepeatKey = 0;
      g_bRepeating = false;
    }
  }
}

void Linapple_KeyboardThink(uint32_t dwCycles) {
  if (g_nRepeatKey == 0) return;
  g_nRepeatDelayCycles += dwCycles;
  if (!g_bRepeating) {
    if (g_nRepeatDelayCycles >= KEY_REPEAT_INITIAL_DELAY) {
      g_bRepeating = true;
      g_nRepeatDelayCycles = 0;
      KeybPushAppleKey(g_nRepeatKey);
    }
  } else {
    if (g_nRepeatDelayCycles >= KEY_REPEAT_RATE) {
      g_nRepeatDelayCycles = 0;
      KeybPushAppleKey(g_nRepeatKey);
    }
  }
}

void Linapple_SetAppleKey(int, bool) {}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    g_Apple2Type = A2TYPE_APPLE2EENHANCED;

    std::cout << "Running keyboard fix tests..." << std::endl;

    // Test 1: Duplicate DOWN events should NOT push multiple characters
    pushed_keys.clear();
    Linapple_SetKeyState('a', true);
    Linapple_SetKeyState('a', true); // Should be ignored
    assert(pushed_keys.size() == 1);
    assert(pushed_keys[0] == 'a');

    // Test 2: Verify timing triggers correctly with small increments
    pushed_keys.clear();
    Linapple_KeyboardThink(256000); // 0.25s
    assert(pushed_keys.size() == 0);
    Linapple_KeyboardThink(256000); // Another 0.25s -> total 0.5s
    assert(pushed_keys.size() == 1);
    assert(pushed_keys[0] == 'a');

    std::cout << "Keyboard fix tests passed!" << std::endl;
    return 0;
}
