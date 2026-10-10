// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "Debugger_Types.h"
#include "Util_MemoryTextFile.h"

// Directives

enum Assemblers : uint8_t {
  ASM_ACME,
  ASM_BIG_MAC,
  ASM_DOS_TOOL_KIT,
  ASM_LISA,
  ASM_MERLIN,
  ASM_MICROSPARC,
  ASM_ORCA,
  ASM_SC,
  ASM_TED,
  ASM_WELLERS,
  ASM_CUSTOM,
  NUM_ASSEMBLERS,
};

enum AsmAcmeDirective : uint8_t {
  ASM_A_DEFINE_BYTE,
  NUM_ASM_ACME_DIRECTIVES,
};

enum AsmBigMacDirective : uint8_t {
  ASM_B_DEFINE_BYTE,
  NUM_ASM_BIG_MAC_DIRECTIVES,
};

enum AsmDosToolKitDirective : uint8_t {
  ASM_D_DEFINE_BYTE,
  NUM_ASM_DOS_TOOL_KIT_DIRECTIVES,
};

enum AsmLisaDirective : uint8_t {
  ASM_L_DEFINE_BYTE,
  NUM_ASM_LISA_DIRECTIVES,
};

enum AsmMerlinDirective : uint8_t {
  ASM_MERLIN_ASCII,
  ASM_M_DEFINE_WORD,
  ASM_M_DEFINE_BYTE,
  ASM_M_DEFINE_STORAGE,
  ASM_M_HEX,
  ASM_M_ORIGIN,
  NUM_ASM_MERLIN_DIRECTIVES,
  ASM_M_DEFINE_BYTE_ALIAS,
  ASM_M_DEFINE_WORD_ALIAS,
};

enum AsmMicroSparcDirective : uint8_t {
  ASM_u_DEFINE_BYTE,
  NUM_ASM_MICROSPARC_DIRECTIVES,
};

enum AsmOrcamDirective : uint8_t {
  ASM_O_DEFINE_BYTE,
  NUM_ASM_ORCA_DIRECTIVES,
};

enum AsmSCMacroDirective : uint8_t {
  ASM_S_ORIGIN,
  ASM_S_TARGET_ADDRESS,
  ASM_S_END_PROGRAM,
  ASM_S_EQUATE,
  ASM_S_DATA,
  ASM_S_ASCII_STRING,
  ASM_S_HEX_STRING,
  NUM_ASM_SC_DIRECTIVES,
};

enum AsmTedDirective : uint8_t { ASM_T_DEFINE_BYTE, NUM_ASM_TED_DIRECTIVES };

enum AsmWellersDirective : uint8_t {
  ASM_W_DEFINE_BYTE,
  NUM_ASM_WELLERS_DIRECTIVES,
};

enum AsmCustomDirective : uint8_t {
  ASM_DEFINE_BYTE,
  ASM_DEFINE_WORD,
  ASM_DEFINE_ADDRESS_16,
  ASM_DEFINE_ASCII_TEXT,
  ASM_DEFINE_APPLE_TEXT,
  ASM_DEFINE_TEXT_HI_LO,
  ASM_DEFINE_FLOAT,
  ASM_DEFINE_FLOAT_X,
  NUM_ASM_CUSTOM_DIRECTIVES,
};

