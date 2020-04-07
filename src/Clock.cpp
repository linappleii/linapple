/*
AppleWin : An Apple //e emulator for Windows

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski
Copyright (C) 2008, LEE Sau Dan

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

/* Description: Emulation of a ProDOS-compatible clock card
 *
 * Author: Copyright (c) 2008, LEE Sau Dan
 *
 * Note: Since ProDOS calculates the year using day+month+{day of week},
 *       the year calculated has a period of 7 years.  Every 6 years,
 *       you need to update ProDOS's built-in year-table.  A utility
 *       for doing this can be found at:
 *
 * http://www.apple2.org.za/mirrors/ftp.gno.org/prodos/system/pdosclockup.shk
 */

#include <time.h>
#include <stdio.h>
#include <assert.h>
#include "stdafx.h"
#include "Clock.h"


/*
I/O map: (please add an offset of Slot#*16 to the address below)

        C080	(r)   latched month (tens)
	C081	(r)   latched month (units)
        C082	(r)   always zero
	C083	(r)   latched day-of-week
        C084	(r)   latched day-of-month (tens)
	C085	(r)   latched day-of-month (units)
        C086	(r)   latched hour (tens)
	C087	(r)   latched hour (units)
        C088	(r)   latched minute (tens)
	C089	(r)   latched minute (units)
	C08F	(r)   get system clock and update the latched values
*/

/*
  This emulates a ProDOS-compatible clock card.
  The interface is specified in chapter 6 of

      ProDOS 8 Tech. Ref.
      http://apple2.info/wiki/index.php?title=P8_Tech_Ref_Chapter_6

  Briefly speaking, ProDOS detects the clock card if it finds that
    $Cx00 => $08
    $Cx02 => $28
    $Cx04 => $58
    $Cx06 => $70

  When ProDOS needs to get the time, it calls $Cx0B with A=0xA3 ("#")
  (which this ROM ignores) and then calls $Cx08.
  Upon returning from the latter, it expects the line buffer (beginning
  at $0200) to contain an ASCII (with 8-th bit on) like:
    01,02,03,04,05
  terminated by a trailing $80.
  The five 2-digit numbers, separated by commas, are:
    month (1--12), day-of-week (0=Sun - 6=Sat), day-of-month,
    hour (24hr clock), minute

  You can easily test this interface by enterint the monitor (CALL -151)
  and then type (the first "*" is the prompt character):

  *                    Cx08G 200.20F

  where x is the slot number of this clock card.
  Please leave at least 16 blanks before the character "C" so that
  the command line buffer won't be trashed by the ROM routine before
  it is processed.
*/


BYTE Clock_ROM[] =
/*
*
* ROM code for a simplistic ProDOS-compatible clock card
* for the Apple II emulator 'linapple'
*
* (C) 2008 by LEE Sau Dan
*
 ORG $C000
STACK EQU $100
IN EQU $200
DEVSEL EQU $C080
MONRTS EQU $FF58
LATCHIT EQU DEVSEL+$F

ST
 PHP  ; this opcode byte must be $08 for prodos to recognize
 BCC D1 ; offset byte must be $28 for prodos to recognize
B1
 BCS D2 ; offset byte must be $58 for prodos to recognize
B2
 DB 00
 DB $70 ; byte must be $70 for prodos to recognize
 DB 00
RDCLK ; ProDOS calls $Cx08 to get clock data
 NOP
 NOP
 DB $A9 ; opcode for LDA #xx, just to skip the next byte
WRCLK ; ProDOS calls $Cx0B with AX="#" before calling RDCLK
 RTS

RDCLK1
 PHP
 SEI
 JSR MONRTS ; known to be RTS
 TSX
 LDA STACK,X ; high byte of PC
 PLP
 ASL
 ASL
 ASL
 ASL
 TAY

 LDA LATCHIT,Y ; latch current time to regs
 LDX #$0
 BEQ RDCLK1_CONT


 DS B1+$28-*
D1
 PLP
 RTS


RDCLK1_CONT
 LDA DEVSEL,Y
 INY
 ORA #"0"
 STA IN,X
 INX
 LDA DEVSEL,Y
 INY
 ORA #"0"
 STA IN,X
 INX
 LDA #","
 STA IN,X
 INX
 TYA
 AND #$0F
 CMP #10
 BCC RDCLK1_CONT
 LDA #$80 ; EOS
 STA IN-1,X ; overwrite the last ','
 RTS


 DS B2+$58-*
D2
 BCS D1


 END
*/
  {
    0x08, 0x90, 0x28, 0xb0, 0x58, 0x00, 0x70, 0x00, 0xea, 0xea, 0xa9, 0x60, 0x08, 0x78, 0x20, 0x58,
    0xff, 0xba, 0xbd, 0x00, 0x01, 0x28, 0x0a, 0x0a, 0x0a, 0x0a, 0xa8, 0xb9, 0x8f, 0xc0, 0xa2, 0x00,
    0xf0, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x60, 0xb9, 0x80, 0xc0,
    0xc8, 0x09, 0xb0, 0x9d, 0x00, 0x02, 0xe8, 0xb9, 0x80, 0xc0, 0xc8, 0x09, 0xb0, 0x9d, 0x00, 0x02,
    0xe8, 0xa9, 0xac, 0x9d, 0x00, 0x02, 0xe8, 0x98, 0x29, 0x0f, 0xc9, 0x0a, 0x90, 0xdf, 0xa9, 0x80,
    0x9d, 0xff, 0x01, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb0, 0xcc,
  };


static BYTE latches[10];

static void set_latch_pair(int index, int value) {
  latches[index&=0x0E] = (value%=100) / 10;
  latches[index|1] = value%10;
}

static void update_latches() {
  time_t t;
  struct tm tm;

  time(&t);
  localtime_r(&t, &tm);
  set_latch_pair(0, 1+tm.tm_mon);
  set_latch_pair(2, tm.tm_wday);
  set_latch_pair(4, tm.tm_mday);
  set_latch_pair(6, tm.tm_hour);
  set_latch_pair(8, tm.tm_min);
}


static BYTE /*__stdcall*/ Clock_IORead (WORD pc, WORD addr,
					BYTE bWrite, BYTE d, ULONG nCyclesLeft)
{
  switch(addr &= 0x0F) {
  case 0: case 1:
  case 2: case 3:
  case 4: case 5:
  case 6: case 7:
  case 8: case 9:
    return latches[addr];

  case 0xF:
    update_latches();
    return 0;

  default:
    return IO_Null(pc, addr, bWrite, d, nCyclesLeft);
  }
  assert(0 == "Compiler Bug?!");
}


void Clock_Insert(int slot) {
  memset(MemGetCxRomPeripheral() + slot*256, 0, 256);
  memcpy(MemGetCxRomPeripheral() + slot*256, Clock_ROM, sizeof(Clock_ROM));

  RegisterIoHandler(slot,
		    Clock_IORead, NULL,
		    NULL, NULL,
		    NULL, NULL);
}


//end
