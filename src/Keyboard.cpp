/*
linapple : An Apple //e emulator for Linux

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: Keyboard emulation
 *
 * Author: Various
 */



#include "stdafx.h"
#include <iostream>

#ifndef SDLK_WORLD_20
#define SDLK_WORLD_20 0xB4
#endif
#ifndef SDLK_WORLD_63
#define SDLK_WORLD_63 0xDF
#endif
#ifndef SDLK_WORLD_64
#define SDLK_WORLD_64 0xE0
#endif
#ifndef SDLK_WORLD_68
#define SDLK_WORLD_68 0xE4
#endif
#ifndef SDLK_WORLD_71
#define SDLK_WORLD_71 0xE7
#endif
#ifndef SDLK_WORLD_72
#define SDLK_WORLD_72 0xE8
#endif
#ifndef SDLK_WORLD_73
#define SDLK_WORLD_73 0xE9
#endif
#ifndef SDLK_WORLD_86
#define SDLK_WORLD_86 0xF6
#endif
#ifndef SDLK_WORLD_89
#define SDLK_WORLD_89 0xF9
#endif
#ifndef SDLK_WORLD_92
#define SDLK_WORLD_92 0xFC
#endif

bool g_bShiftKey = false;
bool g_bCtrlKey = false;
bool g_bAltKey = false;
bool g_bAltGrKey = false;

static bool g_bCapsLock = true;
static int lastvirtkey = 0;
static unsigned char keycode = 0;
static unsigned int keyboardqueries = 0;

KeybLanguage g_KeyboardLanguage = English_US;
bool         g_KeyboardRockerSwitch = false;
static bool  g_bKeybBufferEnable = true;

// Buffered key input:
// - Needed on faster PCs where aliasing occurs during short/fast bursts of 6502 code.
// - Keyboard only sampled during 6502 execution, so if it's run too fast then key presses will be missed.
const int KEY_BUFFER_MIN_SIZE = 1;
const int KEY_BUFFER_MAX_SIZE = 16;
static int g_nKeyBufferSize = KEY_BUFFER_MAX_SIZE;
static int g_nNextInIdx = 0;
static int g_nNextOutIdx = 0;
static int g_nKeyBufferCnt = 0;

static struct {
  int nVirtKey;
  unsigned char nAppleKey;
  uint64_t nTimestamp;
} g_nKeyBuffer[KEY_BUFFER_MAX_SIZE];

static unsigned char g_nLastKey = 0x00;

// All globally accessible functions are below this line

void KeybReset() {
  g_nNextInIdx = 0;
  g_nNextOutIdx = 0;
  g_nKeyBufferCnt = 0;
  g_nLastKey = 0x00;

  g_nKeyBufferSize = g_bKeybBufferEnable ? KEY_BUFFER_MAX_SIZE : KEY_BUFFER_MIN_SIZE;
}

bool KeybGetAltStatus() {
  return g_bAltKey;
}

bool KeybGetCapsStatus() {
  return g_bCapsLock;
}

bool KeybGetCtrlStatus() {
  return g_bCtrlKey;
}

bool KeybGetShiftStatus() {
  return g_bShiftKey;
}

void KeybUpdateCtrlShiftStatus() {
  const bool *keys;
  keys = SDL_GetKeyboardState(NULL);

  g_bShiftKey = (keys[SDL_SCANCODE_LSHIFT] | keys[SDL_SCANCODE_RSHIFT]);
  g_bCtrlKey = (keys[SDL_SCANCODE_LCTRL] | keys[SDL_SCANCODE_RCTRL]);
  g_bAltKey = (keys[SDL_SCANCODE_LALT] | keys[SDL_SCANCODE_RALT]);

  if (g_KeyboardLanguage == Spanish_ES) {
    g_bAltKey = keys[SDL_SCANCODE_LALT];
    g_bAltGrKey = keys[SDL_SCANCODE_RALT];
  }
}

