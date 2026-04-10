#include "Common.h"
#include "Debugger_Breakpoints.h"
#include "CPU.h"
#include "Memory.h"
#include "Debug.h"
#include "Debugger_Parser.h"
#include "Debugger_Help.h"
#include "Debugger_Display.h"
#include "Debugger_Range.h"
#include "Debugger_Symbols.h"
#include "Debugger_DisassemblerData.h"
#include "Debugger_Assembler.h"
#include "Log.h"
#include <cassert>
#include <cstddef>

extern unsigned short g_uBreakMemoryAddress;
extern MemoryTextFile_t g_ConfigState;
extern const Opcodes_t *g_aOpcodes;
extern const Opcodes_t g_aOpcodes65C02[NUM_OPCODES];

bool ConfigSave_BufferToDisk ( const char *pFileName, ConfigSave_t eConfigSave );
void ConfigSave_PrepareHeader ( const Parameters_e eCategory, const Commands_e eCommandClear );

unsigned short g_uBreakMemoryAddress = 0;
// Any Speed Breakpoints
int  g_nDebugBreakOnInvalid  = 0; // Bit Flags of Invalid Opcode to break on
int  g_iDebugBreakOnOpcode   = 0;

int  g_bDebugBreakpointHit = 0;  // See: BreakpointHit_t

int  g_nBreakpoints          = 0;
Breakpoint_t g_aBreakpoints[ MAX_BREAKPOINTS ];

// NOTE: BreakpointSource_t and g_aBreakpointSource must match!
const char *g_aBreakpointSource[ NUM_BREAKPOINT_SOURCES ] =
{
  "A", "X", "Y", "PC", "S", "P", "C", "Z", "I", "D", "B", "R", "V", "N", "OP", "M", "M", "M"
};

// Note: BreakpointOperator_t, _PARAM_BREAKPOINT_, and g_aBreakpointSymbols must match!
const char *g_aBreakpointSymbols[ NUM_BREAKPOINT_OPERATORS ] =
{
  "<=", "< ", "= ", "!=", "> ", ">=", "? ", "@ ", "* "
};

bool IsDebugBreakOnInvalid(int iOpcodeType)
{
  g_bDebugBreakpointHit |= ((g_nDebugBreakOnInvalid >> iOpcodeType) & 1) ? BP_HIT_INVALID : 0;
  return g_bDebugBreakpointHit != 0;
}

void SetDebugBreakOnInvalid(int iOpcodeType, int nValue)
{
  if (iOpcodeType <= AM_3)
  {
    g_nDebugBreakOnInvalid &= ~ (          1  << iOpcodeType);
    g_nDebugBreakOnInvalid |=   ((nValue & 1) << iOpcodeType);
  }
}

Update_t CmdBreakInvalid (int nArgs) // Breakpoint IFF Full-speed!
{
  // Cases:
  // 0.  CMD            // display
  // 1a. CMD #          // display
  // 1b. CMD ON | OFF   //set
  // 1c. CMD ?          // error
  // 2a. CMD # ON | OFF // set
  // 2b. CMD # ?        // error
  char sText[ CONSOLE_WIDTH ];
  bool bValidParam = true;

  int iParamArg = nArgs;
  int iParam;
  int nFound;

  int iType = AM_IMPLIED; // default to BRK
  int nActive = 0;

  if (nArgs > 2) // || (nArgs == 0))
    goto _Help;

  if (nArgs == 0)
  {
    nArgs = 1;
    g_aArgs[ 1 ].nValue = AM_IMPLIED;
    g_aArgs[ 1 ].sArg[0] = 0;
  }

  iType = g_aArgs[ 1 ].nValue;

  nFound = FindParam( g_aArgs[ iParamArg ].sArg, MATCH_EXACT, iParam, _PARAM_GENERAL_BEGIN, _PARAM_GENERAL_END );

  if (nFound)
  {
    if (iParam == PARAM_ON)
      nActive = 1;
    else
    if (iParam == PARAM_OFF)
      nActive = 0;
    else
      bValidParam = false;
  }
  else
    bValidParam = false;

  if (nArgs == 1)
  {
    if (! nFound) // bValidParam) // case 1a or 1c
    {
      if ((iType < AM_IMPLIED) || (iType > AM_3))
        goto _Help;

      if ( IsDebugBreakOnInvalid( iType ) )
        iParam = PARAM_ON;
      else
        iParam = PARAM_OFF;
    }
    else // case 1b
    {
      SetDebugBreakOnInvalid( iType, nActive );
    }

    if (iType == 0)
      ConsoleBufferPushFormat( sText, "Enter debugger on BRK opcode: %s", g_aParameters[ iParam ].m_sName );
    else
      ConsoleBufferPushFormat( sText, "Enter debugger on INVALID %1X opcode: %s", iType, g_aParameters[ iParam ].m_sName );
    return ConsoleUpdate();
  }
  else
  if (nArgs == 2)
  {
    if (! bValidParam) // case 2b
    {
      goto _Help;
    }
    else // case 2a (or not 2b ;-)
    {
      if ((iType < 0) || (iType > AM_3))
        goto _Help;

      SetDebugBreakOnInvalid( iType, nActive );

      if (iType == 0)
        ConsoleBufferPushFormat( sText, "Enter debugger on BRK opcode: %s", g_aParameters[ iParam ].m_sName );
      else
        ConsoleBufferPushFormat( sText, "Enter debugger on INVALID %1X opcode: %s", iType, g_aParameters[ iParam ].m_sName );
      return ConsoleUpdate();
    }
  }

  return UPDATE_CONSOLE_DISPLAY;

_Help:
  return HelpLastCommand();
}


