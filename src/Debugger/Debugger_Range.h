// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>

#include "Debugger_Types.h"

struct RangeEndLen_t {
  uint16_t nAddressEnd;
  int nAddressLen;
};

auto Range_Get(uint16_t& nAddress1_, uint16_t& nAddress2_, const int iArg = 1)
    -> RangeType_t;

auto Range_CalcEndLen(const RangeType_t eRange, const uint16_t& nAddress1,
                      const uint16_t& nAddress2, RangeEndLen_t& tEndLen_)
    -> bool;
