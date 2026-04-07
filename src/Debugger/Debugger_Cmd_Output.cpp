#include "stdafx.h"
#include <vector>
#include "Debugger_Cmd_Output.h"
#include "Debug.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Help.h"
#include "Debugger_Display.h"
#include "Log.h"
#include "Video.h"
#include "Util_Text.h"
#include "Util_MemoryTextFile.h"

// Globals originally from Debug.cpp
extern bool g_bReportMissingScripts;

// Types used by CmdOutputPrint and CmdOutputPrintf
enum PrintState_e
{
  PS_LITERAL
  , PS_ESCAPE
  , PS_NEXT_ARG_BIN
  , PS_NEXT_ARG_HEX
  , PS_NEXT_ARG_DEC
  , PS_NEXT_ARG_CHR
};

struct PrintFormat_t
{
  int nValue;
  int eType;
};

// Implementation

//===========================================================================
void DebuggerRunScript(const char* pFileName)
{
  if (!pFileName || !*pFileName)
    return;

  MemoryTextFile_t script;
  std::string sFileName;

  if (pFileName[0] == '/')
  {
    sFileName = pFileName;
  }
  else
  {
    sFileName = g_state.sCurrentDir;
    sFileName += "/";
    sFileName += pFileName;
  }

  if (g_pConsoleInput == nullptr)
  {
    ConsoleInputReset();
  }

  if (script.Read(sFileName))
  {
    int nLine = script.GetNumLines();
    for (int iLine = 0; iLine < nLine; iLine++)
    {
      if (g_pConsoleInput == nullptr) {
        break;
      }
      script.GetLine(iLine, g_pConsoleInput, CONSOLE_WIDTH - 2);
      g_nConsoleInputChars = (int)strlen(g_pConsoleInput);
      DebuggerProcessCommand(false);
    }
  }
  else if (g_bReportMissingScripts)
  {
    char sText[CONSOLE_WIDTH];
    ConsolePrintFormat(sText, "%sCouldn't load filename:", CHC_ERROR);
    ConsolePrintFormat(sText, "%s%s", CHC_STRING, sFileName.c_str());
  }
}

//===========================================================================
Update_t CmdOutputCalc (int nArgs)
{
  if (! nArgs)
    return Help_Arg_1( CMD_OUTPUT_CALC );

  unsigned short nAddress = g_aArgs[1].nValue;
  char sText [ CONSOLE_WIDTH ];

  bool bHi = false;
  bool bLo = false;
  char c = FormatChar4Font( (unsigned char) nAddress, &bHi, &bLo );
  bool bParen = bHi || bLo;

  int nBit = 0;
  int nWidth = 8;
  nAddress &= 0xFF;

  char sBin[ 16 ] = "";
  for (nBit = 0; nBit < nWidth; nBit++ )
  {
    sBin[ nWidth - 1 - nBit ] = (nAddress & (1 << nBit)) ? '1' : '0';
  }

  sprintf( sText, "  $%02X = %3d = %%%s", nAddress, nAddress, sBin );

  if (bParen)
    strcat( sText, " (" );

  if (bParen)
  {
    int nLen = (int)strlen( sText );
    sText[ nLen ] = c;
    sText[ nLen + 1 ] = 0;
  }

  if (bHi)
    strcat( sText, "High" );
  else
  if (bLo)
    strcat( sText, "Ctrl" );

  if (bParen)
    strcat( sText, ")" );

  ConsoleBufferPush( sText );

  return ConsoleUpdate();
}

//===========================================================================
Update_t CmdOutputEcho (int nArgs)
{
  (void)nArgs;
  if (g_aArgs[1].bType & TYPE_QUOTED_2)
  {
    ConsoleDisplayPush( g_aArgs[1].sArg );
  }
  else
  {
    const char *pText = g_pConsoleFirstArg; // ConsoleInputPeek();
    if (pText)
    {
      ConsoleDisplayPush( pText );
    }
  }

  return ConsoleUpdate();
}

//===========================================================================
Update_t CmdOutputPrint (int nArgs)
{
  // PRINT "A:",A," X:",X
  char sText[ CONSOLE_WIDTH ] = "";
  int nLen = 0;

  unsigned short nValue;

  if (! nArgs)
    goto _Help;

  int iArg;
  for (iArg = 1; iArg <= nArgs; iArg++ )
  {
    if (g_aArgs[ iArg ].bType & TYPE_QUOTED_2)
    {
      nLen += StringCat( sText, g_aArgs[ iArg ].sArg, CONSOLE_WIDTH );
      continue;
    }

    if (! ArgsGetValue( & g_aArgs[ iArg ], & nValue ))
    {
      goto _Help;
    }

    nLen += sprintf( &sText[ nLen ], "%d", nValue );
  }

  if (nLen)
    ConsoleBufferPush( sText );

  return ConsoleUpdate();

_Help:
  return Help_Arg_1( CMD_OUTPUT_PRINT );
}

