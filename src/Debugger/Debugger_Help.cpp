/*
linapple : An Apple //e emulator for Linux

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2014, Tom Charlesworth, Michael Pohoreski

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

/* Description: Debugger
 *
 * Author: Copyright (C) 2006-2010 Michael Pohoreski
 */

#include "stdafx.h"

#include <cstdarg>

#include "Debug.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Help.h"
#include "Debugger_Commands.h"

#include "AppleWin.h"


#define DEBUG_COLOR_CONSOLE 0


// Utility ________________________________________________________________________________________

/*
	String types:

	http://www.codeproject.com/cpp/unicode.asp

				       strrev
	_UNICODE    Unicode      _wcsrev
	_MBCS       Multi-byte   _mbsrev
	n/a         ASCII        strrev

*/

// tests if pSrc fits into pDst
// returns true if pSrc safely fits into pDst, else false (pSrc would of overflowed pDst)
//===========================================================================
bool TestStringCat ( char * pDst, const char* pSrc, const int nDstSize )
{
	int nLenDst = strlen( pDst );
	int nLenSrc = strlen( pSrc );
	int nSpcDst = nDstSize - nLenDst;

	bool bOverflow = (nSpcDst <= nLenSrc); // 2.5.6.25 BUGFIX
	return !bOverflow;
}


// tests if pSrc fits into pDst
// returns true if pSrc safely fits into pDst, else false (pSrc would of overflowed pDst)
//===========================================================================
bool TryStringCat ( char * pDst, const char* pSrc, const int nDstSize )
{
	if (!TestStringCat( pDst, pSrc, nDstSize ))
	{
		return false;
	}

	strcat( pDst, pSrc );
	return true;
}

// cats string as much as possible
// returns true if pSrc safely fits into pDst, else false (pSrc would of overflowed pDst)
//===========================================================================
int StringCat ( char * pDst, const char* pSrc, const int nDstSize )
{
	int nLenDst = (int)strlen( pDst );
	int nLenSrc = (int)strlen( pSrc );
	int nRemaining = nDstSize - nLenDst - 1;

	if (nRemaining <= 0)
		return 0;

	if (nLenSrc > nRemaining)
	{
		memcpy( pDst + nLenDst, pSrc, nRemaining );
		pDst[nDstSize - 1] = '\0';
		return 0;
	}

	strcat( pDst, pSrc );
	return nLenSrc;
}


// Help Table ____________________________________________________________________________________

