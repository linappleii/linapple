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

/* Description: Frame
 *
 * Author: Various
 */

/* Adaptation for SDL and POSIX (l) by beom beotiger, Nov-Dec 2007 */

/* And KREZ */

// for usleep
#include <unistd.h>

#include <SDL_image.h>

#include "stdafx.h"

#include "asset.h"
//#pragma  hdrstop
#include "MouseInterface.h"
//#include "..\resource\resource.h"

// for stat in FrameSaveBMP function
#include <sys/stat.h>

#include <iostream>

#include "../res/icon.xpm"

#define ENABLE_MENU 0

SDL_Surface     *apple_icon;	// icon

SDL_Surface * screen;  // our main screen
// rects for screen stretch if needed
SDL_Rect origRect;
SDL_Rect newRect;
///////////////////////////////////////////
#define  VIEWPORTCX  560
#if ENABLE_MENU
#define  VIEWPORTCY  400
#else
#define  VIEWPORTCY  384
#endif
#define  BUTTONX     (VIEWPORTCX+(VIEWPORTX<<1))
#define  BUTTONY     0
#define  BUTTONCX    45
#define  BUTTONCY    45
#define  FSVIEWPORTX (640-BUTTONCX-VIEWPORTX-VIEWPORTCX)
#define  FSVIEWPORTY ((480-VIEWPORTCY)>>1)
#define  FSBUTTONX   (640-BUTTONCX)
#define  FSBUTTONY   (((480-VIEWPORTCY)>>1)-1)
#define  BUTTONS     8


/*static HBITMAP capsbitmap[2];
  static HBITMAP diskbitmap[ NUM_DISK_STATUS ];

  static HBITMAP buttonbitmap[BUTTONS];*/

//static BOOL    active          = 0;
static bool    g_bAppActive = false;

/*static HBRUSH  btnfacebrush    = (HBRUSH)0;
  static HPEN    btnfacepen      = (HPEN)0;
  static HPEN    btnhighlightpen = (HPEN)0;
  static HPEN    btnshadowpen    = (HPEN)0;*/

//static int     buttonactive    = -1;

static int     buttondown      = -1;


//static int     buttonover      = -1;


//static int     buttonx         = BUTTONX;
//static int     buttony         = BUTTONY;

/*static HRGN    clipregion      = (HRGN)0;
  HDC     g_hFrameDC         = (HDC)0;
  static RECT    framerect       = {0,0,0,0};
  HWND    g_hFrameWindow     = (HWND)0;*/
BOOL    fullscreen      = 0;
BOOL  g_WindowResized;  // if we have not normal window size

//static BOOL    helpquit        = 0;

// static BOOL    painting        = 0;
// static HFONT   smallfont       = (HFONT)0;
// static HWND    tooltipwindow   = (HWND)0;


static BOOL    usingcursor     = 0;
//static int     viewportx       = VIEWPORTX;
//static int     viewporty       = VIEWPORTY;

// Hmmm. I love DirectDraw(tm). But SDL is better???????? for Linux, at least. Ha-ha-ha --bb
// static LPDIRECTDRAW        directdraw = (LPDIRECTDRAW)0;
// static LPDIRECTDRAWSURFACE surface    = (LPDIRECTDRAWSURFACE)0;

void    DrawStatusArea (/*HDC passdc,*/ BOOL drawflags);
void    ProcessButtonClick (int button, int mod); // handle control buttons(F1-..F12) events

//void  ProcessDiskPopupMenu(HWND hwnd, POINT pt, const int iDrive);
//void    RelayEvent (UINT message, WPARAM wparam, LPARAM lparam);

void    ResetMachineState ();
void    SetFullScreenMode ();
void    SetNormalMode ();
void    SetUsingCursor (BOOL);

bool  g_bScrollLock_FullSpeed = false;  // no in full speed!

