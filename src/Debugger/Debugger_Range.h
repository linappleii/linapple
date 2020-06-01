RangeType_t Range_Get( unsigned short & nAddress1_, unsigned short &nAddress2_, const int iArg = 1 );
bool Range_CalcEndLen(
  const RangeType_t eRange,
  const unsigned short & nAddress1,
  const unsigned short & nAddress2,
  unsigned short & nAddressEnd_,
  int & nAddressLen_
);
