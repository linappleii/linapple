/*
AppleWin : An Apple //e emulator for Windows

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

/* Adaptation for SDL and POSIX (l) by beom beotiger, Nov-Dec 2007 */


#include "stdafx.h"
//#pragma  hdrstop

//static bool g_bKeybBufferEnable = false;

#define KEY_OLD

/*static BYTE asciicode[2][10] = {
  {0x08,0x0D,0x15,0x2F,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x08,0x0B,0x15,0x0A,0x00,0x00,0x00,0x00,0x00,0x7F}
};  // Convert PC arrow keys to Apple keycodes*/

/*static*/ bool  g_bShiftKey = false;
/*static*/ bool  g_bCtrlKey  = false;
/*static*/ bool  g_bAltKey   = false;
static bool  g_bCapsLock = true;
static int   lastvirtkey     = 0;  // Current PC keycode
static BYTE  keycode         = 0;  // Current Apple keycode
static DWORD keyboardqueries = 0;

#ifdef KEY_OLD
// Original
static BOOL  keywaiting      = 0;
#else
// Buffered key input:
// - Needed on faster PCs where aliasing occurs during short/fast bursts of 6502 code.
// - Keyboard only sampled during 6502 execution, so if it's run too fast then key presses will be missed.
const int KEY_BUFFER_MIN_SIZE = 1;
const int KEY_BUFFER_MAX_SIZE = 2;
static int g_nKeyBufferSize = KEY_BUFFER_MAX_SIZE;  // Circ key buffer size
static int g_nNextInIdx = 0;
static int g_nNextOutIdx = 0;
static int g_nKeyBufferCnt = 0;

static struct
{
  int nVirtKey;
  BYTE nAppleKey;
} g_nKeyBuffer[KEY_BUFFER_MAX_SIZE];
#endif

static BYTE g_nLastKey = 0x00;

//
// ----- ALL GLOBALLY ACCESSIBLE FUNCTIONS ARE BELOW THIS LINE -----
//

//===========================================================================

void KeybReset()
{
#ifdef KEY_OLD
  keywaiting = 0;
#else
  g_nNextInIdx = 0;
  g_nNextOutIdx = 0;
  g_nKeyBufferCnt = 0;
  g_nLastKey = 0x00;

  g_nKeyBufferSize = g_bKeybBufferEnable ? KEY_BUFFER_MAX_SIZE : KEY_BUFFER_MIN_SIZE;
#endif
}

//===========================================================================

//void KeybSetBufferMode(bool bNewKeybBufferEnable)
//{
//  if(g_bKeybBufferEnable == bNewKeybBufferEnable)
//    return;
//
//  g_bKeybBufferEnable = bNewKeybBufferEnable;
//  KeybReset();
//}
//
//bool KeybGetBufferMode()
//{
//  return g_bKeybBufferEnable;
//}

//===========================================================================
bool KeybGetAltStatus ()
{
  return g_bAltKey;
}

//===========================================================================
bool KeybGetCapsStatus ()
{
  return g_bCapsLock;
}

//===========================================================================
bool KeybGetCtrlStatus ()
{
  return g_bCtrlKey;
}

//===========================================================================
bool KeybGetShiftStatus ()
{
  return g_bShiftKey;
}

//===========================================================================
void KeybUpdateCtrlShiftStatus()
{
//  g_bShiftKey = (GetKeyState( VK_SHIFT  ) & KF_UP) ? true : false; // 0x8000 KF_UP
//  g_bCtrlKey  = (GetKeyState( VK_CONTROL) & KF_UP) ? true : false;
//  g_bAltKey   = (GetKeyState( VK_MENU   ) & KF_UP) ? true : false;
  Uint8 *keys;
  keys = SDL_GetKeyState(NULL);

  g_bShiftKey = (keys[SDLK_LSHIFT] | keys[SDLK_RSHIFT]); // 0x8000 KF_UP   SHIFT
  g_bCtrlKey  = (keys[SDLK_LCTRL]  | keys[SDLK_RCTRL]);  // CTRL
  g_bAltKey   = (keys[SDLK_LALT]   | keys[SDLK_RALT]);  // ALT
}

//===========================================================================
BYTE KeybGetKeycode()    // Used by MemCheckPaging() & VideoCheckMode()
{
  return keycode;
}

//===========================================================================
DWORD KeybGetNumQueries ()  // Used in determining 'idleness' of Apple system
{
  DWORD result = keyboardqueries;
  keyboardqueries = 0;
  return result;
}