//===========================================================================
/*
   void CreateGdiObjects () {
   ZeroMemory(buttonbitmap,BUTTONS*sizeof(HBITMAP));
#define LOADBUTTONBITMAP(bitmapname)  LoadImage(g_hInstance,bitmapname,   \
IMAGE_BITMAP,0,0,      \
LR_CREATEDIBSECTION |  \
LR_LOADMAP3DCOLORS |   \
LR_LOADTRANSPARENT);
buttonbitmap[BTN_HELP   ] = (HBITMAP)LOADBUTTONBITMAP(TEXT("HELP_BUTTON"));
buttonbitmap[BTN_RUN    ] = (HBITMAP)LOADBUTTONBITMAP(TEXT("RUN_BUTTON"));
buttonbitmap[BTN_DRIVE1 ] = (HBITMAP)LOADBUTTONBITMAP(TEXT("DRIVE1_BUTTON"));
buttonbitmap[BTN_DRIVE2 ] = (HBITMAP)LOADBUTTONBITMAP(TEXT("DRIVE2_BUTTON"));
buttonbitmap[BTN_DRIVESWAP] = (HBITMAP)LOADBUTTONBITMAP(TEXT("DRIVESWAP_BUTTON"));
buttonbitmap[BTN_FULLSCR] = (HBITMAP)LOADBUTTONBITMAP(TEXT("FULLSCR_BUTTON"));
buttonbitmap[BTN_DEBUG  ] = (HBITMAP)LOADBUTTONBITMAP(TEXT("DEBUG_BUTTON"));
buttonbitmap[BTN_SETUP  ] = (HBITMAP)LOADBUTTONBITMAP(TEXT("SETUP_BUTTON"));
capsbitmap[0] = (HBITMAP)LOADBUTTONBITMAP(TEXT("CAPSOFF_BITMAP"));
capsbitmap[1] = (HBITMAP)LOADBUTTONBITMAP(TEXT("CAPSON_BITMAP"));

diskbitmap[ DISK_STATUS_OFF  ] = (HBITMAP)LOADBUTTONBITMAP(TEXT("DISKOFF_BITMAP"));
diskbitmap[ DISK_STATUS_READ ] = (HBITMAP)LOADBUTTONBITMAP(TEXT("DISKREAD_BITMAP"));
diskbitmap[ DISK_STATUS_WRITE] = (HBITMAP)LOADBUTTONBITMAP(TEXT("DISKWRITE_BITMAP"));
diskbitmap[ DISK_STATUS_PROT ] = (HBITMAP)LOADBUTTONBITMAP(TEXT("DISKPROT_BITMAP"));

btnfacebrush    = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
btnfacepen      = CreatePen(PS_SOLID,1,GetSysColor(COLOR_BTNFACE));
btnhighlightpen = CreatePen(PS_SOLID,1,GetSysColor(COLOR_BTNHIGHLIGHT));
btnshadowpen    = CreatePen(PS_SOLID,1,GetSysColor(COLOR_BTNSHADOW));
smallfont = CreateFont(11,6,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,
OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
DEFAULT_QUALITY,VARIABLE_PITCH | FF_SWISS,
TEXT("Small Fonts"));
}


//===========================================================================
void DeleteGdiObjects () {
int loop;
for (loop = 0; loop < BUTTONS; loop++)
DeleteObject(buttonbitmap[loop]);
for (loop = 0; loop < 2; loop++)
DeleteObject(capsbitmap[loop]);
for (loop = 0; loop < NUM_DISK_STATUS; loop++)
DeleteObject(diskbitmap[loop]);
DeleteObject(btnfacebrush);
DeleteObject(btnfacepen);
DeleteObject(btnhighlightpen);
DeleteObject(btnshadowpen);
DeleteObject(smallfont);
}

// Draws an 3D box around the main apple screen
//===========================================================================
void Draw3dRect (HDC dc, int x1, int y1, int x2, int y2, BOOL out)
{
SelectObject(dc,GetStockObject(NULL_BRUSH));
SelectObject(dc,out ? btnshadowpen : btnhighlightpen);
POINT pt[3];
pt[0].x = x1;    pt[0].y = y2-1;
pt[1].x = x2-1;  pt[1].y = y2-1;
pt[2].x = x2-1;  pt[2].y = y1;
Polyline(dc,(LPPOINT)&pt,3);
SelectObject(dc,(out == 1) ? btnhighlightpen : btnshadowpen);
pt[1].x = x1;    pt[1].y = y1;
pt[2].x = x2;    pt[2].y = y1;
Polyline(dc,(LPPOINT)&pt,3);
}

//===========================================================================
void DrawBitmapRect (HDC dc, int x, int y, LPRECT rect, HBITMAP bitmap) {
HDC memdc = CreateCompatibleDC(dc);
SelectObject(memdc,bitmap);
BitBlt(dc,x,y,
    rect->right  + 1 - rect->left,
    rect->bottom + 1 - rect->top,
    memdc,
    rect->left,
    rect->top,
    SRCCOPY);
DeleteDC(memdc);
}

//===========================================================================
void DrawButton (HDC passdc, int number) {
  FrameReleaseDC();
  HDC dc = (passdc ? passdc : GetDC(g_hFrameWindow));
  int x  = buttonx;
  int y  = buttony+number*BUTTONCY;
  if (number == buttondown) {
    int loop = 0;
    while (loop++ < 3)
      Draw3dRect(dc,x+loop,y+loop,x+BUTTONCX,y+BUTTONCY,0);
    RECT rect = {0,0,39,39};
    DrawBitmapRect(dc,x+4,y+4,&rect,buttonbitmap[number]);
  }
  else {
    Draw3dRect(dc,x+1,y+1,x+BUTTONCX,y+BUTTONCY,1);
    Draw3dRect(dc,x+2,y+2,x+BUTTONCX-1,y+BUTTONCY-1,1);
    RECT rect = {1,1,40,40};
    DrawBitmapRect(dc,x+3,y+3,&rect,buttonbitmap[number]);
  }
  if ((number == BTN_DRIVE1) || (number == BTN_DRIVE2)) {
    int  offset = (number == buttondown) << 1;
    RECT rect = {x+offset+3,
      y+offset+31,
      x+offset+42,
      y+offset+42};
    SelectObject(dc,smallfont);
    SetTextColor(dc,RGB(0,0,0));
    SetTextAlign(dc,TA_CENTER | TA_TOP);
    SetBkMode(dc,TRANSPARENT);
    ExtTextOut(dc,x+offset+22,rect.top,ETO_CLIPPED,&rect,
        DiskGetName(number-BTN_DRIVE1),
        MIN(8,_tcslen(DiskGetName(number-BTN_DRIVE1))),
        NULL);
  }
  if (!passdc)
    ReleaseDC(g_hFrameWindow,dc);
}

//===========================================================================
void DrawCrosshairs (int x, int y) {
  static int lastx = 0;
  static int lasty = 0;
  FrameReleaseDC();
  HDC dc = GetDC(g_hFrameWindow);
#define LINE(x1,y1,x2,y2) MoveToEx(dc,x1,y1,NULL); LineTo(dc,x2,y2);

  // ERASE THE OLD CROSSHAIRS
  if (lastx && lasty)
    if (fullscreen) {
      int loop = 4;
      while (loop--) {
        RECT rect = {0,0,5,5};
        switch (loop) {
          case 0: OffsetRect(&rect,lastx-2,FSVIEWPORTY-5);           break;
          case 1: OffsetRect(&rect,lastx-2,FSVIEWPORTY+VIEWPORTCY);  break;
          case 2: OffsetRect(&rect,FSVIEWPORTX-5,         lasty-2);  break;
          case 3: OffsetRect(&rect,FSVIEWPORTX+VIEWPORTCX,lasty-2);  break;
        }
        FillRect(dc,&rect,(HBRUSH)GetStockObject(BLACK_BRUSH));
      }
    }
    else {
      int loop = 5;
      while (loop--) {
        switch (loop) {
          case 0: SelectObject(dc,GetStockObject(BLACK_PEN));  break;
          case 1: // fall through
          case 2: SelectObject(dc,btnshadowpen);               break;
          case 3: // fall through
          case 4: SelectObject(dc,btnfacepen);                 break;
        }
        LINE(lastx-2,VIEWPORTY-loop-1,
            lastx+3,VIEWPORTY-loop-1);
        LINE(VIEWPORTX-loop-1,lasty-2,
            VIEWPORTX-loop-1,lasty+3);
        if ((loop == 1) || (loop == 2))
          SelectObject(dc,btnhighlightpen);
        LINE(lastx-2,VIEWPORTY+VIEWPORTCY+loop,
            lastx+3,VIEWPORTY+VIEWPORTCY+loop);
        LINE(VIEWPORTX+VIEWPORTCX+loop,lasty-2,
            VIEWPORTX+VIEWPORTCX+loop,lasty+3);
      }
    }

  // DRAW THE NEW CROSSHAIRS
  if (x && y) {
    int loop = 4;
    while (loop--) {
      if ((loop == 1) || (loop == 2))
        SelectObject(dc,GetStockObject(WHITE_PEN));
      else
        SelectObject(dc,GetStockObject(BLACK_PEN));
      LINE(x+loop-2,viewporty-5,
          x+loop-2,viewporty);
      LINE(x+loop-2,viewporty+VIEWPORTCY+4,
          x+loop-2,viewporty+VIEWPORTCY-1);
      LINE(viewportx-5,           y+loop-2,
          viewportx,             y+loop-2);
      LINE(viewportx+VIEWPORTCX+4,y+loop-2,
          viewportx+VIEWPORTCX-1,y+loop-2);
    }
  }
#undef LINE
  lastx = x;
  lasty = y;
  ReleaseDC(g_hFrameWindow,dc);
}
*/