static const HelpEntry_t g_aHelpTable[] =
{
	{ CMD_ASSEMBLE,        HELP_TYPE_NOTE,    "Built-in assember isn't functional yet." },
	{ CMD_UNASSEMBLE,      HELP_TYPE_USAGE,   "[address | symbol]" },
	{ CMD_UNASSEMBLE,      HELP_TYPE_NOTE,    "Disassembles memory." },
	{ CMD_GO_NORMAL_SPEED, HELP_TYPE_USAGE,   "address | symbol [Skip,Length]" },
	{ CMD_GO_NORMAL_SPEED, HELP_TYPE_USAGE,   "address | symbol [Start:End]" },
	{ CMD_GO_NORMAL_SPEED, HELP_TYPE_NOTE,    "Skip  : Start address to skip stepping" },
	{ CMD_GO_NORMAL_SPEED, HELP_TYPE_NOTE,    "Length: Range of bytes past start address to skip stepping" },
	{ CMD_GO_NORMAL_SPEED, HELP_TYPE_NOTE,    "End   : Inclusive end address to skip stepping" },
	{ CMD_GO_NORMAL_SPEED, HELP_TYPE_NOTE,    "If the Program Counter is outside the skip range, resumes single-stepping." },
	{ CMD_GO_NORMAL_SPEED, HELP_TYPE_NOTE,    "Can be used to skip ROM/OS/user code." },
	{ CMD_GO_NORMAL_SPEED, HELP_TYPE_EXAMPLE, "G C600 FA00,600" },
	{ CMD_GO_NORMAL_SPEED, HELP_TYPE_EXAMPLE, "G C600 F000:FFFF" },
	{ CMD_GO_FULL_SPEED,   HELP_TYPE_USAGE,   "address | symbol [Skip,Length]" },
	{ CMD_GO_FULL_SPEED,   HELP_TYPE_USAGE,   "address | symbol [Start:End]" },
	{ CMD_GO_FULL_SPEED,   HELP_TYPE_EXAMPLE, "GG C600 FA00,600" },
	{ CMD_JSR,             HELP_TYPE_USAGE,   "[symbol | address]" },
	{ CMD_JSR,             HELP_TYPE_NOTE,    "Pushes PC on stack; calls the named subroutine." },
	{ CMD_NOP,             HELP_TYPE_NOTE,    "Puts a NOP opcode at current instruction" },
	{ CMD_OUT,             HELP_TYPE_USAGE,   "[address8 | address16 | symbol] ## [##]" },
	{ CMD_OUT,             HELP_TYPE_NOTE,    "Output a byte or word to the IO address $C0xx" },
	{ CMD_PROFILE,         HELP_TYPE_USAGE,   "[RESET | SAVE | LIST]" },
	{ CMD_PROFILE,         HELP_TYPE_NOTE,    "No arguments resets the profile." },
	{ CMD_REGISTER_SET,    HELP_TYPE_USAGE,   "<reg> <value | expression | symbol>" },
	{ CMD_REGISTER_SET,    HELP_TYPE_NOTE,    "Where <reg> is one of: A X Y PC SP" },
	{ CMD_REGISTER_SET,    HELP_TYPE_SEE_ALSO, "OPERATORS" },
	{ CMD_REGISTER_SET,    HELP_TYPE_EXAMPLE, "R PC RESET + 1" },
	{ CMD_SOURCE,          HELP_TYPE_USAGE,   "[ MEM | SYM ] \"filename\"" },
	{ CMD_SOURCE,          HELP_TYPE_NOTE,    "MEM: read source bytes into memory." },
	{ CMD_SOURCE,          HELP_TYPE_NOTE,    "SYM: read symbols into Source symbol table." },
	{ CMD_SOURCE,          HELP_TYPE_NOTE,    "Supports: MERLIN." },
	{ CMD_STEP_OUT,        HELP_TYPE_NOTE,    "Steps out of current subroutine" },
	{ CMD_STEP_OVER,       HELP_TYPE_USAGE,   "[#]" },
	{ CMD_STEP_OVER,       HELP_TYPE_NOTE,    "Steps, # times, thru current instruction" },
	{ CMD_STEP_OVER,       HELP_TYPE_NOTE,    "JSR will be stepped into AND out of." },
	{ CMD_TRACE,           HELP_TYPE_USAGE,   "[#]" },
	{ CMD_TRACE,           HELP_TYPE_NOTE,    "Traces, # times, current instruction(s)" },
	{ CMD_TRACE,           HELP_TYPE_NOTE,    "JSR will be stepped into" },
	{ CMD_TRACE_FILE,      HELP_TYPE_USAGE,   "\"[filename]\" [v]" },
	{ CMD_TRACE_LINE,      HELP_TYPE_USAGE,   "[#]" },
	{ CMD_TRACE_LINE,      HELP_TYPE_NOTE,    "Traces into current instruction with cycle counting." },
	{ CMD_BOOKMARK_ADD,    HELP_TYPE_USAGE,   "[address | symbol]" },
	{ CMD_BOOKMARK_ADD,    HELP_TYPE_USAGE,   "# <address | symbol>" },
	{ CMD_BOOKMARK_ADD,    HELP_TYPE_NOTE,    "If no address or symbol is specified, lists the current bookmarks." },
	{ CMD_BOOKMARK_ADD,    HELP_TYPE_NOTE,    "Updates the specified bookmark (#)" },
	{ CMD_BOOKMARK_ADD,    HELP_TYPE_EXAMPLE, "BM RESET" },
	{ CMD_BOOKMARK_CLEAR,  HELP_TYPE_USAGE,   "[# | *]" },
	{ CMD_BOOKMARK_CLEAR,  HELP_TYPE_NOTE,    "Clears specified bookmark, or all." },
	{ CMD_BREAK_INVALID,   HELP_TYPE_USAGE,   "[ON | OFF] | [ # | # ON | # OFF ]" },
	{ CMD_BREAK_INVALID,   HELP_TYPE_NOTE,    "Where: # is 0=BRK, 1=Invalid Opcode_1, 2=Invalid Opcode_2, 3=Invalid Opcode_3" },
	{ CMD_BREAKPOINT,      HELP_TYPE_USAGE,   "[LOAD | SAVE | RESET]" },
	{ CMD_BREAKPOINT,      HELP_TYPE_NOTE,    "Set breakpoint at PC if no args." },
	{ CMD_BREAKPOINT_ADD_REG, HELP_TYPE_USAGE, "[A|X|Y|PC|S] [op] <range | value>" },
	{ CMD_BREAKPOINT_ADD_REG, HELP_TYPE_NOTE,  "Set breakpoint when reg is [op] value" },
	{ CMD_BREAKPOINT_ADD_REG, HELP_TYPE_SEE_ALSO, "OPERATORS" },
	{ CMD_BREAKPOINT_ADD_REG, HELP_TYPE_EXAMPLE, "BRP PC < D000" },
	{ CMD_BREAKPOINT_ADD_SMART, HELP_TYPE_USAGE, "[address | register]" },
	{ CMD_BREAKPOINT_ADD_SMART, HELP_TYPE_NOTE,  "If address, sets memory access and PC breakpoints." },
	{ CMD_BREAKPOINT_ADD_PC,    HELP_TYPE_USAGE, "[address]" },
	{ CMD_BREAKPOINT_ADD_PC,    HELP_TYPE_NOTE,  "Sets a breakpoint at the current PC or address." },
	{ CMD_BREAKPOINT_CLEAR,     HELP_TYPE_USAGE, "[# | *]" },
	{ CMD_BREAKPOINT_CLEAR,     HELP_TYPE_NOTE,  "Clears specified breakpoint, or all." },
	{ CMD_BREAKPOINT_ADD_MEM,   HELP_TYPE_USAGE, "<range>" },
	{ CMD_BREAKPOINT_ADD_MEM,   HELP_TYPE_RANGE, "" },
	{ CMD_CONFIG_LOAD,          HELP_TYPE_USAGE, "[\"filename\"]" },
	{ CMD_CONFIG_SAVE,          HELP_TYPE_USAGE, "[\"filename\"]" },
	{ CMD_DEFINE_DATA_BYTE1,    HELP_TYPE_USAGE, "<address> | <symbol address> | <symbol range>" },
	{ CMD_DEFINE_DATA_BYTE1,    HELP_TYPE_NOTE,  "Treat BYTES as data instead of code." },
	{ CMD_MEMORY_FILL,          HELP_TYPE_USAGE, "<address | symbol> <address | symbol> ##" },
	{ CMD_MEMORY_FILL,          HELP_TYPE_NOTE,  "Fills the memory range with the specified byte" },
	{ CMD_MEMORY_SEARCH,        HELP_TYPE_USAGE, "range <\"ASCII text\" | 'apple text' | hex>" },
	{ CMD_MEMORY_SEARCH,        HELP_TYPE_RANGE, "" },
	{ CMD_CYCLES_INFO,          HELP_TYPE_USAGE, "<abs|rel>" },
	{ CMD_VIDEO_SCANNER_INFO,   HELP_TYPE_USAGE, "<dec|hex|real|apple>" },
	{ CMD_ZEROPAGE_POINTER_ADD, HELP_TYPE_USAGE, "<address | symbol>" },
	{ CMD_ZEROPAGE_POINTER_ADD, HELP_TYPE_USAGE, "# <address | symbol> [address...]" },
	{ CMD_VERSION,              HELP_TYPE_USAGE, "[*]" },
	{ 0, NUM_HELP_TYPES, NULL }
};