unsigned char KeybGetKeycode()
{
  return keycode;
}

unsigned int KeybGetNumQueries()
{
  unsigned int result = keyboardqueries;
  keyboardqueries = 0;
  return result;
}

// decode keys for US-keyboard
int KeybDecodeKeyUS(int key)
{
  if (g_bShiftKey) {
    switch (key) {
      case '1':
        return '!';
      case '2':
        return '@';
      case '3':
        return '#';
      case '4':
        return '$';
      case '5':
        return '%';
      case '6':
        return '^';
      case '7':
        return '&';
      case '8':
        return '*';
      case '9':
        return '(';
      case '0':
        return ')';
      case '`':
        return '~';
      case '-':
        return '_';
      case '=':
        return '+';
      case '\\':
        return '|';
      case '[':
        return '{';
      case ']':
        return '}';
      case ';':
        return ':';
      case '\'':
        return '"';
      case ',':
        return '<';
      case '.':
        return '>';
      case '/':
        return '?';
      default:
        break;
    }
  }
  return key;
}

// decode keys for UK keyboard to Apple characters
int KeybDecodeKeyUK(int key)
{
  /* UK is almost identical to US layout, except for '#' vs 'the pound' character.
   * Pound and '#' share the same Apple II ASCII code though - and their non-shifted
   * keycode on the host PC is also identical - so no special keyboard mapping is
   * required to achieve the proper mapping. We just rely on the US-conversion
   * (no need to consider the rocker switch here - it only affects video output in
   * UK mode). */
  return KeybDecodeKeyUS(key);
}

// decode keys for German keyboard to Apple characters
int KeybDecodeKeyDE(int key)
{
  if (!g_KeyboardRockerSwitch)
  {
    /* Rocker switch in US-character-set mode: do a 'reverse' keyboard mapping, assuming the host PC's
     * native keyboard has a German keyboard layout, so converting back to the respective character of a
     * US keyboard layout. */

    if (key == 'z')
      return 'y';
    else
    if (key == 'y')
      return 'z';
    if ((key>='0')&&(key<='9'))
    {
      return KeybDecodeKeyUS(key);
    }

    // reverse mapping of further German keyboard keys to appropriate US-keyboard keys, also considering the ShiftKey
    switch(key)
    {
      case SDLK_WORLD_63: // German SZ
        return (g_bShiftKey) ? '_' : '-';
      case SDLK_WORLD_20: // key next to the German SZ
        return (g_bShiftKey) ? '+' : '=';
      case SDLK_WORLD_68: // German umlaut-a
        return (g_bShiftKey) ? '"' : '\'';
      case SDLK_WORLD_86: // German umlaut-o
        return (g_bShiftKey) ? ':' : ';';
      case SDLK_WORLD_92: // German umlaut-u
        return (g_bShiftKey) ? '{' : '[';
      case '+':
        return (g_bShiftKey) ? '}' : ']';
      case '#':
        return (g_bShiftKey) ? '~' : '`';
      case ',':
        return (g_bShiftKey) ? '<' : ',';
        break;
      case '.':
        return (g_bShiftKey) ? '>' : '.';
        break;
      case '-':
        return (g_bShiftKey) ? '?' : '/';
        break;
      case '<':
        return (g_bShiftKey) ? '|' : '\\';
      default:
        break;
    }

    return key;
  }

  // rocker switch is in German-character-set mode

  if (g_bShiftKey) {
    switch (key) {
      case '1':
        return '!';
      case '2':
        return '"';
      case '3':
        return '@';
      case '4':
        return '$';
      case '5':
        return '%';
      case '6':
        return '&';
      case '7':
        return '/';
      case '8':
        return '(';
      case '9':
        return ')';
      case '0':
        return '=';
      case '-':
        return '_';
      case '+':
        return '*';
      case '#':
        return '^';
      case ',':
        return ';';
      case '.':
        return ':';
      case '<':
        return '>';
      default:
        break;
    }
  }

  switch (key) {
    case SDLK_WORLD_63: // German S
      return (g_bShiftKey) ? '?' : '~';
    case SDLK_WORLD_20: // key next to the German SZ
      return (g_bShiftKey) ? '`' : '\'';
    case SDLK_WORLD_68: // German umlaut-a
      return (g_bShiftKey || g_bCapsLock) ? '[' : '{';
    case SDLK_WORLD_86: // German umlaut-o
      return (g_bShiftKey || g_bCapsLock) ? '\\' : '|';
    case SDLK_WORLD_92: // German umlaut-u
      return (g_bShiftKey || g_bCapsLock) ? ']' : '}';
    default:
      break;
  }

  return key;
}