Update_t CmdBreakOpcode (int nArgs) // Breakpoint IFF Full-speed!
{
  char sText[ CONSOLE_WIDTH ];

  if (nArgs > 1)
    return HelpLastCommand();

  char sAction[ CONSOLE_WIDTH ] = "Current"; // default to display

  if (nArgs == 1)
  {
    int iOpcode = g_aArgs[ 1] .nValue;
    g_iDebugBreakOnOpcode = iOpcode & 0xFF;

    strcpy( sAction, "Setting" );

    if (iOpcode >= NUM_OPCODES)
    {
      ConsoleBufferPushFormat( sText, "Warning: clamping opcode: %02X", g_iDebugBreakOnOpcode );
      return ConsoleUpdate();
    }
  }

  if (g_iDebugBreakOnOpcode == 0)
    // Show what the current break opcode is
    ConsoleBufferPushFormat( sText, "%s Break on Opcode: None"
      , sAction
    );
  else
    // Show what the current break opcode is
    ConsoleBufferPushFormat( sText, "%s Break on Opcode: %02X %s"
      , sAction
      , g_iDebugBreakOnOpcode
      , g_aOpcodes65C02[ g_iDebugBreakOnOpcode ].sMnemonic
    );

  return ConsoleUpdate();
}


// bool bBP = g_nBreakpoints && CheckBreakpoint(nOffset,nOffset == regs.pc);
bool GetBreakpointInfo ( unsigned short nOffset, bool & bBreakpointActive_, bool & bBreakpointEnable_ )
{
  for (int iBreakpoint = 0; iBreakpoint < MAX_BREAKPOINTS; iBreakpoint++)
  {
    Breakpoint_t *pBP = &g_aBreakpoints[ iBreakpoint ];

    if ((pBP->nLength)
//       && (pBP->bEnabled) // not bSet
       && (nOffset >= pBP->nAddress) && (nOffset < (pBP->nAddress + pBP->nLength))) // [nAddress,nAddress+nLength]
    {
      bBreakpointActive_ = pBP->bSet;
      bBreakpointEnable_ = pBP->bEnabled;
      return true;
    }

//    if (g_aBreakpoints[iBreakpoint].nLength && g_aBreakpoints[iBreakpoint].bEnabled &&
//      (g_aBreakpoints[iBreakpoint].nAddress <= targetaddr) &&
//      (g_aBreakpoints[iBreakpoint].nAddress + g_aBreakpoints[iBreakpoint].nLength > targetaddr))
  }

  bBreakpointActive_ = false;
  bBreakpointEnable_ = false;

  return false;
}


// Returns true if we should continue checking breakpoint details, else false
bool _BreakpointValid( Breakpoint_t *pBP ) //, BreakpointSource_t iSrc )
{
  bool bStatus = false;

  if (! pBP->bEnabled)
    return bStatus;

//  if (pBP->eSource != iSrc)
//    return bStatus;

  if (! pBP->nLength)
    return bStatus;

  return true;
}


bool _CheckBreakpointValue( Breakpoint_t *pBP, int nVal )
{
  bool bStatus = false;

  int iCmp = pBP->eOperator;
  switch (iCmp)
  {
    case BP_OP_LESS_EQUAL   :
      if (nVal <= pBP->nAddress)
        bStatus = true;
      break;
    case BP_OP_LESS_THAN    :
      if (nVal < pBP->nAddress)
        bStatus = true;
      break;
    case BP_OP_EQUAL        : // Range is like C++ STL: [,)  (inclusive,not-inclusive)
      if ((nVal >= pBP->nAddress) && ((unsigned int)nVal < (pBP->nAddress + pBP->nLength)))
        bStatus = true;
      break;
    case BP_OP_NOT_EQUAL    : // Rnage is: (,] (not-inclusive, inclusive)
      if ((nVal < pBP->nAddress) || ((unsigned int)nVal >= (pBP->nAddress + pBP->nLength)))
        bStatus = true;
      break;
    case BP_OP_GREATER_THAN :
      if (nVal > pBP->nAddress)
        bStatus = true;
      break;
    case BP_OP_GREATER_EQUAL:
      if (nVal >= pBP->nAddress)
        bStatus = true;
      break;
    default:
      break;
  }

  return bStatus;
}


