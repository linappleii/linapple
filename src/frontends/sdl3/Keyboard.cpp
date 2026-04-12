#include "core/Common.h"
#include "apple2/Keyboard.h"
#include "frontends/sdl3/Frontend.h"
#include "SDL3/SDL.h"
#include "apple2/Structs.h"
#include "core/LinAppleCore.h"

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

LinAppleKey Frontend_ToCoreKey(int key, unsigned int mod) {
  bool bShift = (mod & SDL_KMOD_SHIFT) != 0;
  bool bCtrl = (mod & SDL_KMOD_CTRL) != 0;

  if (key < 128 && key >= 32) {
      if (bCtrl && key >= 'a' && key <= 'z') return (LinAppleKey)(key - 'a' + 1);
      if (bShift) {
          if (key >= 'a' && key <= 'z') return (LinAppleKey)(key - 'a' + 'A');
          if (key >= '0' && key <= '9') {
              static const uint8_t shift_nums[] = { ')','!','@','#','$','%','^','&','*','(' };
              return (LinAppleKey)shift_nums[key - '0'];
          }
          switch(key) {
              case SDLK_GRAVE: return (LinAppleKey)'~';
              case SDLK_MINUS: return (LinAppleKey)'_';
              case SDLK_EQUALS: return (LinAppleKey)'+';
              case SDLK_LEFTBRACKET: return (LinAppleKey)'{';
              case SDLK_RIGHTBRACKET: return (LinAppleKey)'}';
              case SDLK_BACKSLASH: return (LinAppleKey)'|';
              case SDLK_SEMICOLON: return (LinAppleKey)':';
              case SDLK_APOSTROPHE: return (LinAppleKey)'"';
              case SDLK_COMMA: return (LinAppleKey)'<';
              case SDLK_PERIOD: return (LinAppleKey)'>';
              case SDLK_SLASH: return (LinAppleKey)'?';
          }
      }
      return (LinAppleKey)key;
  }

  switch (key) {
    case SDLK_RETURN: return LINAPPLE_KEY_RETURN;
    case SDLK_ESCAPE: return LINAPPLE_KEY_ESCAPE;
    case SDLK_BACKSPACE: return LINAPPLE_KEY_BACKSPACE;
    case SDLK_TAB:    return LINAPPLE_KEY_TAB;
    case SDLK_SPACE:  return LINAPPLE_KEY_SPACE;
    case SDLK_UP:     return LINAPPLE_KEY_UP;
    case SDLK_DOWN:   return LINAPPLE_KEY_DOWN;
    case SDLK_LEFT:   return LINAPPLE_KEY_LEFT;
    case SDLK_RIGHT:  return LINAPPLE_KEY_RIGHT;
    case SDLK_PAGEUP: return LINAPPLE_KEY_PAGEUP;
    case SDLK_PAGEDOWN: return LINAPPLE_KEY_PAGEDOWN;
    case SDLK_HOME:   return LINAPPLE_KEY_HOME;
    case SDLK_END:    return LINAPPLE_KEY_END;
    case SDLK_INSERT: return LINAPPLE_KEY_INSERT;
    case SDLK_DELETE: return LINAPPLE_KEY_DELETE;
    case SDLK_F1: return LINAPPLE_KEY_F1;
    case SDLK_F2: return LINAPPLE_KEY_F2;
    case SDLK_F3: return LINAPPLE_KEY_F3;
    case SDLK_F4: return LINAPPLE_KEY_F4;
    case SDLK_F5: return LINAPPLE_KEY_F5;
    case SDLK_F6: return LINAPPLE_KEY_F6;
    case SDLK_F7: return LINAPPLE_KEY_F7;
    case SDLK_F8: return LINAPPLE_KEY_F8;
    case SDLK_F9: return LINAPPLE_KEY_F9;
    case SDLK_F10: return LINAPPLE_KEY_F10;
    case SDLK_F11: return LINAPPLE_KEY_F11;
    case SDLK_F12: return LINAPPLE_KEY_F12;
    case SDLK_KP_PLUS: return LINAPPLE_KEY_KP_PLUS;
    case SDLK_KP_MINUS: return LINAPPLE_KEY_KP_MINUS;
    case SDLK_KP_MULTIPLY: return LINAPPLE_KEY_KP_MULTIPLY;
    case SDLK_KP_DIVIDE: return LINAPPLE_KEY_KP_DIVIDE;
    case SDLK_KP_ENTER: return LINAPPLE_KEY_KP_ENTER;
    case SDLK_LSHIFT: return LINAPPLE_KEY_LSHIFT;
    case SDLK_RSHIFT: return LINAPPLE_KEY_RSHIFT;
    case SDLK_LCTRL: return LINAPPLE_KEY_LCTRL;
    case SDLK_RCTRL: return LINAPPLE_KEY_RCTRL;
    case SDLK_LALT: return LINAPPLE_KEY_LALT;
    case SDLK_RALT: return LINAPPLE_KEY_RALT;
    default: return LINAPPLE_KEY_UNKNOWN;
  }
}

bool Frontend_HandleKeyEvent(SDL_Keycode key, bool bDown) {
  switch (key) {
    case SDLK_LALT:
    case SDLK_LGUI:
      Linapple_SetAppleKey(0, bDown);
      return true;

    case SDLK_RALT:
    case SDLK_RGUI:
      Linapple_SetAppleKey(1, bDown);
      return true;

    default:
      return false;
  }
}