//===========================================================================
void DrawFrameWindow () {
  VideoRealizePalette(/*dc*/);
  //  printf("In DrawFrameWindow. g_nAppMode == %d\n", g_nAppMode);

  // DRAW THE STATUS AREA
  DrawStatusArea(DRAW_BACKGROUND | DRAW_LEDS);

  // DRAW THE CONTENTS OF THE EMULATED SCREEN
  if (g_nAppMode == MODE_LOGO)
    VideoDisplayLogo(); // logo
  else if (g_nAppMode == MODE_DEBUG)
    DebugDisplay(1);  //debugger
  else
    VideoRedrawScreen(); // normal state - running emulator?
  //  printf("Out of DrawFrameWindow!\n");
}

//===========================================================================
void DrawStatusArea (/*HDC passdc,*/ int drawflags)
{// status area not used now (yet?) --bb
  /*  FrameReleaseDC();
      HDC  dc     = (passdc ? passdc : GetDC(g_hFrameWindow));
      int  x      = buttonx;
      int  y      = buttony+BUTTONS*BUTTONCY+1;
      int  iDrive1Status = DISK_STATUS_OFF;
      int  iDrive2Status = DISK_STATUS_OFF;
      bool bCaps   = KeybGetCapsStatus();
      DiskGetLightStatus(&iDrive1Status,&iDrive2Status);

      if (fullscreen)
      {
      SelectObject(dc,smallfont);
      SetBkMode(dc,OPAQUE);
      SetBkColor(dc,RGB(0,0,0));
      SetTextAlign(dc,TA_LEFT | TA_TOP);
      SetTextColor(dc,RGB((iDrive1Status==2 ? 255 : 0),(iDrive1Status==1 ? 255 : 0),0));
      TextOut(dc,x+ 3,y+2,TEXT("1"),1);
      SetTextColor(dc,RGB((iDrive2Status==2 ? 255 : 0),(iDrive2Status==1 ? 255 : 0),0));
      TextOut(dc,x+13,y+2,TEXT("2"),1);
      if (!IS_APPLE2)
      {
      SetTextAlign(dc,TA_RIGHT | TA_TOP);
      SetTextColor(dc,(bCaps
      ? RGB(128,128,128)
      : RGB(  0,  0,  0) ));
      TextOut(dc,x+BUTTONCX,y+2,TEXT("Caps"),4);
      }
      SetTextAlign(dc,TA_CENTER | TA_TOP);
      SetTextColor(dc,(g_nAppMode == MODE_PAUSED || g_nAppMode == MODE_STEPPING
      ? RGB(255,255,255)
      : RGB(  0,  0,  0)));
      TextOut(dc,x+BUTTONCX/2,y+13,(g_nAppMode == MODE_PAUSED
      ? TITLE_PAUSED
      : TITLE_STEPPING) ,8);
      }
      else
      {*/
  if(font_sfc == NULL)
    if(!fonts_initialization()) {
      fprintf(stderr, "Font file was not loaded.\n");
      return;    //if we don't have a fonts, we just can do none
    }

  SDL_Rect srect;

  Uint32 mybluez = SDL_MapRGB(screen->format, 10, 10, 255);  // bluez color, know that?

  SDL_SetColors(g_hStatusSurface, screen->format->palette->colors,
      0, 256);
  //  Uint32 myblack  = SDL_MapRGB(screen->format, 0, 0, 0);  // black color
  //  SDL_SetColorKey(g_hStatusSurface,SDL_SRCCOLORKEY/* | SDL_RLEACCEL*/, myblack);

  if (drawflags & DRAW_BACKGROUND)
  {
    /* Code moved to Video.cpp in CreateDIBSections()
       srect.x = srect.y = 0;
       srect.w = STATUS_PANEL_W;
       srect.h = STATUS_PANEL_H;
       SDL_FillRect(g_hStatusSurface, &srect, mybluez);  // fill status panel
       rectangle(g_hStatusSurface, 0, 0, STATUS_PANEL_W - 1, STATUS_PANEL_H - 1, myyell);
       rectangle(g_hStatusSurface, 2, 2, STATUS_PANEL_W - 5, STATUS_PANEL_H - 5, myyell);
       font_print(7, 6, "FDD1", g_hStatusSurface, 1.3, 1.5); // show signs
       font_print(40, 6, "FDD2", g_hStatusSurface, 1.3, 1.5);
       font_print(74, 6, "HDD", g_hStatusSurface, 1.3, 1.5);
       */
    //    SDL_SetColors(g_hStatusSurface, screen->format->palette->colors,
    //               0, 256);
    g_iStatusCycle = SHOW_CYCLES;  // start cycle for panel showing
  }
  if (drawflags & DRAW_LEDS)
  {
    srect.x = 4;
    srect.y = 22;
    srect.w = STATUS_PANEL_W - 8;
    srect.h = STATUS_PANEL_H - 25;
    SDL_FillRect(g_hStatusSurface, &srect, mybluez);  // clear

    char leds[2] = "\x64";
#define LEDS  1
    int  iDrive1Status = DISK_STATUS_OFF;
    int  iDrive2Status = DISK_STATUS_OFF;
    int iHDDStatus = DISK_STATUS_OFF;

    //    bool bCaps   = KeybGetCapsStatus();
    DiskGetLightStatus(&iDrive1Status,&iDrive2Status);
    iHDDStatus = HD_GetStatus();

    leds[0] = LEDS + iDrive1Status;
    //    printf("Leds are %d\n",leds[0]);
    font_print(8, 23, leds, g_hStatusSurface, 4, 2.7);

    leds[0] = LEDS + iDrive2Status;
    font_print(40, 23, leds, g_hStatusSurface, 4, 2.7);

    leds[0] = LEDS + iHDDStatus;
    font_print(71, 23, leds, g_hStatusSurface, 4, 2.7);

    if(iDrive1Status | iDrive2Status | iHDDStatus) g_iStatusCycle = SHOW_CYCLES; // show status panel
  }
  //  surface_fader(g_hStatusSurface, nowleds, nowleds, nowleds, -1, 0);
  /*    if (drawflags & DRAW_TITLE)
        {
        TCHAR title[40];
        switch (g_Apple2Type)
        {
        case A2TYPE_APPLE2:      _tcscpy(title, TITLE_APPLE_2); break;
        case A2TYPE_APPLE2PLUS:    _tcscpy(title, TITLE_APPLE_2_PLUS); break;
        case A2TYPE_APPLE2E:    _tcscpy(title, TITLE_APPLE_2E); break;
        case A2TYPE_APPLE2EEHANCED:  _tcscpy(title, TITLE_APPLE_2E_ENHANCED); break;
        }

        switch (g_nAppMode)
        {
        case MODE_PAUSED  : _tcscat(title,TEXT(" [")); _tcscat(title,TITLE_PAUSED  ); _tcscat(title,TEXT("]")); break;
        case MODE_STEPPING: _tcscat(title,TEXT(" [")); _tcscat(title,TITLE_STEPPING); _tcscat(title,TEXT("]")); break;
        }

        SendMessage(g_hFrameWindow,WM_SETTEXT,0,(LPARAM)title);
        }
        if (drawflags & DRAW_BUTTON_DRIVES)
        {
        DrawButton(dc, BTN_DRIVE1);
        DrawButton(dc, BTN_DRIVE2);
        }
        }

        if (!passdc)
        ReleaseDC(g_hFrameWindow,dc);*/
}