//===========================================================================
Update_t CmdOutputPrintf (int nArgs)
{
  // PRINTF "A:%d X:%d",A,X
  // PRINTF "Hex:%x  Dec:%d  Bin:%z",A,A,A

  char sText[ CONSOLE_WIDTH ] = "";

  std::vector<Arg_t> aValues;
  int iValue = 0;
  unsigned short nValue = 0;
  int nParamValues;
  int nWidth = 0;
  int nLen = 0;
  PrintState_e eThis = PS_LITERAL;
  int iArg;

  if (nArgs < 1)
    goto _Help;

  if (! (g_aArgs[ 1 ].bType & TYPE_QUOTED_2))
    goto _Help;

  nParamValues = nArgs - 1;

  for (iArg = 2; iArg <= nArgs; iArg++ )
  {
    aValues.push_back( g_aArgs[ iArg ] );
  }

  const char *pFormat;
  pFormat = g_aArgs[ 1 ].sArg;

  while (*pFormat)
  {
    char c = *pFormat++;

    switch (eThis)
    {
      case PS_LITERAL:
        if (c == '%')
          eThis = PS_ESCAPE;
        else
        {
          sText[ nLen++ ] = c;
        }
        break;

      case PS_ESCAPE:
        nWidth = 0;
        if ((c >= '0') && (c <= '9'))
        {
          nWidth = c - '0';
          c = *pFormat++;
        }

        switch (toupper(c))
        {
          case 'X': eThis = PS_NEXT_ARG_HEX; break;
          case 'D': eThis = PS_NEXT_ARG_DEC; break;
          case 'Z': eThis = PS_NEXT_ARG_BIN; break;
          case 'C': eThis = PS_NEXT_ARG_CHR; break;
          default:
            sText[ nLen++ ] = c;
            eThis = PS_LITERAL;
            break;
        }

        if (eThis != PS_LITERAL)
        {
          if (iValue >= nParamValues)
          {
            goto _Help;
          }

          if (! ArgsGetValue( & aValues.at( iValue ), & nValue ))
          {
            goto _Help;
          }

          char sFormat[ 16 ];
          char sValue [ CONSOLE_WIDTH ];

          switch (eThis)
          {
            case PS_NEXT_ARG_HEX:
              if (nWidth)
                sprintf( sFormat, "%%0%dX", nWidth );
              else
                sprintf( sFormat, "%%X" );

              sprintf( sValue, sFormat, nValue );
              break;

            case PS_NEXT_ARG_DEC:
              if (nWidth)
                sprintf( sFormat, "%%%dd", nWidth );
              else
                sprintf( sFormat, "%%d" );

              sprintf( sValue, sFormat, nValue );
              break;

            case PS_NEXT_ARG_BIN:
              {
                int nBit;
                if (! nWidth)
                  nWidth = 8;

                for (nBit = 0; nBit < nWidth; nBit++ )
                {
                  sValue[ nWidth - 1 - nBit ] = (nValue & (1 << nBit)) ? '1' : '0';
                }
                sValue[ nWidth ] = 0;
              }
              break;

            case PS_NEXT_ARG_CHR:
              sValue[ 0 ] = (char) nValue;
              sValue[ 1 ] = 0;
              break;

            default:
              break;
          }

          nLen += StringCat( sText, sValue, CONSOLE_WIDTH );

          iValue++;
          eThis = PS_LITERAL;
        }
        break;

      default:
        break;
    }

    if (nLen >= (CONSOLE_WIDTH - 1))
      break;
  }

  if (nLen)
    ConsoleBufferPush( sText );

  return ConsoleUpdate();

_Help:
  return Help_Arg_1( CMD_OUTPUT_PRINTF );
}

//===========================================================================
Update_t CmdOutputRun(int nArgs)
{
  if (!nArgs)
    return Help_Arg_1(CMD_OUTPUT_RUN);

  if (nArgs != 1)
    return Help_Arg_1(CMD_OUTPUT_RUN);

  DebuggerRunScript(g_aArgs[1].sArg);

  return ConsoleUpdate();
}