int CheckBreakpointsIO ()
{
  const int NUM_TARGETS = 3;

  int aTarget[ NUM_TARGETS ] =
  {
    NO_6502_TARGET,
    NO_6502_TARGET,
    NO_6502_TARGET
  };
  int  nBytes;
  bool bBreakpointHit = false;

  int  iTarget;
  int  nAddress;

  // bIncludeNextOpcodeAddress == false:
  // . JSR addr16: ignore addr16 as a target
  // . BRK/RTS/RTI: ignore return (or vector) addr16 as a target
  _6502_GetTargets( regs.pc, &aTarget[0], &aTarget[1], &aTarget[2], &nBytes, true, false );

  if (nBytes)
  {
    for (iTarget = 0; iTarget < NUM_TARGETS; iTarget++ )
    {
      nAddress = aTarget[ iTarget ];
      if (nAddress != NO_6502_TARGET)
      {
        for (int iBreakpoint = 0; iBreakpoint < MAX_BREAKPOINTS; iBreakpoint++)
        {
          Breakpoint_t *pBP = &g_aBreakpoints[iBreakpoint];
          if (_BreakpointValid( pBP ))
          {
            if (pBP->eSource == BP_SRC_MEM_RW || pBP->eSource == BP_SRC_MEM_READ_ONLY || pBP->eSource == BP_SRC_MEM_WRITE_ONLY)
            {
              if (_CheckBreakpointValue( pBP, nAddress ))
              {
                g_uBreakMemoryAddress = (unsigned short) nAddress;
                unsigned char opcode = mem[regs.pc];

                if (pBP->eSource == BP_SRC_MEM_RW)
                {
                  return BP_HIT_MEM;
                }
                else if (pBP->eSource == BP_SRC_MEM_READ_ONLY)
                {
                  if (g_aOpcodes[opcode].nMemoryAccess & (MEM_RI|MEM_R))
                    return BP_HIT_MEMR;
                }
                else if (pBP->eSource == BP_SRC_MEM_WRITE_ONLY)
                {
                  if (g_aOpcodes[opcode].nMemoryAccess & (MEM_WI|MEM_W))
                    return BP_HIT_MEMW;
                }
                else
                {
                  assert(0);
                }
              }
            }
          }
        }
      }
    }
  }
  return bBreakpointHit;
}

// Returns true if a register breakpoint is triggered
int CheckBreakpointsReg ()
{
  int bBreakpointHit = 0;

  for (int iBreakpoint = 0; iBreakpoint < MAX_BREAKPOINTS; iBreakpoint++)
  {
    Breakpoint_t *pBP = &g_aBreakpoints[iBreakpoint];

    if (! _BreakpointValid( pBP ))
      continue;

    switch (pBP->eSource)
    {
      case BP_SRC_REG_PC:
        bBreakpointHit = _CheckBreakpointValue( pBP, regs.pc );
        break;
      case BP_SRC_REG_A:
        bBreakpointHit = _CheckBreakpointValue( pBP, regs.a );
        break;
      case BP_SRC_REG_X:
        bBreakpointHit = _CheckBreakpointValue( pBP, regs.x );
        break;
      case BP_SRC_REG_Y:
        bBreakpointHit = _CheckBreakpointValue( pBP, regs.y );
        break;
      case BP_SRC_REG_P:
        bBreakpointHit = _CheckBreakpointValue( pBP, regs.ps );
        break;
      case BP_SRC_REG_S:
        bBreakpointHit = _CheckBreakpointValue( pBP, regs.sp );
        break;
      default:
        break;
    }

    if (bBreakpointHit)
    {
      bBreakpointHit = BP_HIT_REG;
      if (pBP->bTemp)
        _BWZ_Clear(pBP, iBreakpoint);

      break;
    }
  }

  return bBreakpointHit;
}

void ClearTempBreakpoints ()
{
  for (int iBreakpoint = 0; iBreakpoint < MAX_BREAKPOINTS; iBreakpoint++)
  {
    Breakpoint_t *pBP = &g_aBreakpoints[iBreakpoint];

    if (! _BreakpointValid( pBP ))
      continue;

    if (pBP->bTemp)
      _BWZ_Clear(pBP, iBreakpoint);
  }
}