/*
//===========================================================================
void EraseButton (int number) {
RECT rect;
rect.left   = buttonx;
rect.right  = rect.left+BUTTONCX;
rect.top    = buttony+number*BUTTONCY;
rect.bottom = rect.top+BUTTONCY;
InvalidateRect(g_hFrameWindow,&rect,1);
}
*/

void FrameShowHelpScreen(int sx, int sy) // sx, sy - sizes of current window (screen)
{
  // on pressing F1 button shows help screen

  const char * HelpStrings[] = {
    "Welcome to LinApple - Apple][ emulator for Linux!",
    "Conf file is linapple.conf in current directory by default",
    "Hugest archive of Apple][ stuff you can find at ftp.apple.asimov.net",
    " F1 - This help",
    " Ctrl+F2 - Cold reset",
    " Shift+F2 - Reload conf file and restart",
    " F3, F4 - Choose an image file name for floppy disk",
    "             in Slot 6 drive 1 or 2 respectively",
    " Shift+F3, Shift+F4 - The same thing for Apple hard disks",
    "                         (in Slot 7)",
    " F5 - Swap drives for Slot 6",
    " F6 - Toggle fullscreen mode",
    " F7 - Reserved for Debugger!",
    " F8 - Save current screen as a .bmp file",
    " Shift+F8 - Save settings changable at runtime in conf file",
    " F9 - Cycle through various video modes",
    " F10 - Quit emulator",
    " F11 - Save current state to file, Alt+F11 - quick save",
    " F12 - Reload it from file, Alt+F12 - quick load",
    " Ctrl+F12 - Hot reset",
    "  Pause - Pause emulator",
    "  Scroll Lock - Toggle full speed",
    "Num pad keys:",
    "  Grey + - Speed up emulator",
    "  Grey - - Speed it down",
    "  Grey * - Normal speed"
  };

  //   const int PositionsY[] = { 7, 15, 26 };

  SDL_Surface *my_screen;  // for background
  SDL_Surface *tempSurface = NULL;  // temporary surface

  if(font_sfc == NULL)
    if(!fonts_initialization()) {
      fprintf(stderr, "Font file was not loaded.\n");
      return;    //if we don't have a fonts, we just can do none
    }
  if(!g_WindowResized) {
    if(g_nAppMode == MODE_LOGO) tempSurface = g_hLogoBitmap;  // use logobitmap
    else tempSurface = g_hDeviceBitmap;
  }
  else tempSurface = g_origscreen;

  if(tempSurface == NULL) tempSurface = screen;  // use screen, if none available
  my_screen = SDL_CreateRGBSurface(SDL_SWSURFACE, tempSurface->w, tempSurface->h,
      tempSurface->format->BitsPerPixel, 0, 0, 0, 0);
  if(tempSurface->format->palette && my_screen->format->palette)
    SDL_SetColors(my_screen, tempSurface->format->palette->colors,
        0, tempSurface->format->palette->ncolors);

  surface_fader(my_screen, 0.2F, 0.2F, 0.2F, -1, 0);  // fade it out to 20% of normal
  SDL_BlitSurface(tempSurface, NULL, my_screen, NULL);

  SDL_BlitSurface(my_screen, NULL, screen, NULL);    // show background

  double facx = double(g_ScreenWidth) / double(SCREEN_WIDTH);
  double facy = double(g_ScreenHeight) / double(SCREEN_HEIGHT);

  font_print_centered(sx/2, int(5*facy), (char*)HelpStrings[0], screen, 1.5*facx, 1.3*facy);
  font_print_centered(sx/2, int(20*facy), (char*)HelpStrings[1], screen, 1.3*facx, 1.2*facy);
  font_print_centered(sx/2, int(30*facy), (char*)HelpStrings[2], screen, 1.2*facx, 1.0*facy);

  int Help_TopX = int(45*facy);
  int i;
  for(i =  3; i < 25; i++)
    font_print(4, Help_TopX + (i - 3) * 15 * facy, (char*)HelpStrings[i], screen, 1.5*facx, 1.5*facy); // show keys

  // show frames
  rectangle(screen, 0, Help_TopX - 5, /*SCREEN_WIDTH*/g_ScreenWidth - 1, int(335*facy), SDL_MapRGB(screen->format, 255, 255, 255));
  rectangle(screen, 1, Help_TopX - 4, /*SCREEN_WIDTH*/g_ScreenWidth, int(335*facy), SDL_MapRGB(screen->format, 255, 255, 255));

  rectangle(screen, 1, 1, /*SCREEN_WIDTH*/g_ScreenWidth - 2, (Help_TopX - 8), SDL_MapRGB(screen->format, 255, 255, 0));

  tempSurface = SDL_DisplayFormat(IMG_ReadXPMFromArray(icon_xpm));
  SDL_Rect logo, scrr;
  logo.x = logo.y = 0;
  logo.w = tempSurface->w;
  logo.h = tempSurface->h;
  scrr.x = int(460*facx);
  scrr.y = int(270*facy);
  scrr.w = scrr.h = int(100*facy);
  SDL_SoftStretchOr(tempSurface, &logo, screen, &scrr);

  SDL_Flip(screen);  // show the screen
  SDL_Delay(1000);  // wait 1 second to be not too fast

  //////////////////////////////////
  // Wait for keypress
  //////////////////////////////////
  SDL_Event event;  // event

  event.type = SDL_QUIT;
  while(event.type != SDL_KEYDOWN /*&& event.key.keysym.sym != SDLK_ESCAPE*/) {// wait for ESC-key pressed
    usleep(100);
    SDL_PollEvent(&event);
  }

  DrawFrameWindow(); // restore screen
}


void FrameQuickState(int num, int mod)
{
  // quick load or save state with number num,
  // if Shift is pressed, state is being saved,
  // otherwise - being loaded
  char fpath[MAX_PATH];
  snprintf(fpath, MAX_PATH, "%s/SaveState%d.aws", g_sSaveStateDir, num); // prepare file name
  Snapshot_SetFilename(fpath);  // set it as a working name
  if(mod & KMOD_SHIFT)  Snapshot_SaveState();
  else    Snapshot_LoadState();
}




//===========================================================================
/*LRESULT CALLBACK FrameWndProc (
  HWND   window,
  UINT   message,
  WPARAM wparam,
  LPARAM lparam)*/
