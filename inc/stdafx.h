#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <stdint.h>

#include <SDL3/SDL.h>
#include <cassert>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "Common.h"
#include "Structs.h"

#include "AppleWin.h"
#include "AY8910.h"

#include "CPU.h"

#include "Debug.h"


#include "Disk.h"
#include "DiskChoose.h"
#include "DiskImage.h"

#include "Frame.h"
#include "Harddisk.h"
#include "Clock.h"
#include "Joystick.h"
#include "Keyboard.h"
#include "Log.h"
#include "Memory.h"
#include "Mockingboard.h"
#include "MouseInterface.h"
#include "ParallelPrinter.h"
#include "Registry.h"

#include "Riff.h"
#include "SaveState.h"
#include "SerialComms.h"
#include "SoundCore.h"
#include "Speaker.h"

#include "stretch.h"

#include "Timer.h"
#include "Util_Text.h"

#include "Video.h"

#include "Common_Globals.h"