Update_t CmdBreakpoint (int nArgs)
{
  return CmdBreakpointAddPC( nArgs );
}


// smart breakpoint
Update_t CmdBreakpointAddSmart (int nArgs)
{
  int nAddress = g_aArgs[1].nValue;

  if (! nArgs)
  {
    nArgs = 1;
    g_aArgs[ nArgs ].nValue = g_nDisasmCurAddress;
  }

  if ((nAddress >= (int) _6502_IO_BEGIN) && (nAddress <= (int) _6502_IO_END))
  {
    return CmdBreakpointAddIO( nArgs );
  }
  else
  {
    CmdBreakpointAddReg( nArgs );
    CmdBreakpointAddMem( nArgs );
    return UPDATE_BREAKPOINTS;
  }
}


Update_t CmdBreakpointAddReg (int nArgs)
{
  if (! nArgs)
  {
    return Help_Arg_1( CMD_BREAKPOINT_ADD_REG );
  }

  BreakpointSource_t   iSrc = BP_SRC_REG_PC;
  BreakpointOperator_t iCmp = BP_OP_EQUAL  ;

  bool bHaveSrc = false;
  bool bHaveCmp = false;

  int iParamSrc;
  int iParamCmp;

  int  nFound;

  int  iArg   = 0;
  while (iArg++ < nArgs)
  {
    char *sArg = g_aArgs[iArg].sArg;

    bHaveSrc = false;
    bHaveCmp = false;

    nFound = FindParam( sArg, MATCH_EXACT, iParamSrc, _PARAM_REGS_BEGIN, _PARAM_REGS_END );
    if (nFound)
    {
      switch (iParamSrc)
      {
        case PARAM_REG_A : iSrc = BP_SRC_REG_A ; bHaveSrc = true; break;
        case PARAM_FLAGS : iSrc = BP_SRC_REG_P ; bHaveSrc = true; break;
        case PARAM_REG_X : iSrc = BP_SRC_REG_X ; bHaveSrc = true; break;
        case PARAM_REG_Y : iSrc = BP_SRC_REG_Y ; bHaveSrc = true; break;
        case PARAM_REG_PC: iSrc = BP_SRC_REG_PC; bHaveSrc = true; break;
        case PARAM_REG_SP: iSrc = BP_SRC_REG_S ; bHaveSrc = true; break;
        default:
          break;
      }
    }

    nFound = FindParam( sArg, MATCH_EXACT, iParamCmp, _PARAM_BREAKPOINT_BEGIN, _PARAM_BREAKPOINT_END );
    if (nFound)
    {
      switch (iParamCmp)
      {
        case PARAM_BP_LESS_EQUAL   : iCmp = BP_OP_LESS_EQUAL   ; break;
        case PARAM_BP_LESS_THAN    : iCmp = BP_OP_LESS_THAN    ; break;
        case PARAM_BP_EQUAL        : iCmp = BP_OP_EQUAL        ; break;
        case PARAM_BP_NOT_EQUAL    : iCmp = BP_OP_NOT_EQUAL    ; break;
        case PARAM_BP_NOT_EQUAL_1  : iCmp = BP_OP_NOT_EQUAL    ; break;
        case PARAM_BP_GREATER_THAN : iCmp = BP_OP_GREATER_THAN ; break;
        case PARAM_BP_GREATER_EQUAL: iCmp = BP_OP_GREATER_EQUAL; break;
        default:
          break;
      }
    }

    if ((! bHaveSrc) && (! bHaveCmp)) // Inverted/Convoluted logic: didn't find BOTH this pass, so we must have already found them.
    {
      int dArgs = _CmdBreakpointAddCommonArg( iArg, nArgs, iSrc, iCmp );
      if (!dArgs)
      {
        return Help_Arg_1( CMD_BREAKPOINT_ADD_REG );
      }
      iArg += dArgs;
    }
  }

  return UPDATE_ALL;
}


bool _CmdBreakpointAddReg( Breakpoint_t *pBP, BreakpointSource_t iSrc, BreakpointOperator_t iCmp, unsigned short nAddress, int nLen, bool bIsTempBreakpoint )
{
  bool bStatus = false;

  if (pBP)
  {
    assert((unsigned int)nLen <= _6502_MEM_LEN);
    if (nLen > (int) _6502_MEM_LEN) nLen = (int) _6502_MEM_LEN;

    pBP->eSource   = iSrc;
    pBP->eOperator = iCmp;
    pBP->nAddress  = nAddress;
    pBP->nLength   = nLen;
    pBP->bSet      = true;
    pBP->bEnabled  = true;
    pBP->bTemp     = bIsTempBreakpoint;
    bStatus = true;
  }

  return bStatus;
}


