//#define WIN32_LEAN_AND_MEAN

// Mouse Wheel is not supported on Win95.
// If we didn't care about supporting Win95 (compile/run-time errors)
// we would just define the minmimum windows version to support.
// #define _WIN32_WINDOWS  0x0401
#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A
#endif

//#include <crtdbg.h>
//#include <dsound.h>
//#include <dshow.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <tchar.h>
#include <time.h>

#ifndef _WIN32
#include "wincompat.h"
#else
#include <windows.h>
#endif

//#include <winuser.h> // WM_MOUSEWHEEL
//#include <commctrl.h>
//#include <ddraw.h> - no need
//#include <htmlhelp.h> - no need
#include <SDL.h>

#include "Common.h"
#include "Structs.h"

#include "AppleWin.h"
#include "AY8910.h"

#include "CPU.h"

#include "Debug.h" //- may be soon...


#include "Disk.h"
#include "DiskChoose.h"
#include "DiskImage.h"

#include "Frame.h"
#include "Harddisk.h"
#include "Joystick.h"
#include "Keyboard.h"
#include "Log.h"
#include "Memory.h"
#include "Mockingboard.h"
#include "ParallelPrinter.h"
//#include "PropertySheetPage.h"
#include "Registry.h"

#include "Riff.h"
#include "SaveState.h"
#include "SerialComms.h"
#include "SoundCore.h"  //- will do later
#include "Speaker.h"
//#include "Tfe/Tfe.h"

#include "stretch.h"

#include "Timer.h"

#include "Video.h"

//#include "Util_MemoryTextFile.h"

// iK: This is to faciliate porting to SDL2
#include "shim.h"