void  FrameDispatchMessage(SDL_Event * e) // process given SDL event
{
  int mysym = e->key.keysym.sym; // keycode
  int mymod = e->key.keysym.mod; // some special keys flags
  int x,y;  // used for mouse cursor position

  switch (e->type) //type of SDL event
  {
    case SDL_VIDEORESIZE:
      printf("OLD DIMENSIONS: %d  %d\n", g_ScreenWidth, g_ScreenHeight);
      g_ScreenWidth = e->resize.w;
      g_ScreenHeight = (e->resize.h / 96) * 96;
      if( g_ScreenHeight < 192 ) {
        g_ScreenHeight = 192;
      }
      //Resize the screen
      screen = SDL_SetVideoMode( e->resize.w, e->resize.h, SCREEN_BPP, SDL_SWSURFACE | SDL_HWPALETTE | SDL_RESIZABLE );
      if( screen == NULL ) {
        SDL_Quit();
        return;
      } else {
        // define if we have resized window
        g_WindowResized = (g_ScreenWidth != SCREEN_WIDTH) | (g_ScreenHeight != SCREEN_HEIGHT);
        printf("Screen size is %dx%d\n",g_ScreenWidth, g_ScreenHeight);
        if(g_WindowResized) {
          // create rects for screen stretching
          origRect.x = origRect.y = newRect.x = newRect.y = 0;
          origRect.w = SCREEN_WIDTH;
          origRect.h = SCREEN_HEIGHT;
          newRect.w = g_ScreenWidth;
          newRect.h = g_ScreenHeight;
          if ((g_nAppMode != MODE_LOGO) && (g_nAppMode != MODE_DEBUG))
            VideoRedrawScreen();
        }
      }
      break;

    case SDL_ACTIVEEVENT:
      g_bAppActive = e->active.gain; // if gain==1, app is active
      break;

    case SDL_KEYDOWN:
      //      printf("keyb %d is down!\n", mysym);
      if(mysym >= SDLK_0 && mysym <= SDLK_9 && mymod & KMOD_CTRL) {
        FrameQuickState(mysym - SDLK_0, mymod);
        break;
      }

      if(mysym < 128 && mysym != SDLK_PAUSE) { // it should be ASCII code?
        if ((g_nAppMode == MODE_RUNNING) || (g_nAppMode == MODE_LOGO) ||
            ((g_nAppMode == MODE_STEPPING) && (mysym != SDLK_ESCAPE)))
        {
          KeybQueueKeypress(mysym,ASCII);
        }
        else
          if ((g_nAppMode == MODE_DEBUG) || (g_nAppMode == MODE_STEPPING))
          {
            DebuggerInputConsoleChar(mysym);
          }
        break;
      }
      else {// this is function key?
        //    KeybUpdateCtrlShiftStatus(); // if ctrl or shift or alt was pressed?------?
        if ((mysym >= SDLK_F1) && (mysym <= SDLK_F12) && (buttondown == -1))
        {
          SetUsingCursor(0);  //-- for what purpose???
          buttondown = mysym - SDLK_F1;  // special function keys processing

          /*      if (fullscreen && (buttonover != -1)) {
                  if (buttonover != buttondown)
                  EraseButton(buttonover);
                  buttonover = -1;
                  }
                  DrawButton((HDC)0,buttondown);*/
        }
        else if (mysym == SDLK_KP_PLUS) // Gray + - speed up the emulator!
        {
          g_dwSpeed = g_dwSpeed + 2;
          if(g_dwSpeed > SPEED_MAX) g_dwSpeed = SPEED_MAX; // no Maximum tresspassing!
          printf("Now speed=%d\n", (int)g_dwSpeed);
          SetCurrentCLK6502();
        }
        else if (mysym == SDLK_KP_MINUS) // Gray + - speed up the emulator!
        {
          if(g_dwSpeed > SPEED_MIN) g_dwSpeed = g_dwSpeed - 1;// dw is unsigned value!
          //if(g_dwSpeed <= SPEED_MIN) g_dwSpeed = SPEED_MIN; // no Minimum tresspassing!
          printf("Now speed=%d\n", (int)g_dwSpeed);
          SetCurrentCLK6502();
        }
        else if (mysym == SDLK_KP_MULTIPLY) // Gray * - normal speed!
        {
          g_dwSpeed = 10;// dw is unsigned value!
          printf("Now speed=%d\n", (int)g_dwSpeed);
          SetCurrentCLK6502();
        }


        else if (mysym == SDLK_CAPSLOCK) // CapsLock
        {
          KeybToggleCapsLock();
        }
        else if (mysym == SDLK_PAUSE)  // Pause - let us pause all things for the best
        {
          SetUsingCursor(0); // release cursor?
          switch (g_nAppMode)
          {
            case MODE_RUNNING: // go in pause
              g_nAppMode = MODE_PAUSED;
              SoundCore_SetFade(FADE_OUT); // fade out sound?**************
              break;
            case MODE_PAUSED: // go to the normal mode?
              g_nAppMode = MODE_RUNNING;
              SoundCore_SetFade(FADE_IN);  // fade in sound?***************
              break;
            case MODE_STEPPING:
              DebuggerInputConsoleChar( DEBUG_EXIT_KEY );
              break;
            case MODE_LOGO:
            case MODE_DEBUG:
            default:
              break;
          }
          DrawStatusArea(/*(HDC)0,*/DRAW_TITLE);
          if ((g_nAppMode != MODE_LOGO) && (g_nAppMode != MODE_DEBUG))
            VideoRedrawScreen();
          g_bResetTiming = true;
        }
        else if (mysym == SDLK_SCROLLOCK)  // SCROLL LOCK pressed
        {
          g_bScrollLock_FullSpeed = !g_bScrollLock_FullSpeed; // turn on/off full speed?
        }
        else if ((g_nAppMode == MODE_RUNNING) || (g_nAppMode == MODE_LOGO) || (g_nAppMode == MODE_STEPPING))
        {
          // Note about Alt Gr (Right-Alt):
          // . WM_KEYDOWN[Left-Control], then:
          // . WM_KEYDOWN[Right-Alt]
          BOOL autorep  = 0; //previous key was pressed? 30bit of lparam
          BOOL extended = (mysym >= 273); // 24bit of lparam - is an extended key, what is it???
          if ((!JoyProcessKey(mysym ,extended, 1, autorep)) && (g_nAppMode != MODE_LOGO))
            KeybQueueKeypress(mysym, NOT_ASCII);
        }
        else if (g_nAppMode == MODE_DEBUG)
          DebuggerProcessKey(mysym);  // someone should realize debugger for Linapple!?--bb
        /*
           if (wparam == VK_F10)
           {
           SetUsingCursor(0);
           return 0;
           }
           break;*/
      }//else
      break;

    case SDL_KEYUP:
      //  int mysym = e->key.keysym.sym; // keycode
      if ((mysym >= SDLK_F1) && (mysym <= SDLK_F12) && (buttondown == mysym-SDLK_F1))
      {
        buttondown = -1;
        //       if (fullscreen)
        //         EraseButton(wparam-VK_F1);
        //       else
        //         DrawButton((HDC)0,wparam-VK_F1);
        ProcessButtonClick(mysym-SDLK_F1, mymod); // process function keys - special events
      } else if (mysym == SDLK_CAPSLOCK) {
        // GPH Fix caps lock toggle behavior.
        // (http://sdl.beuc.net/sdl.wiki/SDL_KeyboardEvent)
        KeybToggleCapsLock();
      } else {  // mysym >= 300 (or 273????)- check for extended key, what is it EXACTLY???
        JoyProcessKey(mysym,(mysym >= 273), 0, 0);
      }
      break;

    case SDL_MOUSEBUTTONDOWN:
      if(e->button.button == SDL_BUTTON_LEFT) {// left mouse button was pressed
        if (buttondown == -1)
        {
          x = e->button.x; // mouse cursor coordinates
          y = e->button.y;
          if (usingcursor) // we use mouse cursor for our special needs?
          {
            KeybUpdateCtrlShiftStatus(); // if either of ALT, SHIFT or CTRL is pressed
            if (g_bShiftKey | g_bCtrlKey)
            {
              SetUsingCursor(0); // release mouse cursor for user
            }
            else
            {
              if (sg_Mouse.Active())
                sg_Mouse.SetButton(BUTTON0, BUTTON_DOWN);
              else
                JoySetButton(BUTTON0, BUTTON_DOWN);
            }
          }// we do not use mouse
          else if ( (/*(x < buttonx) && JoyUsingMouse() && */((g_nAppMode == MODE_RUNNING) ||
                  (g_nAppMode == MODE_STEPPING))) || (sg_Mouse.Active()) )
          {
            SetUsingCursor(1); // capture cursor
          }
          DebuggerMouseClick( x, y );
        }
        //RelayEvent(WM_LBUTTONDOWN,wparam,lparam);
      }//if left mouse button down
      else if(e->button.button == SDL_BUTTON_RIGHT) {
        if (usingcursor)
        {
          if (sg_Mouse.Active())
            sg_Mouse.SetButton(BUTTON1, BUTTON_DOWN);
          else
            JoySetButton(BUTTON1, BUTTON_DOWN);
        }
      }

      break; // end of MOSEBUTTONDOWN event

    case SDL_MOUSEBUTTONUP:
      if (e->button.button == SDL_BUTTON_LEFT) {// left mouse button was released
        if (usingcursor)
        {
          if (sg_Mouse.Active())
            sg_Mouse.SetButton(BUTTON0, BUTTON_UP);
          else
            JoySetButton(BUTTON0, BUTTON_UP);
        }
        //      RelayEvent(WM_LBUTTONUP,wparam,lparam);
      }
      else if(e->button.button == SDL_BUTTON_RIGHT) {
        if (usingcursor)
        {
          if (sg_Mouse.Active())
            sg_Mouse.SetButton(BUTTON1, BUTTON_UP);
          else
            JoySetButton(BUTTON1, BUTTON_UP);
        }
      }
      break; // MOUSEBUTTONUP event

    case SDL_MOUSEMOTION:
      x = e->motion.x;// get relative coordinates of mouse cursor
      y = e->motion.y;
      if (usingcursor)
      {
        //        DrawCrosshairs(x,y); I do not like those crosshairs, but... --bb
        if (sg_Mouse.Active())
          sg_Mouse.SetPosition(x, VIEWPORTCX-4, y, VIEWPORTCY-4);
        else
          JoySetPosition(x, VIEWPORTCX-4, y, VIEWPORTCY-4);
      }
      //      RelayEvent(WM_MOUSEMOVE,wparam,lparam);
      break;

    case SDL_USEREVENT:
      if (e->user.code == 1) // should do restart?
        ProcessButtonClick(BTN_RUN, KMOD_LCTRL);
      break;

  }//switch

  //  return DefWindowProc(window,message,wparam,lparam);
}