// @return Number of args processed
int _CmdBreakpointAddCommonArg ( int iArg, int nArg, BreakpointSource_t iSrc, BreakpointOperator_t iCmp, bool bIsTempBreakpoint )
{
  int dArg = 0;

  int iBreakpoint = 0;
  Breakpoint_t *pBP = & g_aBreakpoints[ iBreakpoint ];

  while ((iBreakpoint < MAX_BREAKPOINTS) && g_aBreakpoints[iBreakpoint].bSet) //g_aBreakpoints[iBreakpoint].nLength)
  {
    iBreakpoint++;
    pBP++;
  }

  if (iBreakpoint >= MAX_BREAKPOINTS)
  {
    ConsoleDisplayError("All Breakpoints slots are currently in use.");
    return dArg;
  }

  if (iArg <= nArg)
  {
#if DEBUG_VAL_2
    int nLen = g_aArgs[iArg].nVal2;
#endif
    unsigned short nAddress  = 0;
    unsigned short nAddress2 = 0;
    unsigned short nEnd      = 0;
    int  nLen      = 0;

    dArg = 1;
    RangeType_t eRange = Range_Get( nAddress, nAddress2, iArg);
    if ((eRange == RANGE_HAS_END) ||
      (eRange == RANGE_HAS_LEN))
    {
      Range_CalcEndLen( eRange, nAddress, nAddress2, nEnd, nLen );
      dArg = 2;
    }

    if ( !nLen)
    {
      nLen = 1;
    }

    if (! _CmdBreakpointAddReg( pBP, iSrc, iCmp, nAddress, nLen, bIsTempBreakpoint ))
    {
      dArg = 0;
    }
    g_nBreakpoints++;
  }

  return dArg;
}


Update_t CmdBreakpointAddPC (int nArgs)
{
  BreakpointSource_t   iSrc = BP_SRC_REG_PC;
  BreakpointOperator_t iCmp = BP_OP_EQUAL  ;

  if (!nArgs)
  {
    nArgs = 1;
//    g_aArgs[1].nValue = regs.pc;
    g_aArgs[1].nValue = g_nDisasmCurAddress;
  }

//  int iParamSrc;
  int iParamCmp;

  int  nFound = 0;

  int  iArg   = 0;
  while (iArg++ < nArgs)
  {
    char *sArg = g_aArgs[iArg].sArg;

    if (g_aArgs[iArg].bType & TYPE_OPERATOR)
    {
      nFound = FindParam( sArg, MATCH_EXACT, iParamCmp, _PARAM_BREAKPOINT_BEGIN, _PARAM_BREAKPOINT_END );
      if (nFound)
      {
        switch (iParamCmp)
        {
          case PARAM_BP_LESS_EQUAL   : iCmp = BP_OP_LESS_EQUAL   ; break;
          case PARAM_BP_LESS_THAN    : iCmp = BP_OP_LESS_THAN    ; break;
          case PARAM_BP_EQUAL        : iCmp = BP_OP_EQUAL        ; break;
          case PARAM_BP_NOT_EQUAL    : iCmp = BP_OP_NOT_EQUAL    ; break;
          case PARAM_BP_GREATER_THAN : iCmp = BP_OP_GREATER_THAN ; break;
          case PARAM_BP_GREATER_EQUAL: iCmp = BP_OP_GREATER_EQUAL; break;
          default:
            break;
        }
      }
    }
    else
    {
      int dArg = _CmdBreakpointAddCommonArg( iArg, nArgs, iSrc, iCmp );
      if (! dArg)
      {
        return Help_Arg_1( CMD_BREAKPOINT_ADD_PC );
      }
      iArg += dArg;
    }
  }

  return UPDATE_BREAKPOINTS | UPDATE_CONSOLE_DISPLAY; // 1;
}


Update_t CmdBreakpointAddIO   (int nArgs)
{
  return CmdBreakpointAddMem( nArgs );
}

