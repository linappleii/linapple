// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <vector>

#include "Debugger_Types.h"

auto CmdDisasmDataDefByteX(int nArgs) -> Update_t;
auto CmdDisasmDataDefWordX(int nArgs) -> Update_t;

// Data Disassembler
// ______________________________________________________________________________

auto Disassembly_FindOpcode(uint16_t address) -> int;
DisasmData_t* Disassembly_IsDataAddress(uint16_t address);

auto Disassembly_AddData(DisasmData_t tData) -> void;
auto Disassembly_GetData(uint16_t nBaseAddress, const DisasmData_t* pData_,
                         DisasmLine_t& line_) -> void;
auto Disassembly_DelData(DisasmData_t tData) -> void;
DisasmData_t* Disassembly_Enumerate(DisasmData_t* pCurrent = nullptr);

extern std::vector<DisasmData_t> g_disassembler_data;