bool PSP_SaveStateSelectImage(bool saveit)
{
  // Dialog for save or load StateImage
  // if saveit == TRUE, then pick image for saving
  //  else pick an image for loading
  static int findex = 0;    // file index will be remembered for current dir
  static int backdx = 0;  //reserve
  static int dirdx  = 0;  // reserve for dirs

  char * filename = NULL;      // given filename
  char fullpath[MAX_PATH];  // full path for it
  char tmppath [MAX_PATH];
  bool isdir;      // if given filename is a directory?

  findex = backdx;
  isdir = true;
  strcpy(fullpath, g_sSaveStateDir);  // global var for disk selecting directory

  while(isdir)
  {
    if(!ChooseAnImage(/*SCREEN_WIDTH*/g_ScreenWidth, /*SCREEN_HEIGHT*/g_ScreenHeight, fullpath, saveit, &filename, &isdir, &findex)) {
      DrawFrameWindow();
      return false;  // if ESC was pressed, just leave
    }
    //   strcpy(filename, pszFilename);
    //    printf("We got next:\n");
    //    printf("isdir=%d, findex=%d, filename=%s\n", isdir, findex, filename);
    if(isdir)
    {

      if(!strcmp(filename, ".."))  // go to the upper directory
      {
        filename = strrchr(fullpath, FILE_SEPARATOR); // look for last '/'
        if(filename) *filename = '\0';  // cut it off
        if(strlen(fullpath) == 0) strcpy(fullpath,"/");  //we don't want fullpath to be empty
        findex = dirdx;  // restore

      }
      else
      {
        if(strcmp(fullpath, "/")) snprintf(tmppath, MAX_PATH, "%s/%s", fullpath, filename); // next dir
        else snprintf(tmppath, MAX_PATH, "/%s", filename);
        strcpy(fullpath, tmppath);  // got ot anew
        //        printf("We build %s\n", tmppath);
        dirdx = findex; // store it
        findex = 0;  // start with beginning of dir
      }
    }/* if isdir */
  } /* while isdir */
  strcpy(g_sSaveStateDir, fullpath);
  RegSaveString(TEXT("Preferences"),REGVALUE_PREF_SAVESTATE_DIR, 1, g_sSaveStateDir);// save it

  backdx = findex;  //store cursor position

  snprintf(tmppath, MAX_PATH, "%s/%s", fullpath, filename); // next dir
  strcpy(fullpath, tmppath);  // got ot anew

  Snapshot_SetFilename(fullpath);  // set name for snapshot
  RegSaveString(TEXT("Preferences"),REGVALUE_SAVESTATE_FILENAME, 1, fullpath);// save it
  DrawFrameWindow();
  return true;
}

void FrameSaveBMP(void)
{
  // Save current screen as a .bmp file in current directory
  struct stat bufp;
  static int i = 1;  // index
  char bmpname[20];  // file name

  snprintf(bmpname, 20, "linapple%d.bmp", i);
  while(!stat(bmpname, &bufp)) {  // find first absent file
    i++;
    snprintf(bmpname, 20, "linapple%d.bmp", i);
  }
  SDL_SaveBMP(screen, bmpname);  // save file using SDL inner function
  printf("File %s saved!\n", bmpname);
  i++;
}