Update_t CmdBreakpointAddMemA(int nArgs)
{
  return CmdBreakpointAddMem(nArgs);
}
Update_t CmdBreakpointAddMemR(int nArgs)
{
  return CmdBreakpointAddMem(nArgs, BP_SRC_MEM_READ_ONLY);
}
Update_t CmdBreakpointAddMemW(int nArgs)
{
  return CmdBreakpointAddMem(nArgs, BP_SRC_MEM_WRITE_ONLY);
}
Update_t CmdBreakpointAddMem  (int nArgs, BreakpointSource_t bpSrc /*= BP_SRC_MEM_RW*/)
{
  BreakpointSource_t   iSrc = bpSrc;
  BreakpointOperator_t iCmp = BP_OP_EQUAL;

  int iArg = 0;

  while (iArg++ < nArgs)
  {
    if (g_aArgs[iArg].bType & TYPE_OPERATOR)
    {
      return Help_Arg_1( CMD_BREAKPOINT_ADD_MEM );
    }
    else
    {
      int dArg = _CmdBreakpointAddCommonArg( iArg, nArgs, iSrc, iCmp );
      if (! dArg)
      {
        return Help_Arg_1( CMD_BREAKPOINT_ADD_MEM );
      }
      iArg += dArg;
    }
  }

  return UPDATE_BREAKPOINTS | UPDATE_CONSOLE_DISPLAY;
}


void _BWZ_Clear( Breakpoint_t * aBreakWatchZero, int iSlot )
{
  aBreakWatchZero[ iSlot ].bSet     = false;
  aBreakWatchZero[ iSlot ].bEnabled = false;
  aBreakWatchZero[ iSlot ].nLength  = 0;
}

void _BWZ_RemoveOne( Breakpoint_t *aBreakWatchZero, const int iSlot, int & nTotal )
{
  if (aBreakWatchZero[iSlot].bSet)
  {
    _BWZ_Clear( aBreakWatchZero, iSlot );
    nTotal--;
  }
}

void _BWZ_RemoveAll( Breakpoint_t *aBreakWatchZero, const int nMax, int & nTotal )
{
  for( int iSlot = 0; iSlot < nMax; iSlot++ )
  {
    _BWZ_RemoveOne( aBreakWatchZero, iSlot, nTotal );
  }
}

// called by BreakpointsClear, WatchesClear, ZeroPagePointersClear
void _BWZ_ClearViaArgs( int nArgs, Breakpoint_t * aBreakWatchZero, const int nMax, int & nTotal )
{
  int iSlot = 0;

  // Clear specified breakpoints
  while (nArgs)
  {
    iSlot = g_aArgs[nArgs].nValue;

    if (! strcmp(g_aArgs[nArgs].sArg, g_aParameters[ PARAM_WILDSTAR ].m_sName))
    {
      _BWZ_RemoveAll( aBreakWatchZero, nMax, nTotal );
      break;
    }
    else
    if ((iSlot >= 0) && (iSlot < nMax))
    {
      _BWZ_RemoveOne( aBreakWatchZero, iSlot, nTotal );
    }

    nArgs--;
  }
}

// called by BreakpointsEnable, WatchesEnable, ZeroPagePointersEnable
// called by BreakpointsDisable, WatchesDisable, ZeroPagePointersDisable
void _BWZ_EnableDisableViaArgs( int nArgs, Breakpoint_t * aBreakWatchZero, const int nMax, const bool bEnabled )
{
  int iSlot = 0;

  // Enable each breakpoint in the list
  while (nArgs)
  {
    iSlot = g_aArgs[nArgs].nValue;

    if (! strcmp(g_aArgs[nArgs].sArg, g_aParameters[ PARAM_WILDSTAR ].m_sName))
    {
      for( ; iSlot < nMax; iSlot++ )
      {
        aBreakWatchZero[ iSlot ].bEnabled = bEnabled;
      }
    }
    else
    if ((iSlot >= 0) && (iSlot < nMax))
    {
      aBreakWatchZero[ iSlot ].bEnabled = bEnabled;
    }

    nArgs--;
  }
}

Update_t CmdBreakpointClear (int nArgs)
{
  if (!g_nBreakpoints)
    return ConsoleDisplayError("There are no breakpoints defined.");

  if (!nArgs)
  {
    _BWZ_RemoveAll( g_aBreakpoints, MAX_BREAKPOINTS, g_nBreakpoints );
  }
  else
  {
    _BWZ_ClearViaArgs( nArgs, g_aBreakpoints, MAX_BREAKPOINTS, g_nBreakpoints );
  }

  return UPDATE_DISASM | UPDATE_BREAKPOINTS | UPDATE_CONSOLE_DISPLAY;
}

Update_t CmdBreakpointDisable (int nArgs)
{
  if (! g_nBreakpoints)
    return ConsoleDisplayError("There are no PC Breakpoints defined.");

  if (! nArgs)
    return Help_Arg_1( CMD_BREAKPOINT_DISABLE );

  _BWZ_EnableDisableViaArgs( nArgs, g_aBreakpoints, MAX_BREAKPOINTS, false );

  return UPDATE_BREAKPOINTS;
}

//===========================================================================
Update_t CmdBreakpointEdit (int nArgs)
{
  (void)nArgs;
  return (UPDATE_DISASM | UPDATE_BREAKPOINTS);
}


