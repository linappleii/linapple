#include "stdafx.h"
#include "Keyboard.h"

// SDL3 specific key translation logic
uint8_t Frontend_TranslateKey(SDL_Keycode key, SDL_Keymod mod) {
  bool bShift = (mod & SDL_KMOD_SHIFT) != 0;
  bool bCtrl = (mod & SDL_KMOD_CTRL) != 0;
  bool bCaps = KeybGetCapsStatus();

  uint8_t apple_code = 0;

  if (key >= 'a' && key <= 'z') {
    if (bCtrl) {
      apple_code = key - 'a' + 1;
    } else if (bCaps || bShift) {
      apple_code = key - 'a' + 'A';
    } else {
      apple_code = key;
    }
  } else if (key >= '0' && key <= '9') {
    if (bShift) {
      static const uint8_t shift_nums[] = { ')','!','@','#','$','%','^','&','*','(' };
      apple_code = shift_nums[key - '0'];
    } else {
      apple_code = key;
    }
  } else {
    // Handling special keys and symbols
    switch (key) {
      case SDLK_RETURN: apple_code = 0x0D; break;
      case SDLK_ESCAPE: apple_code = 0x1B; break;
      case SDLK_BACKSPACE: apple_code = 0x08; break;
      case SDLK_TAB:    apple_code = 0x09; break;
      case SDLK_SPACE:  apple_code = 0x20; break;
      case SDLK_LEFT:   apple_code = 0x08; break;
      case SDLK_RIGHT:  apple_code = 0x15; break;
      case SDLK_UP:     apple_code = IS_APPLE2() ? 0x0D : 0x0B; break;
      case SDLK_DOWN:   apple_code = IS_APPLE2() ? 0x2F : 0x0A; break;
      case SDLK_DELETE: apple_code = IS_APPLE2() ? 0x00 : 0x7F; break;

      // Symbols
      case SDLK_GRAVE: apple_code = bShift ? '~' : '`'; break;
      case SDLK_MINUS: apple_code = bShift ? '_' : '-'; break;
      case SDLK_EQUALS: apple_code = bShift ? '+' : '='; break;
      case SDLK_LEFTBRACKET: apple_code = bShift ? '{' : '['; break;
      case SDLK_RIGHTBRACKET: apple_code = bShift ? '}' : ']'; break;
      case SDLK_BACKSLASH: apple_code = bShift ? '|' : '\\'; break;
      case SDLK_SEMICOLON: apple_code = bShift ? ':' : ';'; break;
      case SDLK_APOSTROPHE: apple_code = bShift ? '"' : '\''; break;
      case SDLK_COMMA: apple_code = bShift ? '<' : ','; break;
      case SDLK_PERIOD: apple_code = bShift ? '>' : '.'; break;
      case SDLK_SLASH: apple_code = bShift ? '?' : '/'; break;
      default: break;
    }
  }

  return apple_code;
}