//===========================================================================
void ProcessButtonClick (int button, int mod) {
  // button - number of button pressed (starting with 0, which means F1
  // mod - what modifiers been set (like CTRL, ALT etc.)
  SDL_Event qe;  // for Quitting and Reset

  SoundCore_SetFade(FADE_OUT); // sound/music off?

  switch (button) {

    case BTN_HELP:  // will get some help on the screen?
      FrameShowHelpScreen(screen->w, screen->h);

      //         TCHAR filename[MAX_PATH];
      //         _tcscpy(filename,g_sProgramDir);
      //         _tcscat(filename,TEXT("APPLEWIN.CHM"));
      //         HtmlHelp(g_hFrameWindow,filename,HH_DISPLAY_TOC,0);
      //         helpquit = 1;
      break;

    case BTN_RUN:  // F2 - Run that thing! Or Shift+2 ReloadConfig and run it anyway!
      if((mod & (KMOD_LCTRL)) == (KMOD_LCTRL) ||
          (mod & (KMOD_RCTRL)) == (KMOD_RCTRL))  {
        if (g_nAppMode == MODE_LOGO)
          DiskBoot();
        else if (g_nAppMode == MODE_RUNNING)
          ResetMachineState();
        if ((g_nAppMode == MODE_DEBUG) || (g_nAppMode == MODE_STEPPING))
          DebugEnd();
        g_nAppMode = MODE_RUNNING;
        DrawStatusArea(/*(HDC)0,*/DRAW_TITLE);
        VideoRedrawScreen();
        g_bResetTiming = true;
      }
      else if(mod & KMOD_SHIFT) {
        restart = 1;  // keep up flag of restarting
        qe.type = SDL_QUIT;
        SDL_PushEvent(&qe);// push quit event
      }
      break;

    case BTN_DRIVE1:
    case BTN_DRIVE2:
      if (mod & KMOD_SHIFT) {
        if(mod & KMOD_ALT)
          HD_FTP_Select(button - BTN_DRIVE1);// select HDV image through FTP
        else
          HD_Select(button - BTN_DRIVE1);  // select HDV image from local disk
      } else {
        if(mod & KMOD_ALT)
          Disk_FTP_SelectImage(button - BTN_DRIVE1);//select through FTP
        else
          DiskSelect(button - BTN_DRIVE1); // select image file for appropriate disk drive(#1 or #2)
      }
      /*      if (!fullscreen)
              DrawButton((HDC)0,button);*/
      break;

    case BTN_DRIVESWAP:  // F5 - swap disk drives
      DiskDriveSwap();
      break;

    case BTN_FULLSCR:  // F6 - Fullscreen on/off
      if (fullscreen) { fullscreen = 0;
        SetNormalMode(); }
      else { fullscreen = 1;
        SetFullScreenMode();}
      break;

    case BTN_DEBUG:  // F7 - debug mode - not implemented yet? Please, see README about it. --bb
      /*    if (g_nAppMode == MODE_LOGO)
            {
            ResetMachineState();
            }

            if (g_nAppMode == MODE_STEPPING)
            {
            DebuggerInputConsoleChar( DEBUG_EXIT_KEY );
            }
            else
            if (g_nAppMode == MODE_DEBUG)
            {
            g_bDebugDelayBreakCheck = true;
            ProcessButtonClick(BTN_RUN);
            }
            else
            {
            DebugBegin();
            }*/
      break;

    case BTN_SETUP:  // setup is in conf file - linapple.conf.
      // may be it should be implemented using SDL??? 0_0 --bb
      // Now Shift-F8 save settings changed run-tme in linapple.conf
      // F8 - save current screen as a .bmp file
      // Currently these setting are just next:
      if(mod & KMOD_SHIFT) {
        RegSaveValue(TEXT("Configuration"),TEXT("Video Emulation"),1,g_videotype);
        RegSaveValue(TEXT("Configuration"),TEXT("Emulation Speed"),1,g_dwSpeed);
        RegSaveValue(TEXT("Configuration"),TEXT("Fullscreen"),1,fullscreen);
      }
      else {
        FrameSaveBMP();
      }

      //      {
      //      PSP_Init();
      //}
      break;


      ////////////////////////// my buttons handlers F9..F12 ////////////////////////////
    case BTN_CYCLE: // F9 - CYCLE through allowed video modes
      //    printf("F9 has been pressed!\n");
      g_videotype++;  // Cycle through available video modes
      if (g_videotype >= VT_NUM_MODES)
        g_videotype = 0;
      VideoReinitialize();
      if ((g_nAppMode != MODE_LOGO) || ((g_nAppMode == MODE_DEBUG) && (g_bDebuggerViewingAppleOutput))) // +PATCH
      {
        VideoRedrawScreen();
        g_bDebuggerViewingAppleOutput = true;  // +PATCH
      }

      break;
    case BTN_QUIT:  // F10 - exit from emulator?

      qe.type = SDL_QUIT;
      SDL_PushEvent(&qe);// push quit event
      break;  //

    case BTN_SAVEST:  // Save state (F11)
      if(mod & KMOD_ALT) { // quick save
        Snapshot_SaveState();
      }
      else
        if(PSP_SaveStateSelectImage(true))
        {
          Snapshot_SaveState();
        }
      break;
    case BTN_LOADST:  // Load state (F12) or Hot Reset (Ctrl+F12)
      if(mod & KMOD_CTRL) {
        // Ctrl+Reset
        if (!IS_APPLE2)
          MemResetPaging();

        DiskReset();
        KeybReset();
        if (!IS_APPLE2)
          VideoResetState();  // Switch Alternate char set off
        MB_Reset();
        CpuReset();
      }
      else if(mod & KMOD_ALT)  // quick load state
      {
        Snapshot_LoadState();
      }
      else if(PSP_SaveStateSelectImage(false))
      {
        Snapshot_LoadState();
      }
      break;
  }//switch (button)
  //////////////////////////////////////////// end of my buttons handlers //////////////////

  if((g_nAppMode != MODE_DEBUG) && (g_nAppMode != MODE_PAUSED))
  {
    SoundCore_SetFade(FADE_IN);
  }
}

//===========================================================================
void ResetMachineState () {
  DiskReset();    // Set floppymotoron=0
  g_bFullSpeed = 0;  // Might've hit reset in middle of InternalCpuExecute() - so beep may get (partially) muted

  MemReset();
  DiskBoot();
  VideoResetState();
  sg_SSC.CommReset();
  PrintReset();
  JoyReset();
  MB_Reset();
  SpkrReset();
  //  SoundCore_SetFade(FADE_NONE);
}


//===========================================================================
static bool bIamFullScreened;  // for correct fullscreen switching

void SetFullScreenMode () {
  // It is simple, as almost everything in SDL. Thank you, Sam Lantinga. My appreciation! ^_^ --bb

  if(!bIamFullScreened) {
    bIamFullScreened = true;
    /*fullscreen =*/ SDL_WM_ToggleFullScreen(screen);
    //if(fullscreen) // we are in full screen disable mouse cursor
    SDL_ShowCursor(SDL_DISABLE);
  }
}

//===========================================================================
void SetNormalMode () {
  // It is simple, as almost everything in SDL. Thank you, Sam Lantinga. My appreciation! ^_^ --bb

  if(bIamFullScreened) {
    bIamFullScreened = 0;
    SDL_WM_ToggleFullScreen(screen);// we should go back anyway!? ^_^  --bb
    if(!usingcursor) SDL_ShowCursor(SDL_ENABLE); // show mouse cursor if not use it
  }
}

