#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include <vector>
#include <string>
#include "Debugger_Cmd_Output.h"
#include "Debug.h"
#include "Debugger_Console.h"
#include "Debugger_Parser.h"
#include "Debugger_Help.h"
#include "Debugger_Display.h"
#include "core/Log.h"
#include "Video.h"
#include "core/Util_Text.h"
#include "Util_MemoryTextFile.h"
#include <cstddef>

// Globals originally from Debug.cpp
extern bool g_report_missing_scripts;

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
  if (!pFileName || !*pFileName) {
    return;
}

  MemoryTextFile_t script;
  std::string sFileName;

  if (pFileName[0] == '/')
  {
    sFileName = pFileName;
  }
  else
  {
    sFileName = g_state.current_dir.data();
    sFileName += "/";
    sFileName += pFileName;
  }

  if (g_console_input_ptr == nullptr)
  {
    ConsoleInputReset();
  }

  if (script.Read(sFileName))
  {
    int nLine = script.GetNumLines();
    for (int iLine = 0; iLine < nLine; iLine++)
    {
      if (g_console_input_ptr == nullptr) {
        break;
      }
      script.GetLine(iLine, g_console_input_ptr, CONSOLE_WIDTH - 2);
      g_console_input_chars = static_cast<int>(strlen(g_console_input_ptr));
      DebuggerProcessCommand(false);
    }
  }
  else if (g_report_missing_scripts)
  {
    char sText[CONSOLE_WIDTH];
    ConsolePrintFormat(sText, "%sCouldn't load filename:", CHC_ERROR);
    ConsolePrintFormat(sText, "%s%s", CHC_STRING, sFileName.c_str());
  }
}

//===========================================================================
auto CmdOutputCalc (int nArgs) -> Update_t
{
  if (! nArgs) {
    return Help_Arg_1( CMD_OUTPUT_CALC );
}

  uint16_t address = g_args[1].nValue;
  char sText [ CONSOLE_WIDTH ];

  bool bHi = false;
  bool bLo = false;
  char c = FormatChar4Font( static_cast<uint8_t>(address), &bHi, &bLo );
  bool bParen = bHi || bLo;

  int nBit = 0;
  int nWidth = 8;
  address &= 0xFF;

  char sBin[ 16 ] = "";
  for (nBit = 0; nBit < nWidth; nBit++ )
  {
    sBin[ nWidth - 1 - nBit ] = (address & (1 << nBit)) ? '1' : '0';
  }

  sprintf( sText, "  $%02X = %3d = %%%s", address, address, sBin );

  if (bParen) {
    strcat( sText, " (" );
}

  if (bParen)
  {
    int nLen = static_cast<int>(strlen( sText ));
    sText[ nLen ] = c;
    sText[ nLen + 1 ] = 0;
  }

  if (bHi) {
    strcat( sText, "High" );
  } else
  if (bLo) {
    strcat( sText, "Ctrl" );
}

  if (bParen) {
    strcat( sText, ")" );
}

  ConsoleBufferPush( sText );

  return ConsoleUpdate();
}

//===========================================================================
auto CmdOutputEcho (int nArgs) -> Update_t
{
  (void)nArgs;
  if (g_args[1].bType & TYPE_QUOTED_2)
  {
    ConsoleDisplayPush( g_args[1].sArg );
  }
  else
  {
    const char *text = g_console_first_arg; // ConsoleInputPeek();
    if (text)
    {
      ConsoleDisplayPush( text );
    }
  }

  return ConsoleUpdate();
}