// Help ___________________________________________________________________________________________


//===========================================================================
Update_t HelpLastCommand()
{
	return Help_Arg_1( g_iCommand );
}


// Loads the arguments with the command to get help on and call display help.
//===========================================================================
Update_t Help_Arg_1( int iCommandHelp )
{
	_Arg_1( iCommandHelp );

	sprintf( g_aArgs[ 1 ].sArg, "%s", g_aCommands[ iCommandHelp ].m_sName ); // .3 Fixed: Help_Arg_1() now copies command name into arg.name

	return CmdHelpSpecific( 1 );
}


//===========================================================================
void Help_Categories()
{
	const int nBuf = CONSOLE_WIDTH * 2;

	char sText[ nBuf ] = "";
	int  nLen = 0;

		// TODO/FIXME: Colorize( sText, ... )
		// Colorize("Usage:")
		nLen += StringCat( sText, CHC_USAGE , nBuf );
		nLen += StringCat( sText, "Usage", nBuf );

		nLen += StringCat( sText, CHC_DEFAULT, nBuf );
		nLen += StringCat( sText, ": " , nBuf );

		nLen += StringCat( sText, CHC_ARG_OPT, nBuf );
		nLen += StringCat( sText, "[ ", nBuf );

		nLen += StringCat( sText, CHC_ARG_MAND, nBuf );
		nLen += StringCat( sText, "< ", nBuf );


		for (int iCategory = _PARAM_HELPCATEGORIES_BEGIN ; iCategory < _PARAM_HELPCATEGORIES_END; iCategory++)
		{
			char *pName = g_aParameters[ iCategory ].m_sName;

			if (nLen + strlen( pName ) >= (CONSOLE_WIDTH - 1))
			{
				ConsolePrint( sText );
				sText[ 0 ] = 0;
				nLen = StringCat( sText, "    ", nBuf ); // indent
			}

			        StringCat( sText, CHC_COMMAND, nBuf );
			nLen += StringCat( sText, pName      , nBuf );

			if (iCategory < (_PARAM_HELPCATEGORIES_END - 1))
			{
				char sSep[] = " | ";

				if (nLen + strlen( sSep ) >= (CONSOLE_WIDTH - 1))
				{
					ConsolePrint( sText );
					sText[ 0 ] = 0;
					nLen = StringCat( sText, "    ", nBuf ); // indent
				}
				        StringCat( sText, CHC_ARG_SEP, nBuf );
				nLen += StringCat( sText, sSep, nBuf );
			}
		}
		StringCat( sText, CHC_ARG_MAND, nBuf );
		StringCat( sText, " >", nBuf);

		StringCat( sText, CHC_ARG_OPT, nBuf );
		StringCat( sText, " ]", nBuf);

//		ConsoleBufferPush( sText );
		ConsolePrint( sText );  // Transcode colored text to native console color text

		ConsolePrintFormat( sText, "%sNotes%s: %s<>%s = mandatory, %s[]%s = optional, %s|%s argument option"
			, CHC_USAGE
			, CHC_DEFAULT
			, CHC_ARG_MAND
			, CHC_DEFAULT
			, CHC_ARG_OPT
			, CHC_DEFAULT
			, CHC_ARG_SEP
			, CHC_DEFAULT
		);
//		ConsoleBufferPush( sText );
}

void Help_Examples()
{
	char sText[ CONSOLE_WIDTH ];
	ConsolePrintFormat( sText, " %sExamples%s:%s"
		, CHC_USAGE
		, CHC_ARG_SEP
		, CHC_DEFAULT
	);
}

//===========================================================================
void Help_Range()
{
	ConsoleBufferPush( "  Where <range> is of the form:"                 );
	ConsoleBufferPush( "    address , length   [address,address+length)" );
	ConsoleBufferPush( "    address : end      [address,end]"            );
}