//===========================================================================
void KeybQueueKeypress (int key, BOOL bASCII)
{
//  static bool bFreshReset; - do not use

  if (bASCII == ASCII)
  {
/*    if (bFreshReset && key == 0x03)
    {
      bFreshReset = 0;
      return; // Swallow spurious CTRL-C caused by CTRL-BREAK
    }
    bFreshReset = 0;*/
    if (key > 0x7F) return;
// Conver SHIFTed keys to their secondary values
// may be this is straitfoward method, but it seems to be working. What else we need?? --bb
    KeybUpdateCtrlShiftStatus();
    if(g_bShiftKey)     // SHIFT is pressed
      switch(key) {
        case '1': key = '!'; break;
        case '2': key = '@'; break;
        case '3': key = '#'; break;
        case '4': key = '$'; break;
        case '5': key = '%'; break;
        case '6': key = '^'; break;
        case '7': key = '&'; break;
        case '8': key = '*'; break;
        case '9': key = '('; break;
        case '0': key = ')'; break;
        case '`': key = '~'; break;
        case '-': key = '_'; break;
        case '=': key = '+'; break;
        case '\\': key = '|'; break;
        case '[': key = '{'; break;
        case ']': key = '}'; break;
        case ';': key = ':'; break;
        case '\'': key = '"'; break;
        case ',': key = '<'; break;
        case '.': key = '>'; break;
        case '/': key = '?'; break;
        default:        break;
      }
    else if (g_bCtrlKey) {
      if(key >= SDLK_a && key <= SDLK_z) key = key - SDLK_a + 1;
      else switch(key) {
        case '\\': key = 28; break;
        case '[' : key = 27; break;
        case ']' : key = 29; break;
        case SDLK_RETURN: key = 10; break;

        default: break;
      }
    }


    if (!IS_APPLE2)
    {
      if (g_bCapsLock && (key >= 'a') && (key <='z'))
        keycode = key - 32;
      else
        keycode = key;
    }
    else
    {
      if (key >= '`')
        keycode = key - 32;
      else
        keycode = key;
    }
    lastvirtkey = key;
  }
  else
  {
/*    if ((key == VK_CANCEL) && (GetKeyState(VK_CONTROL) < 0)) - implement in Frame.cpp
    {
      // Ctrl+Reset
      if (!IS_APPLE2)
        MemResetPaging();

      DiskReset();
      KeybReset();
      if (!IS_APPLE2)
        VideoResetState();  // Switch Alternate char set off
      MB_Reset();

#ifndef KEY_OLD
      g_nNextInIdx = g_nNextOutIdx = g_nKeyBufferCnt = 0;
#endif

      CpuReset();
      bFreshReset = 1;
      return;
    }
*/
/*   No pasting??? Ye-e-e-e-et! */
//     if ((key == VK_INSERT) && (GetKeyState(VK_SHIFT) < 0))
//     {
//       // Shift+Insert
//       ClipboardInitiatePaste();
//       return;
//     }

//     if (!((key >= VK_LEFT) && (key <= VK_DELETE) && asciicode[IS_APPLE2 ? 0 : 1][key - VK_LEFT]))
//       return;
//     keycode = asciicode[IS_APPLE2 ? 0 : 1][key - VK_LEFT];    // Convert to Apple arrow keycode
//     lastvirtkey = key;
//     {0x08,0x0D,0x15,0x2F,0x00,0x00,0x00,0x00,0x00,0x00}, - good old APPLE2
//     {0x08,0x0B,0x15,0x0A,0x00,0x00,0x00,0x00,0x00,0x7F}

    if(IS_APPLE2)
      switch(key) {
        case SDLK_LEFT: keycode = 0x08; break;
        case SDLK_UP:  keycode = 0x0D; break;
        case SDLK_RIGHT:keycode = 0x15; break;
        case SDLK_DOWN: keycode = 0x2F; break;
        case SDLK_DELETE:keycode = 0x00;break;
        default: return;
      }
    else
      switch(key) {
        case SDLK_LEFT: keycode = 0x08; break;
        case SDLK_UP:  keycode = 0x0B; break;
        case SDLK_RIGHT:keycode = 0x15; break;
        case SDLK_DOWN: keycode = 0x0A; break;
        case SDLK_DELETE:keycode = 0x7F;break;
        default: return;
      }
    lastvirtkey = key;
  }


#ifdef KEY_OLD
  keywaiting = 1;
#else
  bool bOverflow = false;

  if(g_nKeyBufferCnt < g_nKeyBufferSize)
    g_nKeyBufferCnt++;
  else
    bOverflow = true;

  g_nKeyBuffer[g_nNextInIdx].nVirtKey = lastvirtkey;
  g_nKeyBuffer[g_nNextInIdx].nAppleKey = keycode;
  g_nNextInIdx = (g_nNextInIdx + 1) % g_nKeyBufferSize;

  if(bOverflow)
    g_nNextOutIdx = (g_nNextOutIdx + 1) % g_nKeyBufferSize;
#endif
}

