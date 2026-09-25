// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "Debugger_Types.h"
#include "Util_MemoryTextFile.h"

// Directives

enum Assemblers_e {
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
  NUM_ASSEMBLERS
};

enum AsmAcmeDirective_e { ASM_A_DEFINE_BYTE, NUM_ASM_ACME_DIRECTIVES };

enum AsmBigMacDirective_e { ASM_B_DEFINE_BYTE, NUM_ASM_BIG_MAC_DIRECTIVES };

enum AsmDosToolKitDirective_e {
  ASM_D_DEFINE_BYTE,
  NUM_ASM_DOS_TOOL_KIT_DIRECTIVES
};

enum AsmLisaDirective_e { ASM_L_DEFINE_BYTE, NUM_ASM_LISA_DIRECTIVES };

enum AsmMerlinDirective_e {
  ASM_MERLIN_ASCII,
  ASM_M_DEFINE_WORD,
  ASM_M_DEFINE_BYTE,
  ASM_M_DEFINE_STORAGE,
  ASM_M_HEX,
  ASM_M_ORIGIN,
  NUM_ASM_MERLIN_DIRECTIVES,
  ASM_M_DEFINE_BYTE_ALIAS,
  ASM_M_DEFINE_WORD_ALIAS
};

enum AsmMicroSparcDirective_e {
  ASM_u_DEFINE_BYTE,
  NUM_ASM_MICROSPARC_DIRECTIVES
};

enum AsmOrcamDirective_e { ASM_O_DEFINE_BYTE, NUM_ASM_ORCA_DIRECTIVES };

enum AsmSCMacroDirective_e {
  ASM_S_ORIGIN,
  ASM_S_TARGET_ADDRESS,
  ASM_S_END_PROGRAM,
  ASM_S_EQUATE,
  ASM_S_DATA,
  ASM_S_ASCII_STRING,
  ASM_S_HEX_STRING,
  NUM_ASM_SC_DIRECTIVES
};

enum AsmTedDirective_e { ASM_T_DEFINE_BYTE, NUM_ASM_TED_DIRECTIVES };

enum AsmWellersDirective_e { ASM_W_DEFINE_BYTE, NUM_ASM_WELLERS_DIRECTIVES };

enum AsmCustomDirective_e {
  ASM_DEFINE_BYTE,
  ASM_DEFINE_WORD,
  ASM_DEFINE_ADDRESS_16,
  ASM_DEFINE_ASCII_TEXT,
  ASM_DEFINE_APPLE_TEXT,
  ASM_DEFINE_TEXT_HI_LO,
  ASM_DEFINE_FLOAT,
  ASM_DEFINE_FLOAT_X,
  NUM_ASM_CUSTOM_DIRECTIVES
};

// NOTE: Keep in sync AsmDirectives_e and g_assembler_directives
enum AsmDirectives_e {
  FIRST_ACME_DIRECTIVE = 1,
  FIRST_BIG_MAC_DIRECTIVE = FIRST_ACME_DIRECTIVE + NUM_ASM_ACME_DIRECTIVES,
  FIRST_DOS_TOOL_KIT_DIRECTIVE =
      FIRST_BIG_MAC_DIRECTIVE + NUM_ASM_BIG_MAC_DIRECTIVES,
  FIRST_LISA_DIRECTIVE =
      FIRST_DOS_TOOL_KIT_DIRECTIVE + NUM_ASM_DOS_TOOL_KIT_DIRECTIVES,
  FIRST_MERLIN_DIRECTIVE = FIRST_LISA_DIRECTIVE + NUM_ASM_LISA_DIRECTIVES,
  FIRST_MICROSPARC_DIRECTIVE =
      FIRST_MERLIN_DIRECTIVE + NUM_ASM_MERLIN_DIRECTIVES,
  FIRST_ORCA_DIRECTIVE =
      FIRST_MICROSPARC_DIRECTIVE + NUM_ASM_MICROSPARC_DIRECTIVES,
  FIRST_SC_DIRECTIVE = FIRST_ORCA_DIRECTIVE + NUM_ASM_ORCA_DIRECTIVES,
  FIRST_TED_DIRECTIVE = FIRST_SC_DIRECTIVE + NUM_ASM_SC_DIRECTIVES,
  FIRST_WELLERS_DIRECTIVE = FIRST_TED_DIRECTIVE + NUM_ASM_TED_DIRECTIVES,
  FIRST_CUSTOM_DIRECTIVE = FIRST_WELLERS_DIRECTIVE + NUM_ASM_WELLERS_DIRECTIVES,
  NUM_ASM_DIRECTIVES = FIRST_CUSTOM_DIRECTIVE + NUM_ASM_CUSTOM_DIRECTIVES
};