// decode keys for French keyboard to Apple characters
int KeybDecodeKeyFR(int key)
{
  if (!g_KeyboardRockerSwitch)
  {
    /* Rocker switch in US-character-set mode: do a 'reverse' keyboard mapping, assuming the host PC's
     * native keyboard has a French keyboard layout, so converting back to the respective character of a
     * US keyboard layout. */
    switch(key)
    {
      case '&':
        return (g_bShiftKey) ? '!' : '1';
      case SDLK_WORLD_73:
        return (g_bShiftKey) ? '@' : '2';
      case '"':
        return (g_bShiftKey) ? '#' : '3';
      case '\'':
        return (g_bShiftKey) ? '$' : '4';
      case '(':
        return (g_bShiftKey) ? '%' : '5';
      case '-':
        return (g_bShiftKey) ? '^' : '6';
      case SDLK_WORLD_72:
        return (g_bShiftKey) ? '&' : '7';
      case '_':
        return (g_bShiftKey) ? '*' : '8';
      case SDLK_WORLD_71:
        return (g_bShiftKey) ? '(' : '9';
      case SDLK_WORLD_64:
        return (g_bShiftKey) ? ')' : '0';
      case ')':
        return (g_bShiftKey) ? '_' : '-';
      case '=':
        return (g_bShiftKey) ? '+' : '=';
      case 'a':
        return 'q';
      case 'z':
        return 'w';
      case '^':
        return (g_bShiftKey) ? '{' : '[';
      case '$':
        return (g_bShiftKey) ? '}' : ']';
      case 'q':
        return 'a';
      case 'm':
        return (g_bShiftKey) ? ':' : ';';
      case SDLK_WORLD_89:
        return (g_bShiftKey) ? '"' : '\'';
      case '*':
        return '~';
      case '<':
        return (g_bShiftKey) ? '|' : '\\';
      case 'w':
        return 'z';
      case ',':
        return (g_bShiftKey | g_bCapsLock) ? 'M' : 'm';
      case ';':
        return (g_bShiftKey) ? '<' : ',';
      case ':':
        return (g_bShiftKey) ? '>' : '.';
      case '!':
        return (g_bShiftKey) ? '?' : '/';
      default:
        break;
    }
    return key;
  }

  // rocker switch is in French-character-set mode
  switch(key)
  {
    case '&':
      return (g_bShiftKey) ? '1' : '&';
    case SDLK_WORLD_73:
      return (g_bShiftKey) ? '2' : '{';
    case '"':
      return (g_bShiftKey) ? '3' : '"';
    case '\'':
      return (g_bShiftKey) ? '4' : '\'';
    case '(':
      return (g_bShiftKey) ? '5' : '(';
    case '-':
      return (g_bShiftKey) ? '6' : ']';
    case SDLK_WORLD_72:
      return (g_bShiftKey) ? '7' : '}';
    case '_':
      return (g_bShiftKey) ? '8' : '!';
    case SDLK_WORLD_71:
      return (g_bShiftKey) ? '9' : '\\';
    case SDLK_WORLD_64:
      return (g_bShiftKey) ? '0' : '@';
    case ')':
      return (g_bShiftKey) ? '[' : ')';
    case '=':
      return (g_bShiftKey) ? '_' : '-';
    case '^':
      return (g_bShiftKey) ? '~' : '^';
    case '$':
      return (g_bShiftKey) ? '*' : '$';
    case SDLK_WORLD_89:
      return (g_bShiftKey) ? '%' : '|';
    case '*':
      return (g_bShiftKey) ? '`' : '#';
    case '<':
      return (g_bShiftKey) ? '>' : '<';
    case ',':
      return (g_bShiftKey) ? '?' : ',';
    case ';':
      return (g_bShiftKey) ? '.' : ';';
    case ':':
      return (g_bShiftKey) ? '/' : ':';
    case '!':
      return (g_bShiftKey) ? '+' : '=';
    default:
      break;
  }

  return key;
}