//===========================================================================

/*static HGLOBAL hglb = NULL;
static LPTSTR lptstr = NULL;
static bool g_bPasteFromClipboard = false;
static bool g_bClipboardActive = false;*/
/*
void ClipboardInitiatePaste()
{
  if (g_bClipboardActive)
    return;

  g_bPasteFromClipboard = true;
}

static void ClipboardDone()
{
  if (g_bClipboardActive)
  {
    g_bClipboardActive = false;
    GlobalUnlock(hglb);
    CloseClipboard();
  }
}

static void ClipboardInit()
{
  ClipboardDone();

  if (!IsClipboardFormatAvailable(CF_TEXT))
    return;

  if (!OpenClipboard(g_hFrameWindow))
    return;

  hglb = GetClipboardData(CF_TEXT);
  if (hglb == NULL)
  {
    CloseClipboard();
    return;
  }

  lptstr = (char*) GlobalLock(hglb);
  if (lptstr == NULL)
  {
    CloseClipboard();
    return;
  }

  g_bPasteFromClipboard = false;
  g_bClipboardActive = true;
}

static char ClipboardCurrChar(bool bIncPtr)
{
  char nKey;
  int nInc = 1;

  if((lptstr[0] == 0x0D) && (lptstr[1] == 0x0A))
  {
    nKey = 0x0D;
    nInc = 2;
  }
  else
  {
    nKey = lptstr[0];
  }

  if(bIncPtr)
    lptstr += nInc;

  return nKey;
}*/

//===========================================================================

BYTE /*__stdcall */KeybReadData (WORD, WORD, BYTE, BYTE, ULONG)
{
  keyboardqueries++;

//   if(g_bPasteFromClipboard)
//     ClipboardInit();
//
//   if(g_bClipboardActive)
//   {
//     if(*lptstr == 0)
//       ClipboardDone();
//     else
//       return 0x80 | ClipboardCurrChar(false);
//   }

  //

#ifdef KEY_OLD
  return keycode | (keywaiting ? 0x80 : 0);
#else
  BYTE nKey = g_nKeyBufferCnt ? 0x80 : 0;
  if(g_nKeyBufferCnt)
  {
    nKey |= g_nKeyBuffer[g_nNextOutIdx].nAppleKey;
    g_nLastKey = g_nKeyBuffer[g_nNextOutIdx].nAppleKey;
  }
  else
  {
    nKey |= g_nLastKey;
  }
  return nKey;
#endif
}

//===========================================================================

BYTE /*__stdcall */KeybReadFlag (WORD, WORD, BYTE, BYTE, ULONG)
{
  keyboardqueries++;

  //

//   if(g_bPasteFromClipboard)
//     ClipboardInit();
//
//   if(g_bClipboardActive)
//   {
//     if(*lptstr == 0)
//       ClipboardDone();
//     else
//       return 0x80 | ClipboardCurrChar(true);
//   }

  //

  Uint8 *keys;
  keys = SDL_GetKeyState(NULL); // get current key state - thanx to SDL developers! ^_^ beom beotiger
#ifdef KEY_OLD
  keywaiting = 0;
  return keycode | (keys[lastvirtkey] ? 0x80 : 0);
#else
  BYTE nKey = (keys[g_nKeyBuffer[g_nNextOutIdx].nVirtKey]) ? 0x80 : 0;
  nKey |= g_nKeyBuffer[g_nNextOutIdx].nAppleKey;
  if(g_nKeyBufferCnt)
  {
    g_nKeyBufferCnt--;
    g_nNextOutIdx = (g_nNextOutIdx + 1) % g_nKeyBufferSize;
  }
  return nKey;
#endif
}

//===========================================================================
void KeybToggleCapsLock ()
{
  if (!IS_APPLE2)
  {
    g_bCapsLock = !g_bCapsLock;// never mind real CapsLock status, heh???(GetKeyState(VK_CAPITAL) & 1);
//    printf("g_bCapsLock=%d\n", g_bCapsLock);
//    FrameRefreshStatus(DRAW_LEDS);
  }
}

//===========================================================================

DWORD KeybGetSnapshot(SS_IO_Keyboard* pSS)
{
  pSS->keyboardqueries  = keyboardqueries;
  pSS->nLastKey      = g_nLastKey;
  return 0;
}

DWORD KeybSetSnapshot(SS_IO_Keyboard* pSS)
{
  keyboardqueries  = pSS->keyboardqueries;
  g_nLastKey    = pSS->nLastKey;
  return 0;
}