// NOTE: Keep in sync AsmDirectives and assembler_directives
enum AsmDirectives : uint8_t {
  FIRST_ACME_DIRECTIVE = 1,
  FIRST_BIG_MAC_DIRECTIVE = FIRST_ACME_DIRECTIVE + NUM_ASM_ACME_DIRECTIVES,
  FIRST_DOS_TOOL_KIT_DIRECTIVE = FIRST_BIG_MAC_DIRECTIVE +
      NUM_ASM_BIG_MAC_DIRECTIVES,
  FIRST_LISA_DIRECTIVE = FIRST_DOS_TOOL_KIT_DIRECTIVE +
      NUM_ASM_DOS_TOOL_KIT_DIRECTIVES,
  FIRST_MERLIN_DIRECTIVE = FIRST_LISA_DIRECTIVE + NUM_ASM_LISA_DIRECTIVES,
  FIRST_MICROSPARC_DIRECTIVE = FIRST_MERLIN_DIRECTIVE +
      NUM_ASM_MERLIN_DIRECTIVES,
  FIRST_ORCA_DIRECTIVE = FIRST_MICROSPARC_DIRECTIVE +
      NUM_ASM_MICROSPARC_DIRECTIVES,
  FIRST_SC_DIRECTIVE = FIRST_ORCA_DIRECTIVE + NUM_ASM_ORCA_DIRECTIVES,
  FIRST_TED_DIRECTIVE = FIRST_SC_DIRECTIVE + NUM_ASM_SC_DIRECTIVES,
  FIRST_WELLERS_DIRECTIVE = FIRST_TED_DIRECTIVE + NUM_ASM_TED_DIRECTIVES,
  FIRST_CUSTOM_DIRECTIVE = FIRST_WELLERS_DIRECTIVE + NUM_ASM_WELLERS_DIRECTIVES,
  NUM_ASM_DIRECTIVES = FIRST_CUSTOM_DIRECTIVE + NUM_ASM_CUSTOM_DIRECTIVES,
};

extern int assembler_syntax;
extern int assembler_first_directive[NUM_ASSEMBLERS];

// Addressing
// _____________________________________________________________________________________

extern AddressingMode opmodes[NUM_ADDRESSING_MODES];

// Assembler
// ______________________________________________________________________________________

// Hashing for Assembler
using Hash = uint32_t;

struct HashOpcode {
  int opcode;
  Hash value;

  auto operator()(const HashOpcode& lhs, const HashOpcode& rhs) const
      -> bool {
    return lhs.value < rhs.value;
  }
};

struct AssemblerDirective {
  const char* mnemonic;
  Hash hash;
};

extern bool assembler_opcodes_hashed;
extern Hash opcodes_hash[NUM_OPCODES];
extern bool assembler_input;
extern int assembler_address;

extern const Opcodes* opcodes;

extern const Opcodes opcodes65_c02[NUM_OPCODES];
extern const Opcodes opcodes6502[NUM_OPCODES];

extern AssemblerDirective assembler_directives[NUM_ASM_DIRECTIVES];

// Prototypes _______________________________________________________________

auto GetOpmodeOpbyte(int nBaseAddress, int& iOpmode_, int& nOpbyte_,
                     const DisasmData** pData_ = nullptr) -> int;
auto GetOpcodeOpmodeOpbyte(int& iOpcode_, int& iOpmode_, int& nOpbyte_) -> void;
auto GetStackReturnAddress(uint16_t& nAddress_) -> bool;
auto GetTargets(uint16_t address, int* pTargetPartial_, int* pTargetPartial2_,
                int* pTargetPointer_, int* pTargetBytes_,
                bool bIgnoreBranch = true,
                bool bIncludeNextOpcodeAddress = true) -> bool;
auto GetTargetAddress(const uint16_t& address, uint16_t& nTarget_) -> bool;
auto IsOpcodeBranch(int opcode) -> bool;
auto IsOpcodeValid(int opcode) -> bool;

auto AssemblerHashMnemonic(const char* mnemonic) -> uint32_t;
auto CmdAssembleHashDump() -> void;

auto AssemblerDelayedTargetsSize() -> int;
auto AssemblerStartup() -> void;
auto Assemble(int iArg, int nArgs, uint16_t address) -> bool;

auto AssemblerOn() -> void;
auto AssemblerOff() -> void;

auto debugger_get_file_size(FILE* file) -> size_t;
auto CmdAssemble(uint16_t address, int iArg, int nArgs) -> UpdateResult;

extern bool source_level_debugging;
extern bool source_add_symbols;
extern bool source_add_memory;
extern std::string source_file_name;
extern MemoryTextFile assembler_source_buffer;
extern int source_display_start;
extern int source_assemble_bytes;
extern int source_assembly_symbols;
extern SourceAssembly source_debug;

auto BufferAssemblyListing(const std::string& filename) -> bool;
auto ParseAssemblyListing(bool bBytesToMemory, bool bAddSymbols) -> bool;
auto FindAddressFromSourceLine(int line) -> int;
auto FindSourceLineFromAddress(uint16_t address) -> int;
