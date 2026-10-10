// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <vector>

#include "Debugger_Types.h"

auto CmdDisasmDataDefByteX(int nArgs) -> UpdateResult;
auto CmdDisasmDataDefWordX(int nArgs) -> UpdateResult;

// Data Disassembler
// ______________________________________________________________________________

auto Disassembly_FindOpcode(uint16_t address) -> int;
auto Disassembly_IsDataAddress(uint16_t address) -> DisasmData*;

auto Disassembly_AddData(DisasmData tData) -> void;
auto Disassembly_GetData(uint16_t nBaseAddress, const DisasmData* data,
                         DisasmLine& line) -> void;
auto Disassembly_DelData(DisasmData tData) -> void;
auto Disassembly_Enumerate(DisasmData* pCurrent = nullptr) -> DisasmData*;

extern std::vector<DisasmData> disassembler_data;
