#include "Debugger_Types.h"

RangeType_t Range_Get(uint16_t& nAddress1_, uint16_t& nAddress2_,
                      const int iArg = 1);
struct RangeEndLen_t {
  uint16_t nAddressEnd;
  int nAddressLen;
};

bool Range_CalcEndLen(const RangeType_t eRange, const uint16_t& nAddress1,
                      const uint16_t& nAddress2, RangeEndLen_t& tEndLen_);