extern int g_assembler_syntax;
extern int g_assembler_first_directive[NUM_ASSEMBLERS];

// Addressing
// _____________________________________________________________________________________

extern AddressingMode_t g_opmodes[NUM_ADDRESSING_MODES];

// Assembler
// ______________________________________________________________________________________

// Hashing for Assembler
typedef uint32_t Hash_t;

struct HashOpcode_t {
  int opcode;
  Hash_t value;

  auto operator()(const HashOpcode_t& lhs, const HashOpcode_t& rhs) const
      -> bool {
    return lhs.value < rhs.value;
  }
};

struct AssemblerDirective_t {
  const char* mnemonic;
  Hash_t hash;
};

extern bool g_assembler_opcodes_hashed;
extern Hash_t g_opcodes_hash[NUM_OPCODES];
extern bool g_assembler_input;
extern int g_assembler_address;

extern const Opcodes_t* g_opcodes;

extern const Opcodes_t g_opcodes65_c02[NUM_OPCODES];
extern const Opcodes_t g_opcodes6502[NUM_OPCODES];

extern AssemblerDirective_t g_assembler_directives[NUM_ASM_DIRECTIVES];

// Prototypes _______________________________________________________________

auto GetOpmodeOpbyte(int address, int& opmode, int& opbytes,
                     const DisasmData_t** data = nullptr) -> int;
auto GetOpcodeOpmodeOpbyte(int& opcode, int& opmode, int& opbytes) -> void;
auto GetStackReturnAddress(uint16_t& address) -> bool;
auto GetTargets(uint16_t address, int* target_partial_1, int* target_partial_2,
                int* target_pointer, int* bytes, bool ignore_branch = true,
                bool include_next_opcode_address = true) -> bool;
auto GetTargetAddress(const uint16_t& address, uint16_t& target) -> bool;
auto IsOpcodeBranch(int opcode) -> bool;
auto IsOpcodeValid(int opcode) -> bool;

auto AssemblerHashMnemonic(const char* mnemonic) -> uint32_t;
auto CmdAssembleHashDump() -> void;

auto AssemblerDelayedTargetsSize() -> int;
auto AssemblerStartup() -> void;
auto Assemble(int arg_index, int arg_count, uint16_t address) -> bool;

auto AssemblerOn() -> void;
auto AssemblerOff() -> void;

auto debugger_get_file_size(FILE* file) -> size_t;
auto CmdAssemble(uint16_t address, int arg_index, int arg_count) -> Update_t;

auto CmdAssemble(int arg_count) -> Update_t;
auto CmdSource(int arg_count) -> Update_t;
auto CmdUnassemble(int arg_count) -> Update_t;

extern bool g_source_level_debugging;
extern bool g_source_add_symbols;
extern bool g_source_add_memory;
extern std::string g_source_file_name;
extern MemoryTextFile_t g_assembler_source_buffer;
extern int g_source_display_start;
extern int g_source_assemble_bytes;
extern int g_source_assembly_symbols;
extern SourceAssembly_t g_source_debug;

auto BufferAssemblyListing(const std::string& filename) -> bool;
auto ParseAssemblyListing(bool bytes_to_memory, bool add_symbols) -> bool;
auto FindAddressFromSourceLine(int line) -> int;
auto FindSourceLineFromAddress(uint16_t address) -> int;