//===========================================================================
void Help_Operators()
{
	char sText[ CONSOLE_WIDTH ];

//	ConsolePrintFormat( sText," %sOperators%s:"                                 , CHC_USAGE, CHC_DEFAULT );
//	ConsolePrintFormat( sText,"  Operators: (Math)"                             );
	ConsolePrintFormat( sText,"  Operators: (%sMath%s)"                         , CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s+%s   Addition"                            , CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s-%s   Subtraction"                         , CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s*%s   Multiplication"                      , CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s/%s   Division"                            , CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s%%%s   Modulas or Remainder"               , CHC_USAGE, CHC_DEFAULT );
//ConsoleBufferPush( "  Operators: (Bit Wise)"                         );
	ConsolePrintFormat( sText,"  Operators: (%sBit Wise%s)"                     , CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s&%s   Bit-wise and (AND)"                  , CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s|%s   Bit-wise or  (OR )"                  , CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s^%s   Bit-wise exclusive-or (EOR/XOR)"     , CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s!%s   Bit-wise negation (NOT)"             , CHC_USAGE, CHC_DEFAULT );
//ConsoleBufferPush( "  Operators: (Input)"                            );
	ConsolePrintFormat( sText,"  Operators: (%sInput%s)"                        , CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s@%s   next number refers to search results", CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s\"%s   Designate string in ASCII format"   , CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s\'%s   Desginate string in High-Bit apple format", CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s$%s   Designate number/symbol"             , CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s#%s   Designate number in hex"             , CHC_USAGE, CHC_DEFAULT );
//ConsoleBufferPush( "  Operators: (Range)"                            );
	ConsolePrintFormat( sText,"  Operators: (%sRange%s)"                        , CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s,%s   range seperator (2nd address is relative)", CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s:%s   range seperator (2nd address is absolute)", CHC_USAGE, CHC_DEFAULT );
//	ConsolePrintFormat( sText,"  Operators: (Misc)"                             );
	ConsolePrintFormat( sText,"  Operators: (%sMisc%s)"                         , CHC_USAGE, CHC_DEFAULT );
	ConsolePrintFormat( sText,"    %s//%s  comment until end of line"           , CHC_USAGE, CHC_DEFAULT );
//ConsoleBufferPush( "  Operators: (Breakpoint)"                       );
	ConsolePrintFormat( sText,"  Operators: (%sBreakpoint%s)"                   , CHC_USAGE, CHC_DEFAULT );

	strcpy( sText, "    " );
	strcat( sText, CHC_USAGE );
	int iBreakOp = 0;
	for( iBreakOp = 0; iBreakOp < NUM_BREAKPOINT_OPERATORS; iBreakOp++ )
	{
		if ((iBreakOp >= PARAM_BP_LESS_EQUAL) &&
			(iBreakOp <= PARAM_BP_GREATER_EQUAL))
		{
			strcat( sText, g_aBreakpointSymbols[ iBreakOp ] );
			strcat( sText, " " );
		}
	}
	strcat( sText, CHC_DEFAULT );
	ConsolePrint( sText );
}

void Help_KeyboardShortcuts()
{
	ConsoleBufferPush("  Scrolling:"                                         );
	ConsoleBufferPush("    Up Arrow"                                         );
	ConsoleBufferPush("    Down Arrow"                                       );
	ConsoleBufferPush("    Shift + Up Arrow"                                 );
	ConsoleBufferPush("    Shift + Down Arrow"                               );
	ConsoleBufferPush("    Page Up"                                          );
	ConsoleBufferPush("    Page Down"                                        );
	ConsoleBufferPush("    Shift + Page Up"                                  );
	ConsoleBufferPush("    Shift + Page Down"                                );

	ConsoleBufferPush("  Bookmarks:"                                         );
	ConsoleBufferPush("    Ctrl-Shift-#"                                     );
	ConsoleBufferPush("    Ctrl-#      "                                     );
}


void _ColorizeHeader( char * & pDst, const char * & pSrc, const char * pHeader, const int nHeaderLen )
{
	int nLen;

	nLen = strlen( CHC_USAGE );
	strcpy( pDst, CHC_USAGE );
	pDst += nLen;

	nLen = nHeaderLen - 1;
	Util_SafeStrCpy( pDst, pHeader, nLen );
	pDst += nLen;

	pSrc += nHeaderLen;

	nLen = strlen( CHC_ARG_SEP );
	strcpy( pDst, CHC_ARG_SEP );
	pDst += nLen;

	*pDst = ':';
	pDst++;

	nLen = strlen( CHC_DEFAULT );
	strcpy( pDst, CHC_DEFAULT );
	pDst += nLen;
}


void _ColorizeString(
	char * & pDst,
	const char *pSrc, const size_t nLen )
{
	strcpy( pDst, pSrc );
	pDst += nLen;
}


// pOperator is one of CHC_*
void _ColorizeOperator(
	char * & pDst, const char * & pSrc,
	char * pOperator )
{
	int nLen;

	nLen = strlen( pOperator );
	strcpy( pDst, pOperator );
	pDst += nLen;

	*pDst = *pSrc;
	pDst++;

	nLen = strlen( CHC_DEFAULT );
	strcpy( pDst, CHC_DEFAULT );
	pDst += nLen;

	pSrc++;
}




bool isHexDigit( char c )
{
	if ((c >= '0') &&  (c <= '9')) return true;
	if ((c >= 'A') &&  (c <= 'F')) return true;
	if ((c >= 'a') &&  (c <= 'f')) return true;

	return false;
}


bool Colorize( char * pDst, const char * pSrc )
{
	if (! pSrc)
		return false;

	if (! pDst)
		return false;

	const char sNote [] = "Note:";
	const int  nNote    = sizeof( sNote ) - 1; 

	const char sSeeAlso[] = "See also:";
	const char nSeeAlso   = sizeof( sSeeAlso ) - 1;

	const char sUsage[] = "Usage:";
	const int  nUsage   = sizeof( sUsage ) - 1;

	const char sTotal[] = "Total:";
	const int  nTotal = sizeof( sTotal ) - 1;

	const char sExamples[] = "Examples:";
	const int  nExamples = sizeof( sExamples ) - 1;

	while (*pSrc)
	{
		if (strncmp( sUsage, pSrc, nUsage) == 0)
		{
			_ColorizeHeader( pDst, pSrc, sUsage, nUsage );
		}
		else
		if (strncmp( sSeeAlso, pSrc, nSeeAlso) == 0)
		{
			_ColorizeHeader( pDst, pSrc, sSeeAlso, nSeeAlso );
		}
		else
		if (strncmp( sNote, pSrc, nNote) == 0)
		{
			_ColorizeHeader( pDst, pSrc, sNote, nNote );
		}
		else
		if (strncmp( sTotal, pSrc, nNote) == 0)
		{
			_ColorizeHeader( pDst, pSrc, sTotal, nTotal );
		}
		else
		if (strncmp( sExamples, pSrc, nExamples) == 0)
		{
			_ColorizeHeader( pDst, pSrc, sExamples, nExamples );
		}
		else
		if (*pSrc == '[')
		{
			_ColorizeOperator( pDst, pSrc, CHC_ARG_OPT );
		}
		else
		if (*pSrc == ']')
		{
			_ColorizeOperator( pDst, pSrc, CHC_ARG_OPT );
		}
		else
		if (*pSrc == '<')
		{
			_ColorizeOperator( pDst, pSrc, CHC_ARG_MAND );
		}
		else
		if (*pSrc == '>')
		{
			_ColorizeOperator( pDst, pSrc, CHC_ARG_MAND );
		}
		else
		if (*pSrc == '|')
		{
			_ColorizeOperator( pDst, pSrc, CHC_ARG_SEP );
		}
		else
		if (*pSrc == '\'')
		{
			_ColorizeOperator( pDst, pSrc, CHC_ARG_SEP );
		}
		else
		if ((*pSrc == '$') && isHexDigit(pSrc[1])) // Hex Number
		{
			_ColorizeOperator( pDst, pSrc, CHC_ARG_SEP );

			const char *start = pSrc;
			const char *end   = pSrc;

			while( isHexDigit( *end ) )
				end++;

			size_t nDigits = end - start;

			_ColorizeString( pDst, CHC_NUM_HEX, strlen( CHC_NUM_HEX ) );
			_ColorizeString( pDst, start      , nDigits               );
			_ColorizeString( pDst, CHC_DEFAULT, strlen( CHC_DEFAULT ) );

			pSrc += nDigits;
		}
		else
		{
			*pDst = *pSrc;
			pDst++;
			pSrc++;
		}
	}
	*pDst = 0;
	return true;
}

// NOTE: This appends a new line
inline bool ConsoleColorizePrint( char* colorizeBuf, size_t /*colorizeBufSz*/,
                                  const char* pText )
{
   if (!Colorize(colorizeBuf, pText)) return false;
   return ConsolePrint(colorizeBuf);
}

template<size_t _ColorizeBufSz>
inline bool ConsoleColorizePrint( char (&colorizeBuf)[_ColorizeBufSz],
                                  const char* pText )
{
   return ConsoleColorizePrint(colorizeBuf, _ColorizeBufSz, pText);
}

inline bool ConsoleColorizePrintVa( char* colorizeBuf, size_t colorizeBufSz,
                                    char* buf, size_t bufsz,
                                    const char* format, va_list va )
{
   vsnprintf(buf, bufsz, format, va);
   return ConsoleColorizePrint(colorizeBuf, colorizeBufSz, buf);
}

template<size_t _ColorizeBufSz, size_t _BufSz>
inline bool ConsoleColorizePrintVa( char (&colorizeBuf)[_ColorizeBufSz],
                                    char (&buf)[_BufSz],
                                    const char* format, va_list va )
{
   return ConsoleColorizePrintVa(colorizeBuf, _ColorizeBufSz, buf, _BufSz, format, va);
}

inline bool ConsoleColorizePrintFormat( char* colorizeBuf, size_t colorizeBufSz,
                                        char* buf, size_t bufsz,
                                        const char* format, ... )
{
   va_list va;
   va_start(va, format);
   bool const r = ConsoleColorizePrintVa(colorizeBuf, colorizeBufSz, buf, bufsz, format, va);
   va_end(va);
   return r;
}

template<size_t _ColorizeBufSz, size_t _BufSz>
inline bool ConsoleColorizePrintFormat( char (&colorizeBuf)[_ColorizeBufSz],
                                        char (&buf)[_BufSz],
                                        const char* format, ... )
{
   va_list va;
   va_start(va, format);
   bool const r = ConsoleColorizePrintVa(colorizeBuf, buf, format, va);
   va_end(va);
   return r;
}

//===========================================================================
Update_t CmdMOTD( int nArgs )	// Message Of The Day
{
  (void)nArgs;
	char sText[ CONSOLE_WIDTH*2 ];
	char sTemp[ CONSOLE_WIDTH*2 ];

#if DEBUG_COLOR_CONSOLE
	ConsolePrint( "`" );
	ConsolePrint( "`A" );
	ConsolePrint( "`2`A" );
#endif

	ConsolePrintFormat( sText, "`9`A`7 Apple `9][ ][+ //e `7Emulator for Linux `9`@" );

	CmdVersion(0);
	CmdSymbols(0);
	ConsoleColorizePrintFormat( sTemp, sText, "  '%sCtrl ~'%s console, '%s%s'%s (specific), '%s%s'%s (all)"
		, CHC_KEY
		, CHC_DEFAULT
		, CHC_COMMAND
		, g_aCommands[ CMD_HELP_SPECIFIC ].m_sName
		, CHC_DEFAULT
//		, g_aCommands[ CMD_HELP_SPECIFIC ].pHelpSummary
		, CHC_COMMAND
		, g_aCommands[ CMD_HELP_LIST     ].m_sName
		, CHC_DEFAULT
//		, g_aCommands[ CMD_HELP_LIST     ].pHelpSummary
	);

	ConsoleUpdate();

	return UPDATE_ALL;
}


// Help on specific command
//===========================================================================
Update_t CmdHelpSpecific (int nArgs)
{
	int iArg;
	char sText[ CONSOLE_WIDTH * 2 ];
	char sTemp[ CONSOLE_WIDTH * 2 ];
	memset( sText, 0, CONSOLE_WIDTH*2 );
	memset( sTemp, 0, CONSOLE_WIDTH*2 );

	if (! nArgs)
	{
		Help_Categories();
		return ConsoleUpdate();
	}

	CmdFuncPtr_t pFunction = NULL;
	bool bAllCommands = false;
	bool bCategory = false;
	bool bDisplayCategory = true;

	if ((! strcmp( g_aArgs[1].sArg, g_aParameters[ PARAM_WILDSTAR ].m_sName)) ||
		(! strcmp( g_aArgs[1].sArg, g_aParameters[ PARAM_MEM_SEARCH_WILD ].m_sName)) )
	{
		bAllCommands = true;
		nArgs = NUM_COMMANDS;
	}

	// If Help on category, push command name as arg
	// Mame has categories:
	//	General, Memory, Execution, Breakpoints, Watchpoints, Expressions, Comments
	int iParam = 0;

	int nNewArgs  = 0;
	int iCmdBegin = 0;
	int iCmdEnd   = 0;
	int nFound    = 0;
	int iCommand  = 0;

	if (! bAllCommands)
	{
		for (iArg = 1; iArg <= nArgs; iArg++ )
		{
	//		int nFoundCategory = FindParam( g_aArgs[ iArg ].sArg, MATCH_EXACT, iParam, _PARAM_HELPCATEGORIES_BEGIN, _PARAM_HELPCATEGORIES_END );
			int nFoundCategory = FindParam( g_aArgs[ iArg ].sArg, MATCH_FUZZY, iParam, _PARAM_HELPCATEGORIES_BEGIN, _PARAM_HELPCATEGORIES_END );
			bCategory = nFoundCategory != 0;
			switch( iParam )
			{
				case PARAM_CAT_BOOKMARKS  : iCmdBegin = CMD_BOOKMARK        ; iCmdEnd = CMD_BOOKMARK_SAVE        ; break;
				case PARAM_CAT_BREAKPOINTS: iCmdBegin = CMD_BREAK_INVALID   ; iCmdEnd = CMD_BREAKPOINT_SAVE      ; break;
				case PARAM_CAT_CONFIG     : iCmdBegin = CMD_BENCHMARK       ; iCmdEnd = CMD_CONFIG_SET_DEBUG_DIR; break;
				case PARAM_CAT_CPU        : iCmdBegin = CMD_ASSEMBLE        ; iCmdEnd = CMD_UNASSEMBLE           ; break;
				case PARAM_CAT_FLAGS      :
					nFound = FindCommand( g_aArgs[iArg].sArg, pFunction, & iCommand ); // check if we have an exact command match first
					if ( nFound ) // && (iCommand != CMD_MEMORY_FILL))
						bCategory = false;
					else if ( nFoundCategory )
					{
						iCmdBegin = CMD_FLAG_CLEAR     ; iCmdEnd = CMD_FLAG_SET_N;
					}
					break;
				case PARAM_CAT_HELP       : iCmdBegin = CMD_HELP_LIST       ; iCmdEnd = CMD_MOTD                 ; break;
				case PARAM_CAT_KEYBOARD   :
					nFound = FindCommand( g_aArgs[iArg].sArg, pFunction, & iCommand ); // check if we have an exact command match first
					if ((!nFound) || (iCommand != CMD_INPUT_KEY))
					{
						nArgs = 0;
						Help_KeyboardShortcuts();
					}
					bCategory = false;
					break;
				case PARAM_CAT_MEMORY     :
					nFound = FindCommand( g_aArgs[iArg].sArg, pFunction, & iCommand );  // check if we have an exact command match first
					if ( nFound )// && (iCommand != CMD_MEMORY_MOVE))
						bCategory = false;
					else if ( nFoundCategory )
					{
						iCmdBegin = CMD_MEMORY_COMPARE                      ; iCmdEnd = CMD_MEMORY_FILL           ;
					}
					break;
				case PARAM_CAT_OUTPUT     :
					nFound = FindCommand( g_aArgs[iArg].sArg, pFunction, & iCommand );  // check if we have an exact command match first
					if ( nFound ) // && (iCommand != CMD_OUT))
						bCategory = false;
					else if ( nFoundCategory )
					{
						iCmdBegin = CMD_OUTPUT_CALC                         ; iCmdEnd = CMD_OUTPUT_RUN           ;
					}
					break;
				case PARAM_CAT_SYMBOLS    :
					nFound = FindCommand( g_aArgs[iArg].sArg, pFunction, & iCommand );  // check if we have an exact command match first
					if ( nFound ) // && (iCommand != CMD_SYMBOLS_LOOKUP) && (iCommand != CMD_MEMORY_SEARCH))
						bCategory = false;
					else if ( nFoundCategory )
					{
						iCmdBegin = CMD_SYMBOLS_LOOKUP                      ; iCmdEnd = CMD_SYMBOLS_LIST         ;
					}
					break;
				case PARAM_CAT_VIEW       :
					{
						iCmdBegin = CMD_VIEW_TEXT4X                         ; iCmdEnd = CMD_VIEW_DHGR2          ;
					}
					break;
				case PARAM_CAT_WATCHES    :
					nFound = FindCommand( g_aArgs[iArg].sArg, pFunction, & iCommand );  // check if we have an exact command match first
					if (nFound) {
						bCategory = false;
					} else  // 2.7.0.17: HELP <category> wasn't displaying when category was one of: FLAGS, OUTPUT, WATCHES
						if( nFoundCategory )
						{
							iCmdBegin = CMD_WATCH_ADD       ; iCmdEnd = CMD_WATCH_LIST           ;
						}
					break;
				case PARAM_CAT_WINDOW     : iCmdBegin = CMD_WINDOW          ; iCmdEnd = CMD_WINDOW_OUTPUT        ; break;
				case PARAM_CAT_ZEROPAGE   : iCmdBegin = CMD_ZEROPAGE_POINTER; iCmdEnd = CMD_ZEROPAGE_POINTER_SAVE; break;
//				case PARAM_CAT_EXPRESSION : // fall-through
				case PARAM_CAT_OPERATORS  : nArgs = 0; Help_Operators(); break;
				case PARAM_CAT_RANGE      :
					// HACK: check if we have an exact command match first
					nFound = FindCommand( g_aArgs[iArg].sArg, pFunction, & iCommand );
					if ((!nFound) || (iCommand != CMD_REGISTER_SET))
					{
						nArgs = 0;
						Help_Range();
					}
					bCategory = false;
					break;
				default:
					bCategory = false;
					break;
			}
			if (iCmdEnd)
				iCmdEnd++;
			nNewArgs = (iCmdEnd - iCmdBegin);
			if (nNewArgs > 0)
				break;
		}
	}

	if (nNewArgs > 0)
	{
		nArgs = nNewArgs;
		for (iArg = 1; iArg <= nArgs; iArg++ )
		{
#if DEBUG_VAL_2
			g_aArgs[ iArg ].nVal2 = iCmdBegin + iArg - 1;
#endif
			g_aArgs[ iArg ].nValue = iCmdBegin + iArg - 1;
		}
	}

	for (iArg = 1; iArg <= nArgs; iArg++ )
	{
		iCommand = 0;
		nFound = 0;

		if (bCategory)
		{
#if DEBUG_VAL_2
			iCommand = g_aArgs[iArg].nVal2;
#endif
			iCommand = g_aArgs[ iArg  ].nValue;
			nFound = 1;
		}
		else
		if (bAllCommands)
		{
			iCommand = iArg;
			if (iCommand == NUM_COMMANDS) // skip: Internal Consistency Check __COMMANDS_VERIFY_TXT__
				continue;
			nFound = 1;
		}
		else
			nFound = FindCommand( g_aArgs[iArg].sArg, pFunction, & iCommand );

		if (nFound > 1)
		{
			DisplayAmbigiousCommands( nFound );
		}

		if (iCommand > NUM_COMMANDS)
			continue;

		if ((nArgs == 1) && (! nFound))
			iCommand = g_aArgs[iArg].nValue;

		Command_t *pCommand = & g_aCommands[ iCommand ];

		if (! nFound)
		{
			iCommand = NUM_COMMANDS;
			pCommand = NULL;
		}

//		if (nFound && (! bAllCommands) && (! bCategory))
		if (nFound && (! bAllCommands) && bDisplayCategory)
		{
			char sCategory[ CONSOLE_WIDTH ];
			int iCmd = g_aCommands[ iCommand ].iCommand; // Unaliased command

			// HACK: Major kludge to display category!!!
			if (iCmd <= CMD_UNASSEMBLE)
				sprintf( sCategory, "%s", g_aParameters[ PARAM_CAT_CPU ].m_sName );
			else
			if (iCmd <= CMD_BOOKMARK_SAVE)
				sprintf( sCategory, "%s", g_aParameters[ PARAM_CAT_BOOKMARKS ].m_sName );
			else
			if (iCmd <= CMD_BREAKPOINT_SAVE)
				sprintf( sCategory, "%s", g_aParameters[ PARAM_CAT_BREAKPOINTS ].m_sName );
			else
			if (iCmd <= CMD_CONFIG_SET_DEBUG_DIR)
				sprintf( sCategory, "%s", g_aParameters[ PARAM_CAT_CONFIG ].m_sName );
			else
			if (iCmd <= CMD_CURSOR_PAGE_DOWN_4K)
				sprintf( sCategory, "Scrolling" );
			else
			if (iCmd <= CMD_FLAG_SET_N)
				sprintf( sCategory, "%s", g_aParameters[ PARAM_CAT_FLAGS ].m_sName );
			else
			if (iCmd <= CMD_MOTD)
				sprintf( sCategory, "%s", g_aParameters[ PARAM_CAT_HELP ].m_sName );
			else
			if (iCmd <= CMD_MEMORY_FILL)
				sprintf( sCategory, "%s", g_aParameters[ PARAM_CAT_MEMORY ].m_sName );
			else
			if (iCmd <= CMD_OUTPUT_RUN)
				sprintf( sCategory, "%s", g_aParameters[ PARAM_CAT_OUTPUT ].m_sName );
			else
			if (iCmd <= CMD_SYNC)
				sprintf( sCategory, "Source" );
			else
			if (iCmd <= CMD_SYMBOLS_LIST)
				sprintf( sCategory, "%s", g_aParameters[ PARAM_CAT_SYMBOLS ].m_sName );
			else
			if (iCmd <= CMD_VIEW_DHGR2)
				sprintf( sCategory, "%s", g_aParameters[ PARAM_CAT_VIEW ].m_sName );
			else
			if (iCmd <= CMD_WATCH_SAVE)
				sprintf( sCategory, "%s", g_aParameters[ PARAM_CAT_WATCHES ].m_sName );
			else
			if (iCmd <= CMD_WINDOW_OUTPUT)
				sprintf( sCategory, "%s", g_aParameters[ PARAM_CAT_WINDOW ].m_sName );
			else
			if (iCmd <= CMD_ZEROPAGE_POINTER_SAVE)
				sprintf( sCategory, "%s", g_aParameters[ PARAM_CAT_ZEROPAGE ].m_sName );
			else
				sprintf( sCategory, "Unknown!" );

			ConsolePrintFormat( sText, "%sCategory%s: %s%s"
				, CHC_USAGE
				, CHC_DEFAULT
				, CHC_CATEGORY
				, sCategory );

			if (bCategory)
				if (bDisplayCategory)
					bDisplayCategory = false;
		}

		if (pCommand)
		{
			char *pHelp = pCommand->pHelpSummary;
			if (pHelp)
			{
				if (bCategory)
					sprintf( sText, "%s%8s%s, "
						, CHC_COMMAND
						, pCommand->m_sName
						, CHC_ARG_SEP
					);
				else
					sprintf( sText, "%s%s%s, "
						, CHC_COMMAND
						, pCommand->m_sName
						, CHC_ARG_SEP
					);

//				if (! TryStringCat( sText, pHelp, g_nConsoleDisplayWidth ))
//				{
//					if (! TryStringCat( sText, pHelp, CONSOLE_WIDTH-1 ))
//					{
						strncat( sText, CHC_DEFAULT, CONSOLE_WIDTH );
						strncat( sText, pHelp      , CONSOLE_WIDTH );
//						ConsoleBufferPush( sText );
//					}
//				}
				ConsolePrint( sText );
			}
			else
			{
#if _DEBUG
				ConsoleBufferPushFormat( sText, "%s  <-- Missing", pCommand->m_sName );
	#if DEBUG_COMMAND_HELP
				if (! bAllCommands) // Release version doesn't display message
				{
					ConsoleBufferPushFormat( sText, "Missing Summary Help: %s", g_aCommands[ iCommand ].aName );
				}
	#endif
#endif
			}

			if (bCategory)
				continue;
		}

		// MASTER HELP
		bool bFoundAny = false;
		for (int iHelp = 0; g_aHelpTable[iHelp].pText != NULL; iHelp++)
		{
			if (g_aHelpTable[iHelp].iCommand == iCommand)
			{
				bFoundAny = true;
				const HelpEntry_t* pEntry = &g_aHelpTable[iHelp];
				switch (pEntry->eType)
				{
				case HELP_TYPE_USAGE:
					ConsoleColorizePrintFormat(sTemp, sText, " Usage: %s", pEntry->pText);
					break;
				case HELP_TYPE_NOTE:
					ConsoleBufferPushFormat(sText, "  %s", pEntry->pText);
					break;
				case HELP_TYPE_EXAMPLE:
					Help_Examples();
					ConsolePrintFormat(sText, "%s  %s", CHC_EXAMPLE, pEntry->pText);
					break;
				case HELP_TYPE_RANGE:
					Help_Range();
					break;
				case HELP_TYPE_SEE_ALSO:
					ConsoleColorizePrintFormat(sTemp, sText, " See also: %s%s", CHC_CATEGORY, pEntry->pText);
					break;
				default:
					break;
				}
			}
		}

		if (!bFoundAny && !bAllCommands)
		{
			if ((!nFound) || (!pCommand))
			{
				ConsoleBufferPush(" Invalid command.");
			}
			else
			{
#if _DEBUG
				ConsoleBufferPushFormat(sText, "Command help not done yet!: %s", g_aCommands[iCommand].m_sName);
#endif
			}
		}

	}

	return ConsoleUpdate();
}

//===========================================================================
Update_t CmdHelpList (int nArgs)
{
  (void)nArgs;
	const int nBuf = CONSOLE_WIDTH * 2;

	char sText[ nBuf ] = "";

	int nMaxWidth = g_nConsoleDisplayWidth - 1;
	int iCommand;

	extern std::vector<Command_t> g_vSortedCommands;

	if (! g_vSortedCommands.size())
	{
		for (iCommand = 0; iCommand < g_nNumCommandsWithAliases; iCommand++ )
		{
			g_vSortedCommands.push_back( g_aCommands[ iCommand ] );
		}
		std::sort( g_vSortedCommands.begin(), g_vSortedCommands.end(), commands_functor_compare() );
	}

	int nLen = 0;
//		Colorize( sText, "Commands: " );
 		        StringCat( sText, CHC_USAGE , nBuf );
		nLen += StringCat( sText, "Commands", nBuf );

		        StringCat( sText, CHC_DEFAULT, nBuf );
		nLen += StringCat( sText, ": " , nBuf );

	for( iCommand = 0; iCommand < g_nNumCommandsWithAliases; iCommand++ ) // aliases are not printed
	{
		Command_t *pCommand = & g_vSortedCommands.at( iCommand );
//		Command_t *pCommand = & g_aCommands[ iCommand ];
		char      *pName = pCommand->m_sName;

		if (! pCommand->pFunction)
			continue; // not implemented function

		int nLenCmd = strlen( pName );
		if ((nLen + nLenCmd) < (nMaxWidth))
		{
			        StringCat( sText, CHC_COMMAND, nBuf );
			nLen += StringCat( sText, pName      , nBuf );
		}
		else
		{
			ConsolePrint( sText );
			nLen = 1;
			strcpy( sText, " " );
			        StringCat( sText, CHC_COMMAND, nBuf );
			nLen += StringCat( sText, pName, nBuf );
		}

		strcat( sText, " " );
		nLen++;
	}

	//ConsoleBufferPush( sText );
	ConsolePrint( sText );
	ConsoleUpdate();

	return UPDATE_CONSOLE_DISPLAY;
}


Update_t CmdVersion (int nArgs)
{
	char sText[ CONSOLE_WIDTH ];

	unsigned int nVersion = DEBUGGER_VERSION;
	int nMajor;
	int nMinor;
	int nFixMajor;
	int nFixMinor;
	UnpackVersion( nVersion, nMajor, nMinor, nFixMajor, nFixMinor );

	ConsolePrintFormat( sText, "  Emulator:  %s%s%s    Debugger: %s%d.%d.%d.%d%s"
		, CHC_SYMBOL
		, VERSIONSTRING
		, CHC_DEFAULT
		, CHC_SYMBOL
		, nMajor, nMinor, nFixMajor, nFixMinor
		, CHC_DEFAULT
	);

	if (nArgs)
	{
		for (int iArg = 1; iArg <= g_nArgRaw; iArg++ )
		{
			// * PARAM_WILDSTAR -> ? PARAM_MEM_SEARCH_WILD
			if ((! strcmp( g_aArgs[ iArg ].sArg, g_aParameters[ PARAM_WILDSTAR        ].m_sName )) ||
				(! strcmp( g_aArgs[ iArg ].sArg, g_aParameters[ PARAM_MEM_SEARCH_WILD ].m_sName )) )
			{
				ConsoleBufferPushFormat( sText, "  Arg: %d bytes * %d = %d bytes",
					sizeof(Arg_t), MAX_ARGS, sizeof(g_aArgs) );

				ConsoleBufferPushFormat( sText, "  Console: %d bytes * %d height = %d bytes",
					sizeof( g_aConsoleDisplay[0] ), CONSOLE_HEIGHT, sizeof(g_aConsoleDisplay) );

				ConsoleBufferPushFormat( sText, "  Commands: %d   (Aliased: %d)   Params: %d",
					NUM_COMMANDS, g_nNumCommandsWithAliases, NUM_PARAMS );

				ConsoleBufferPushFormat( sText, "  Cursor(%d)  T: %04X  C: %04X  B: %04X %c D: %02X", // Top, Cur, Bot, Delta
					g_nDisasmCurLine, g_nDisasmTopAddress, g_nDisasmCurAddress, g_nDisasmBotAddress,
					g_bDisasmCurBad ? '*' : ' '
					, g_nDisasmBotAddress - g_nDisasmTopAddress
				);

				CmdConfigGetFont( 0 );

				break;
			}
			else
				return Help_Arg_1( CMD_VERSION );
		}
	}

	return ConsoleUpdate();
}