//===========================================================================
Update_t CmdBreakpointEnable (int nArgs) {

  if (! g_nBreakpoints)
    return ConsoleDisplayError("There are no PC Breakpoints defined.");

  if (! nArgs)
    return Help_Arg_1( CMD_BREAKPOINT_ENABLE );

  _BWZ_EnableDisableViaArgs( nArgs, g_aBreakpoints, MAX_BREAKPOINTS, true );

  return UPDATE_BREAKPOINTS;
}


void _BWZ_List( const Breakpoint_t * aBreakWatchZero, const int iBWZ ) //, bool bZeroBased )
{
  static       char sText[ CONSOLE_WIDTH ];
  static const char sFlags[] = "-*";
  static       char sName[ MAX_SYMBOLS_LEN+1 ];

  unsigned short nAddress = aBreakWatchZero[ iBWZ ].nAddress;
  const char*  pSymbol = GetSymbol( nAddress, 2 );
  if (! pSymbol)
  {
    sName[0] = 0;
    pSymbol = sName;
  }

  char cBPM = aBreakWatchZero[iBWZ].eSource == BP_SRC_MEM_READ_ONLY ? 'R'
        : aBreakWatchZero[iBWZ].eSource == BP_SRC_MEM_WRITE_ONLY ? 'W'
        : ' ';

  ConsoleBufferPushFormat( sText, "  #%d %c %04X %c %s",
//    (bZeroBased ? iBWZ + 1 : iBWZ),
    iBWZ,
    sFlags[ (int) aBreakWatchZero[ iBWZ ].bEnabled ],
    aBreakWatchZero[ iBWZ ].nAddress,
    cBPM,
    pSymbol
  );
}

void _BWZ_ListAll( const Breakpoint_t * aBreakWatchZero, const int nMax )
{
  int iBWZ = 0;
  while (iBWZ < nMax) //
  {
    if (aBreakWatchZero[ iBWZ ].bSet)
    {
      _BWZ_List( aBreakWatchZero, iBWZ );
    }
    iBWZ++;
  }
}

//===========================================================================
Update_t CmdBreakpointList (int nArgs)
{
  (void)nArgs;
//  ConsoleBufferPush( );
//  std::vector<int> vBreakpoints;
//  int iBreakpoint = MAX_BREAKPOINTS;
//  while (iBreakpoint--)
//  {
//    if (g_aBreakpoints[iBreakpoint].enabled)
//    {
//      vBreakpoints.push_back( g_aBreakpoints[iBreakpoint].address );
//    }
//  }
//  std::sort( vBreakpoints.begin(), vBreakpoints.end() );
//  iBreakpoint = vBreakPoints.size();

  if (! g_nBreakpoints)
  {
    char sText[ CONSOLE_WIDTH ];
    ConsoleBufferPushFormat( sText, "  There are no current breakpoints.  (Max: %d", MAX_BREAKPOINTS );
  }
  else
  {
    _BWZ_ListAll( g_aBreakpoints, MAX_BREAKPOINTS );
  }
  return ConsoleUpdate();
}

//===========================================================================
Update_t CmdBreakpointLoad (int nArgs)
{
  (void)nArgs;
  return UPDATE_ALL;
}


//===========================================================================
Update_t CmdBreakpointSave (int nArgs)
{
  char sText[ CONSOLE_WIDTH ];

  g_ConfigState.Reset();

  ConfigSave_PrepareHeader( PARAM_CAT_BREAKPOINTS, CMD_BREAKPOINT_CLEAR );

  int iBreakpoint = 0;
  while (iBreakpoint < MAX_BREAKPOINTS)
  {
    if (g_aBreakpoints[ iBreakpoint ].bSet)
    {
      sprintf( sText, "%s %x %04X,%04X\n"
        , g_aCommands[ CMD_BREAKPOINT_ADD_REG ].m_sName
        , iBreakpoint
        , g_aBreakpoints[ iBreakpoint ].nAddress
        , g_aBreakpoints[ iBreakpoint ].nLength
      );
      g_ConfigState.PushLine( sText );
    }
    if (! g_aBreakpoints[ iBreakpoint ].bEnabled)
    {
      sprintf( sText, "%s %x\n"
        , g_aCommands[ CMD_BREAKPOINT_DISABLE ].m_sName
        , iBreakpoint
      );
      g_ConfigState.PushLine( sText );
    }

    iBreakpoint++;
  }

  if (nArgs)
  {
    if (! (g_aArgs[ 1 ].bType & TYPE_QUOTED_2))
      return Help_Arg_1( CMD_BREAKPOINT_SAVE );

    if (ConfigSave_BufferToDisk( g_aArgs[ 1 ].sArg, CONFIG_SAVE_FILE_CREATE ))
    {
      ConsoleBufferPush(  "Saved."  );
      return ConsoleUpdate();
    }
  }

  return UPDATE_CONSOLE_DISPLAY;
}
// Breakpoints ____________________________________________________________________________________
Update_t CmdWatch (int nArgs)
{
  return CmdWatchAdd( nArgs );
}


