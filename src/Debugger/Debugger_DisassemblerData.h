// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <vector>

#include "Debugger_Types.h"

Update_t _CmdDisasmDataDefByteX(int nArgs);
Update_t _CmdDisasmDataDefWordX(int nArgs);

// Data Disassembler
// ______________________________________________________________________________

int Disassembly_FindOpcode(uint16_t address);
DisasmData_t* Disassembly_IsDataAddress(uint16_t address);

void Disassembly_AddData(DisasmData_t tData);
void Disassembly_GetData(uint16_t nBaseAddress, const DisasmData_t* pData_,
                         DisasmLine_t& line_);
void Disassembly_DelData(DisasmData_t tData);
DisasmData_t* Disassembly_Enumerate(DisasmData_t* pCurrent = nullptr);

extern std::vector<DisasmData_t> g_disassembler_data;