// decode keys for ES-keyboard  (EU Version Spanish)
int KeybDecodeKeyES(int key)
{
  if (g_bShiftKey) {
    switch (key) {
      case '1':
        return '!';
      case '2':
        return '"';
      case '3':
        return '#';
      case '4':
        return '$';
      case '5':
        return '%';
      case '6':
        return '&';
      case '7':
        return '/';
      case '8':
        return '(';
      case '9':
        return ')';
      case '0':
        return '=';
      case '\'':
        return '?';
      case '`':
        return '^';
      case '+':
        return '*';
      case '-':
        return '_';
      case '.':
        return ':';
      case ',':
        return ';';
      case '<':
        return '>';
      default:
        break;
    }
  }

  if (g_bAltGrKey) {
    switch (key) {
      case '2':
        return '@';
      case '3':
        return '#';
      case '4':
        return '~';
      case '+':
        return ']';
      default:
        break;
    }
  }

  switch (key) {
    case 180:
      return '{';
    case 186:
      return '\\';
    case 231:
      return '}';
    default:
      break;
  }

  return key;
}

// decode keycode for selected keyboard
int KeybDecodeKey(int key)
{
  KeybUpdateCtrlShiftStatus();

  switch(g_KeyboardLanguage)
  {
    case English_UK:
      key = KeybDecodeKeyUK(key);
      break;
    case French_FR:
      key = KeybDecodeKeyFR(key);
      break;
    case German_DE:
      key = KeybDecodeKeyDE(key);
      break;
    case Spanish_ES:
      key = KeybDecodeKeyES(key);
      break;
    case English_US:
    default:
      key = KeybDecodeKeyUS(key);
      break;
  }

  return key;
}

void KeybQueueKeypress(int key, bool bASCII)
{
  key = KeybDecodeKey(key);

  if ((key>=0)&&(key < 0x80)) {
    if (g_bCtrlKey) {
      if (key >= 'a' && key <= 'z') {
        key = key - 'a' + 1;
      } else {
        switch (key) {
          case '\\':
            key = 28;
            break;
          case '[' :
            key = 27;
            break;
          case ']' :
            key = 29;
            break;
          case SDLK_RETURN:
            key = 10;
            break;

          default:
            break;
        }
      }
    }

    if (!IS_APPLE2()) {
      if (g_bCapsLock && (key >= 'a') && (key <= 'z')) {
        keycode = key - 32;
      } else {
        keycode = key;
      }
    } else {
      if (key >= '`') {
        keycode = key - 32;
      } else {
        keycode = key;
      }
    }
    lastvirtkey = key;
  } else {
    if (IS_APPLE2()) {
      switch (key) {
        case SDLK_LEFT:
          keycode = 0x08;
          break;
        case SDLK_UP:
          keycode = 0x0D; // CR for Apple ][
          break;
        case SDLK_RIGHT:
          keycode = 0x15;
          break;
        case SDLK_DOWN:
          keycode = 0x2F; // '/' for Apple ][
          break;
        case SDLK_DELETE:
          keycode = 0x00;
          break;
        default:
          return;
      }
    } else {
      switch (key) {
        case SDLK_LEFT:
          keycode = 0x08;
          break;
        case SDLK_UP:
          keycode = 0x0B; // Control-K for IIe
          break;
        case SDLK_RIGHT:
          keycode = 0x15;
          break;
        case SDLK_DOWN:
          keycode = 0x0A; // Control-J for IIe
          break;
        case SDLK_DELETE:
          keycode = 0x7F;
          break;
        default:
          return;
      }
    }
    lastvirtkey = key;
  }

  bool bOverflow = false;

  if(g_nKeyBufferCnt < g_nKeyBufferSize) {
    g_nKeyBufferCnt++;
  } else {
    bOverflow = true;
  }

  g_nKeyBuffer[g_nNextInIdx].nVirtKey = lastvirtkey;
  g_nKeyBuffer[g_nNextInIdx].nAppleKey = keycode;
  g_nKeyBuffer[g_nNextInIdx].nTimestamp = SDL_GetTicks();
  g_nNextInIdx = (g_nNextInIdx + 1) % g_nKeyBufferSize;

  if(bOverflow) {
    g_nNextOutIdx = (g_nNextOutIdx + 1) % g_nKeyBufferSize;
  }
}