//===========================================================================
auto CmdOutputPrint (int nArgs) -> Update_t
{
  // PRINT "A:",A," X:",X
  char sText[ CONSOLE_WIDTH ] = "";
  int nLen = 0;

  uint16_t nValue = 0;
  int iArg = 0;

  if (! nArgs) {
    return Help_Arg_1( CMD_OUTPUT_PRINT );
  }

  for (iArg = 1; iArg <= nArgs; iArg++ )
  {
    if (g_args[ iArg ].bType & TYPE_QUOTED_2)
    {
      nLen += StringCat( sText, g_args[ iArg ].sArg, CONSOLE_WIDTH );
      continue;
    }

    if (! ArgsGetValue( & g_args[ iArg ], & nValue ))
    {
      return Help_Arg_1( CMD_OUTPUT_PRINT );
    }

    nLen += sprintf( &sText[ nLen ], "%d", nValue );
  }

  if (nLen) {
    ConsoleBufferPush( sText );
  }

  return ConsoleUpdate();
}

//===========================================================================
auto CmdOutputPrintf (int nArgs) -> Update_t
{
  // PRINTF "A:%d X:%d",A,X
  // PRINTF "Hex:%x  Dec:%d  Bin:%z",A,A,A

  char sText[ CONSOLE_WIDTH ] = "";

  std::vector<Arg_t> aValues;
  int iValue = 0;
  uint16_t nValue = 0;
  int nParamValues = 0;
  int nWidth = 0;
  int nLen = 0;
  PrintState_e eThis = PS_LITERAL;
  int iArg = 0;
  const char *pFormat = nullptr;

  if (nArgs < 1) {
    return Help_Arg_1( CMD_OUTPUT_PRINTF );
  }

  if (! (g_args[ 1 ].bType & TYPE_QUOTED_2)) {
    return Help_Arg_1( CMD_OUTPUT_PRINTF );
  }

  nParamValues = nArgs - 1;

  for (iArg = 2; iArg <= nArgs; iArg++ )
  {
    aValues.push_back( g_args[ iArg ] );
  }

  pFormat = g_args[ 1 ].sArg;

  while (*pFormat)
  {
    char c = *pFormat++;

    switch (eThis)
    {
      case PS_LITERAL:
        if (c == '%') {
          eThis = PS_ESCAPE;
        } else
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
            return Help_Arg_1( CMD_OUTPUT_PRINTF );
          }

          if (! ArgsGetValue( & aValues.at( iValue ), & nValue ))
          {
            return Help_Arg_1( CMD_OUTPUT_PRINTF );
          }

          char sFormat[ 16 ];
          char sValue [ CONSOLE_WIDTH ];

          switch (eThis)
          {
            case PS_NEXT_ARG_HEX:
              if (nWidth) {
                sprintf( sFormat, "%%0%dX", nWidth );
              } else {
                sprintf( sFormat, "%%X" );
}

              sprintf( sValue, sFormat, nValue );
              break;

            case PS_NEXT_ARG_DEC:
              if (nWidth) {
                sprintf( sFormat, "%%%dd", nWidth );
              } else {
                sprintf( sFormat, "%%d" );
}

              sprintf( sValue, sFormat, nValue );
              break;

            case PS_NEXT_ARG_BIN:
              {
                int nBit = 0;
                if (! nWidth) {
                  nWidth = 8;
}

                for (nBit = 0; nBit < nWidth; nBit++ )
                {
                  sValue[ nWidth - 1 - nBit ] = (nValue & (1 << nBit)) ? '1' : '0';
                }
                sValue[ nWidth ] = 0;
              }
              break;

            case PS_NEXT_ARG_CHR:
              sValue[ 0 ] = static_cast<char>(nValue);
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

    if (nLen >= (CONSOLE_WIDTH - 1)) {
      break;
}
  }

  if (nLen) {
    ConsoleBufferPush( sText );
  }

  return ConsoleUpdate();
}

//===========================================================================
auto CmdOutputRun(int nArgs) -> Update_t
{
  if (!nArgs) {
    return Help_Arg_1(CMD_OUTPUT_RUN);
}

  if (nArgs != 1) {
    return Help_Arg_1(CMD_OUTPUT_RUN);
}

  DebuggerRunScript(g_args[1].sArg);

  return ConsoleUpdate();
}
