// SPDX-License-Identifier: GPL-2.0-only

#include "Debugger_Range.h"

#include <cstdint>

#include "Debugger_Parser.h"
#include "Debugger_Types.h"
#include "apple2/Apple2Types.h"

// Util - Range _______________________________________________________________

auto Range_CalcEndLen(const RangeType_t eRange, const uint16_t& nAddress1,
                      const uint16_t& nAddress2, RangeEndLen_t& tEndLen_)
    -> bool {
  bool bValid = false;

  if (eRange == RANGE_HAS_LEN) {
    // BSAVE 2000,0  Len=0 End=n/a
    // BSAVE 2000,1  Len=1 End=2000
    // 0,FFFF [,)
    // End =  FFFE = Len-1
    // Len =  FFFF
    tEndLen_.nAddressLen = nAddress2;
    uint32_t nTemp = nAddress1 + tEndLen_.nAddressLen - 1;
    if (nTemp > APPLE2_6502_MEM_END) {
      nTemp = APPLE2_6502_MEM_END;
    }
    tEndLen_.nAddressEnd = nTemp;
    bValid = true;
  } else if (eRange == RANGE_HAS_END) {
    // BSAVE 2000:2000 Len=0, End=n/a
    // BSAVE 2000:2001 Len=1, End=2000
    // 0:FFFF [,]
    // End =  FFFF
    // Len = 10000 = End+1
    tEndLen_.nAddressEnd = nAddress2;
    tEndLen_.nAddressLen = nAddress2 - nAddress1 + 1;
    bValid = true;
  }

  return bValid;
}

auto Range_Get(uint16_t& nAddress1_, uint16_t& nAddress2_, const int iArg)
    -> RangeType_t {
  nAddress1_ = static_cast<unsigned>(g_args[iArg].nValue);
  if (nAddress1_ > APPLE2_6502_MEM_END) {
    nAddress1_ = APPLE2_6502_MEM_END;
  }

  nAddress2_ = 0;
  int nTemp = 0;

  RangeType_t eRange = RANGE_MISSING_ARG_2;

  if (g_args[iArg + 1].eToken == TOKEN_COMMA) {
    // 0,FFFF [,) // Note the mathematical range
    // End =  FFFE = Len-1
    // Len =  FFFF
    eRange = RANGE_HAS_LEN;
    nTemp = g_args[iArg + 2].nValue;
    nAddress2_ = nTemp;
  } else if (g_args[iArg + 1].eToken == TOKEN_COLON) {
    // 0:FFFF [,] // Note the mathematical range
    // End =  FFFF
    // Len = 10000 = End+1
    eRange = RANGE_HAS_END;
    nTemp = g_args[iArg + 2].nValue;

    // i.e.
    // FFFF:D000
    // 1    2    Temp
    // FFFF      D000
    //      FFFF
    // D000
    if (nAddress1_ > nTemp) {
      nAddress2_ = nAddress1_;
      nAddress1_ = nTemp;
    } else {
      nAddress2_ = nTemp;
    }
  }

  return eRange;
}