unsigned char KeybReadData(unsigned short, unsigned short, unsigned char, unsigned char, uint32_t) {
  keyboardqueries++;

  unsigned char nKey = g_nKeyBufferCnt ? 0x80 : 0;
  if(g_nKeyBufferCnt)
  {
    nKey |= g_nKeyBuffer[g_nNextOutIdx].nAppleKey;
    g_nLastKey = g_nKeyBuffer[g_nNextOutIdx].nAppleKey;

    if (g_nKeyBuffer[g_nNextOutIdx].nTimestamp > 0) {
        uint64_t now = SDL_GetTicks();
        fprintf(stderr, "PERF: Key 0x%02X read after %llu ms\n", (int)g_nLastKey, (unsigned long long)(now - g_nKeyBuffer[g_nNextOutIdx].nTimestamp));
        fflush(stderr);
        g_nKeyBuffer[g_nNextOutIdx].nTimestamp = 0;
    }
  }
  else
  {
    nKey |= g_nLastKey;
  }
  return nKey;
}

unsigned char KeybReadFlag(unsigned short, unsigned short, unsigned char, unsigned char, uint32_t) {
  keyboardqueries++;

  const bool *keys;
  keys = SDL_GetKeyboardState(NULL);
  unsigned char nKey = (keys[SDL_GetScancodeFromKey(g_nKeyBuffer[g_nNextOutIdx].nVirtKey, NULL)]) ? 0x80 : 0;
  nKey |= g_nKeyBuffer[g_nNextOutIdx].nAppleKey;
  if(g_nKeyBufferCnt) {
    if (g_nKeyBuffer[g_nNextOutIdx].nTimestamp > 0) {
        uint64_t now = SDL_GetTicks();
        fprintf(stderr, "PERF: Key 0x%02X cleared after %llu ms\n", (int)g_nKeyBuffer[g_nNextOutIdx].nAppleKey, (unsigned long long)(now - g_nKeyBuffer[g_nNextOutIdx].nTimestamp));
        fflush(stderr);
    }
    g_nKeyBufferCnt--;
    g_nNextOutIdx = (g_nNextOutIdx + 1) % g_nKeyBufferSize;
  }
  return nKey;
}

void KeybToggleCapsLock()
{
  if (!IS_APPLE2()) {
    g_bCapsLock = !g_bCapsLock;
    FrameRefreshStatus(DRAW_LEDS);
  }
}

unsigned int KeybGetSnapshot(SS_IO_Keyboard *pSS) {
  pSS->keyboardqueries = keyboardqueries;
  pSS->nLastKey = g_nLastKey;
  return 0;
}

unsigned int KeybSetSnapshot(SS_IO_Keyboard *pSS) {
  keyboardqueries = pSS->keyboardqueries;
  g_nLastKey = pSS->nLastKey;
  return 0;
}
