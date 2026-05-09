#include <cstdint>
#pragma once

typedef struct tagSS_CARD_MOCKINGBOARD SS_CARD_MOCKINGBOARD;

extern bool g_bMBTimerIrqActive;
extern uint32_t g_uTimer1IrqCount;

enum eSOUNDCARDTYPE {
  SC_UNINIT = 0,
  SC_NONE,
  SC_MOCKINGBOARD,
  SC_PHASOR
};  // Apple soundcard type

// Mockingboard card now uses the Peripheral ABI - no direct procedural calls
// required from core or frontends.