//===========================================================================
Update_t CmdWatchAdd (int nArgs)
{
  // WA [adddress]
  // WA # address
  if (! nArgs)
  {
    return CmdWatchList( 0 );
  }

  int iArg = 1;
  int iWatch = NO_6502_TARGET;
  if (nArgs > 1)
  {
    iWatch = g_aArgs[ 1 ].nValue;
    iArg++;
  }

  bool bAdded = false;
  for (; iArg <= nArgs; iArg++ )
  {
    unsigned short nAddress = g_aArgs[iArg].nValue;

    // Make sure address isn't an IO address
    if ((nAddress >= _6502_IO_BEGIN) && (nAddress <= _6502_IO_END))
      return ConsoleDisplayError("You may not watch an I/O location.");

    if (iWatch == NO_6502_TARGET)
    {
      iWatch = 0;
      while ((iWatch < MAX_WATCHES) && (g_aWatches[iWatch].bSet))
      {
        iWatch++;
      }
    }

    if ((iWatch >= MAX_WATCHES) && !bAdded)
    {
      char sText[ CONSOLE_WIDTH ];
      sprintf( sText, "All watches are currently in use.  (Max: %d)", MAX_WATCHES );
      ConsoleDisplayPush( sText );
      return ConsoleUpdate();
    }

    if ((iWatch < MAX_WATCHES) && (g_nWatches < MAX_WATCHES))
    {
      g_aWatches[iWatch].bSet = true;
      g_aWatches[iWatch].bEnabled = true;
      g_aWatches[iWatch].nAddress = (unsigned short) nAddress;
      bAdded = true;
      g_nWatches++;
      iWatch++;
    }
  }

  if (!bAdded)
    goto _Help;

  return UPDATE_WATCH;

_Help:
  return Help_Arg_1( CMD_WATCH_ADD );
}

//===========================================================================
Update_t CmdWatchClear (int nArgs)
{
  if (!g_nWatches)
    return ConsoleDisplayError("There are no watches defined.");

  if (!nArgs)
    return Help_Arg_1( CMD_WATCH_CLEAR );

  _BWZ_ClearViaArgs( nArgs, g_aWatches, MAX_WATCHES, g_nWatches );

//  if (! g_nWatches)
//  {
//    UpdateDisplay(UPDATE_BACKGROUND); // 1
//    return UPDATE_NOTHING; // 0
//  }

  return UPDATE_CONSOLE_DISPLAY | UPDATE_WATCH; // 1
}

//===========================================================================
Update_t CmdWatchDisable (int nArgs)
{
  if (! g_nWatches)
    return ConsoleDisplayError("There are no watches defined.");

  if (!nArgs)
    return Help_Arg_1( CMD_WATCH_DISABLE );

  _BWZ_EnableDisableViaArgs( nArgs, g_aWatches, MAX_WATCHES, false );

  return UPDATE_WATCH;
}

//===========================================================================
Update_t CmdWatchEnable (int nArgs)
{
  if (! g_nWatches)
    return ConsoleDisplayError("There are no watches defined.");

  if (!nArgs)
    return Help_Arg_1( CMD_WATCH_ENABLE );

  _BWZ_EnableDisableViaArgs( nArgs, g_aWatches, MAX_WATCHES, true );

  return UPDATE_WATCH;
}

//===========================================================================
Update_t CmdWatchList (int nArgs)
{
  (void)nArgs;
  if (! g_nWatches)
  {
    char sText[ CONSOLE_WIDTH ];
    ConsoleBufferPushFormat( sText, "  There are no current watches.  (Max: %d", MAX_WATCHES );
  }
  else
  {
//    _BWZ_List( g_aWatches, MAX_WATCHES );
    _BWZ_ListAll( g_aWatches, MAX_WATCHES );
  }
  return ConsoleUpdate();
}

/*
//===========================================================================
Update_t CmdWatchLoad (int nArgs)
{
  if (!nArgs)
    return Help_Arg_1( CMD_WATCH_LOAD );

  return UPDATE_CONSOLE_DISPLAY;
}
*/

//===========================================================================
Update_t CmdWatchSave (int nArgs)
{
  if (!nArgs)
    return Help_Arg_1( CMD_WATCH_SAVE );

  return UPDATE_CONSOLE_DISPLAY;
}


// Window _________________________________________________________________________________________

//===========================================================================