//===========================================================================
void SetUsingCursor (BOOL newvalue) {
  //  if (newvalue == usingcursor)
  //return;
  usingcursor = newvalue;
  if (usingcursor) {// hide mouse cursor and grab input (mouse and keyboard)
    SDL_ShowCursor(SDL_DISABLE);
    SDL_WM_GrabInput(SDL_GRAB_ON);
  }
  else {// on the contrary - show mouse cursor and ungrab input
    if(!bIamFullScreened)  SDL_ShowCursor(SDL_ENABLE);  // show cursor if not in fullscreen mode
    SDL_WM_GrabInput(SDL_GRAB_OFF);
  }
}

//
// ----- ALL GLOBALLY ACCESSIBLE FUNCTIONS ARE BELOW THIS LINE -----
//

//===========================================================================
int FrameCreateWindow ()
{
  ////************** Init SDL and create window screen
  //   int xpos;
  //   if (!RegLoadValue(TEXT("Preferences"),TEXT("Window X-Position"),1,(DWORD *)&xpos))
  //     xpos = (GetSystemMetrics(SM_CXSCREEN)-width) >> 1;
  //   int ypos;
  //   if (!RegLoadValue(TEXT("Preferences"),TEXT("Window Y-Position"),1,(DWORD *)&ypos))
  //     ypos = (GetSystemMetrics(SM_CYSCREEN)-height) >> 1;

  static char sdlCmd[] = "SDL_VIDEO_CENTERED=center";
  SDL_putenv(sdlCmd); //center our window

  bIamFullScreened = false; // at startup not in fullscreen mode
  screen = SDL_SetVideoMode(g_ScreenWidth, g_ScreenHeight, SCREEN_BPP, SDL_SWSURFACE | SDL_HWPALETTE);
  if (screen == NULL) {
    fprintf(stderr, "Could not set SDL video mode: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }//if

  // define if we have resized window
  g_WindowResized = (g_ScreenWidth != SCREEN_WIDTH) | (g_ScreenHeight != SCREEN_HEIGHT);
  printf("Screen size is %dx%d\n",g_ScreenWidth, g_ScreenHeight);
  if(g_WindowResized) {
    // create rects for screen stretching
    origRect.x = origRect.y = newRect.x = newRect.y = 0;
    origRect.w = SCREEN_WIDTH;
    origRect.h = SCREEN_HEIGHT;
    newRect.w = g_ScreenWidth;
    newRect.h = g_ScreenHeight;
  }
  // let us use keyrepeat?
  SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

  //
  //  SDL_WM_SetCaption(g_pAppTitle, g_pAppTitle); // set caption for our window screen
  return 0;
}

void SetIcon()
{
  SDL_Surface *icon = IMG_ReadXPMFromArray(icon_xpm);

  /* Black is the transparency colour.
     Part of the logo seems to use it !? */
  Uint32 colorkey = SDL_MapRGB(icon->format, 0, 0, 0);
  SDL_SetColorKey(icon, SDL_SRCCOLORKEY, colorkey);

  SDL_WM_SetIcon(icon, NULL);
}

int InitSDL()
{
  // initialize SDL subsystems, return 0 if all OK, else return 1
  if(SDL_Init(SDL_INIT_EVERYTHING) != 0){
    fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
    return 1;
  }//if

  // SDL ref: Icon should be set *before* the first call to SDL_SetVideoMode.
  SetIcon();

  return 0;
}

//===========================================================================

/*HDC FrameGetDC () {
  if (!g_hFrameDC) {
  g_hFrameDC = GetDC(g_hFrameWindow);
  SetViewportOrgEx(g_hFrameDC,viewportx,viewporty,NULL);
  }
  return g_hFrameDC;
  }

//===========================================================================
HDC FrameGetVideoDC (LPBYTE *addr, LONG *pitch) {
if (fullscreen && g_bAppActive && !painting) {
RECT rect = {FSVIEWPORTX,
FSVIEWPORTY,
FSVIEWPORTX+VIEWPORTCX,
FSVIEWPORTY+VIEWPORTCY};
DDSURFACEDESC surfacedesc;
surfacedesc.dwSize = sizeof(surfacedesc);
if (surface->Lock(&rect,&surfacedesc,0,NULL) == DDERR_SURFACELOST) {
surface->Restore();
surface->Lock(&rect,&surfacedesc,0,NULL);
}
 *addr  = (LPBYTE)surfacedesc.lpSurface+(VIEWPORTCY-1)*surfacedesc.lPitch;
 *pitch = -surfacedesc.lPitch;
 return (HDC)0;
 }
 else return FrameGetDC();
 }*/

//===========================================================================
void FrameRefreshStatus (int drawflags) {
  DrawStatusArea(/*(HDC)0,*/drawflags);
}

// //===========================================================================
// void FrameRegisterClass () {
//   WNDCLASSEX wndclass;
//   ZeroMemory(&wndclass,sizeof(WNDCLASSEX));
//   wndclass.cbSize        = sizeof(WNDCLASSEX);
//   wndclass.style         = CS_OWNDC | CS_BYTEALIGNCLIENT;
//   wndclass.lpfnWndProc   = FrameWndProc;
//   wndclass.hInstance     = g_hInstance;
//   wndclass.hIcon         = LoadIcon(g_hInstance,TEXT("APPLEWIN_ICON"));
//   wndclass.hCursor       = LoadCursor(0,IDC_ARROW);
//   wndclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
// #if ENABLE_MENU
//   wndclass.lpszMenuName   = (LPCSTR)IDR_MENU1;
// #endif
//   wndclass.lpszClassName = TEXT("APPLE2FRAME");
//   wndclass.hIconSm       = (HICON)LoadImage(g_hInstance,TEXT("APPLEWIN_ICON"),
//                                             IMAGE_ICON,16,16,LR_DEFAULTCOLOR);
//   RegisterClassEx(&wndclass);
// }

//===========================================================================
// void FrameReleaseDC () {
//   if (g_hFrameDC) {
//     SetViewportOrgEx(g_hFrameDC,0,0,NULL);
//     ReleaseDC(g_hFrameWindow,g_hFrameDC);
//     g_hFrameDC = (HDC)0;
//   }
// }
//
// //===========================================================================
// void FrameReleaseVideoDC () {
//   if (fullscreen && g_bAppActive && !painting) {
//
//     // THIS IS CORRECT ACCORDING TO THE DIRECTDRAW DOCS
//     RECT rect = {FSVIEWPORTX,
//                  FSVIEWPORTY,
//                  FSVIEWPORTX+VIEWPORTCX,
//                  FSVIEWPORTY+VIEWPORTCY};
//     surface->Unlock(&rect);
//
//     // BUT THIS SEEMS TO BE WORKING
//     surface->Unlock(NULL);
//   }
// }

