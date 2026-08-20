#include "Debugger_Assembler.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Debug.h"
#include "Debugger_Console.h"
#include "Debugger_DisassemblerData.h"
#include "Debugger_Parser.h"
#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"

#define DEBUG_ASSEMBLER 0

// Globals __________________________________________________________________

// Addressing
// _____________________________________________________________________________________

AddressingMode_t g_opmodes[NUM_ADDRESSING_MODES] = {
    // Output, but eventually used for Input when Assembler is working.
    {"", 1, "(implied)"},             // AM_IMPLIED
    {"", 1, "n/a 1"},                 // AM_1
    {"", 2, "n/a 2"},                 // AM_2
    {"", 3, "n/a 3"},                 // AM_3
    {"%02X", 2, "Immediate"},         // AM_M // #$%02X -> %02X
    {"%04X", 3, "Absolute"},          // AM_A
    {"%02X", 2, "Zero Page"},         // AM_Z
    {"%04X,X", 3, "Absolute,X"},      // AM_AX     // %s,X
    {"%04X,Y", 3, "Absolute,Y"},      // AM_AY     // %s,Y
    {"%02X,X", 2, "Zero Page,X"},     // AM_ZX     // %s,X
    {"%02X,Y", 2, "Zero Page,Y"},     // AM_ZY     // %s,Y
    {"%s", 2, "Relative"},            // AM_R
    {"(%02X,X", 2, "(Zero Page,X)"},  // AM_IZX // ($%02X,X) -> %s,X
    {"(%04X,X", 3, "(Absolute,X)"},   // AM_IAX // ($%04X,X) -> %s,X
    {"(%02X,Y", 2, "(Zero Page),Y"},  // AM_NZY // ($%02X),Y
    {"(%02X", 2, "(Zero Page)"},      // AM_NZ  // ($%02X) -> $%02X
    {"(%04X", 3, "(Absolute)"},       // AM_NA  // (%04X) -> %s
    {"", 1, "Data"}                   // AM_DATA
};

// Assembler
// ______________________________________________________________________________________

int g_assembler_opcodes_hashed = false;
Hash_t g_opcodes_hash[NUM_OPCODES] = {};  // for faster mnemonic lookup, for the
                                          // assembler
bool g_assembler_input = false;
int g_assembler_address = 0;

const Opcodes_t* g_opcodes = nullptr;  // & g_opcodes65_c02[ 0 ];

// Disassembler Data
// _____________________________________________________________________________

std::vector<DisasmData_t> g_disassembler_data;

// Instructions / Opcodes
// _________________________________________________________________________

// @reference: http://www.6502.org/tutorials/compare_instructions.html
// 10   signed: BPL BGE
// B0 unsigned: BCS BGE

#define R_ MEM_R
#define _W MEM_W
#define RW MEM_R | MEM_W
#define _S MEM_S
constexpr auto IM = MEM_IM;
#define SW MEM_S | MEM_WI
#define SR MEM_S | MEM_RI
const Opcodes_t g_opcodes65_c02[NUM_OPCODES] = {
    {"BRK", 0, SW},      {"ORA", AM_IZX, R_},
    {"nop", AM_M, IM},   {"nop", 0, 0},  // 00 .. 03
    {"TSB", AM_Z, _W},   {"ORA", AM_Z, R_},
    {"ASL", AM_Z, RW},   {"nop", 0, 0},  // 04 .. 07
    {"PHP", 0, SW},      {"ORA", AM_M, IM},
    {"ASL", 0, 0},       {"nop", 0, 0},  // 08 .. 0B
    {"TSB", AM_A, _W},   {"ORA", AM_A, R_},
    {"ASL", AM_A, RW},   {"nop", 0, 0},  // 0C .. 0F
    {"BPL", AM_R, 0},    {"ORA", AM_NZY, R_},
    {"ORA", AM_NZ, R_},  {"nop", 0, 0},  // 10 .. 13
    {"TRB", AM_Z, _W},   {"ORA", AM_ZX, R_},
    {"ASL", AM_ZX, RW},  {"nop", 0, 0},  // 14 .. 17
    {"CLC", 0, 0},       {"ORA", AM_AY, R_},
    {"INC", 0, 0},       {"nop", 0, 0},  // 18 .. 1B
    {"TRB", AM_A, _W},   {"ORA", AM_AX, R_},
    {"ASL", AM_AX, RW},  {"nop", 0, 0},  // 1C .. 1F

    {"JSR", AM_A, SW},   {"AND", AM_IZX, R_},
    {"nop", AM_M, IM},   {"nop", 0, 0},  // 20 .. 23
    {"BIT", AM_Z, R_},   {"AND", AM_Z, R_},
    {"ROL", AM_Z, RW},   {"nop", 0, 0},  // 24 .. 27
    {"PLP", 0, SR},      {"AND", AM_M, IM},
    {"ROL", 0, 0},       {"nop", 0, 0},  // 28 .. 2B
    {"BIT", AM_A, R_},   {"AND", AM_A, R_},
    {"ROL", AM_A, RW},   {"nop", 0, 0},  // 2C .. 2F
    {"BMI", AM_R, 0},    {"AND", AM_NZY, R_},
    {"AND", AM_NZ, R_},  {"nop", 0, 0},  // 30 .. 33
    {"BIT", AM_ZX, R_},  {"AND", AM_ZX, R_},
    {"ROL", AM_ZX, RW},  {"nop", 0, 0},  // 34 .. 37
    {"SEC", 0, 0},       {"AND", AM_AY, R_},
    {"DEC", 0, 0},       {"nop", 0, 0},  // 38 .. 3B
    {"BIT", AM_AX, R_},  {"AND", AM_AX, R_},
    {"ROL", AM_AX, RW},  {"nop", 0, 0},  // 3C .. 3F

    {"RTI", 0, SR},      {"EOR", AM_IZX, R_},
    {"nop", AM_M, IM},   {"nop", 0, 0},  // 40 .. 43
    {"nop", AM_Z, 0},    {"EOR", AM_Z, R_},
    {"LSR", AM_Z, _W},   {"nop", 0, 0},  // 44 .. 47
    {"PHA", 0, SW},      {"EOR", AM_M, IM},
    {"LSR", 0, 0},       {"nop", 0, 0},  // 48 .. 4B
    {"JMP", AM_A, 0},    {"EOR", AM_A, R_},
    {"LSR", AM_A, _W},   {"nop", 0, 0},  // 4C .. 4F
    {"BVC", AM_R, 0},    {"EOR", AM_NZY, R_},
    {"EOR", AM_NZ, R_},  {"nop", 0, 0},  // 50 .. 53
    {"nop", AM_ZX, 0},   {"EOR", AM_ZX, R_},
    {"LSR", AM_ZX, _W},  {"nop", 0, 0},  // 54 .. 57
    {"CLI", 0, 0},       {"EOR", AM_AY, R_},
    {"PHY", 0, SW},      {"nop", 0, 0},  // 58 .. 5B
    {"nop", AM_AX, 0},   {"EOR", AM_AX, R_},
    {"LSR", AM_AX, RW},  {"nop", 0, 0},  // 5C .. 5F

    {"RTS", 0, SR},      {"ADC", AM_IZX, R_},
    {"nop", AM_M, IM},   {"nop", 0, 0},  // 60 .. 63
    {"STZ", AM_Z, _W},   {"ADC", AM_Z, R_},
    {"ROR", AM_Z, RW},   {"nop", 0, 0},  // 64 .. 67
    {"PLA", 0, SR},      {"ADC", AM_M, IM},
    {"ROR", 0, 0},       {"nop", 0, 0},  // 68 .. 6B
    {"JMP", AM_NA, R_},  {"ADC", AM_A, R_},
    {"ROR", AM_A, RW},   {"nop", 0, 0},  // 6C .. 6F
    {"BVS", AM_R, 0},    {"ADC", AM_NZY, R_},
    {"ADC", AM_NZ, R_},  {"nop", 0, 0},  // 70 .. 73
    {"STZ", AM_ZX, _W},  {"ADC", AM_ZX, R_},
    {"ROR", AM_ZX, RW},  {"nop", 0, 0},  // 74 .. 77
    {"SEI", 0, 0},       {"ADC", AM_AY, R_},
    {"PLY", 0, SR},      {"nop", 0, 0},  // 78 .. 7B
    {"JMP", AM_IAX, R_}, {"ADC", AM_AX, R_},
    {"ROR", AM_AX, RW},  {"nop", 0, 0},  // 7C .. 7F

    {"BRA", AM_R, 0},    {"STA", AM_IZX, _W},
    {"nop", AM_M, IM},   {"nop", 0, 0},  // 80 .. 83
    {"STY", AM_Z, _W},   {"STA", AM_Z, _W},
    {"STX", AM_Z, _W},   {"nop", 0, 0},  // 84 .. 87
    {"DEY", 0, 0},       {"BIT", AM_M, IM},
    {"TXA", 0, 0},       {"nop", 0, 0},  // 88 .. 8B
    {"STY", AM_A, _W},   {"STA", AM_A, _W},
    {"STX", AM_A, _W},   {"nop", 0, 0},  // 8C .. 8F
    {"BCC", AM_R, 0},    {"STA", AM_NZY, _W},
    {"STA", AM_NZ, _W},  {"nop", 0, 0},  // 90 .. 93
    {"STY", AM_ZX, _W},  {"STA", AM_ZX, _W},
    {"STX", AM_ZY, _W},  {"nop", 0, 0},  // 94 .. 97
    {"TYA", 0, 0},       {"STA", AM_AY, _W},
    {"TXS", 0, 0},       {"nop", 0, 0},  // 98 .. 9B
    {"STZ", AM_A, _W},   {"STA", AM_AX, _W},
    {"STZ", AM_AX, _W},  {"nop", 0, 0},  // 9C .. 9F

    {"LDY", AM_M, IM},   {"LDA", AM_IZX, R_},
    {"LDX", AM_M, IM},   {"nop", 0, 0},  // A0 .. A3
    {"LDY", AM_Z, R_},   {"LDA", AM_Z, R_},
    {"LDX", AM_Z, R_},   {"nop", 0, 0},  // A4 .. A7
    {"TAY", 0, 0},       {"LDA", AM_M, IM},
    {"TAX", 0, 0},       {"nop", 0, 0},  // A8 .. AB
    {"LDY", AM_A, R_},   {"LDA", AM_A, R_},
    {"LDX", AM_A, R_},   {"nop", 0, 0},  // AC .. AF
    {"BCS", AM_R, 0},    {"LDA", AM_NZY, R_},
    {"LDA", AM_NZ, R_},  {"nop", 0, 0},  // B0 .. B3
    {"LDY", AM_ZX, R_},  {"LDA", AM_ZX, R_},
    {"LDX", AM_ZY, R_},  {"nop", 0, 0},  // B4 .. B7
    {"CLV", 0, 0},       {"LDA", AM_AY, R_},
    {"TSX", 0, 0},       {"nop", 0, 0},  // B8 .. BB
    {"LDY", AM_AX, R_},  {"LDA", AM_AX, R_},
    {"LDX", AM_AY, R_},  {"nop", 0, 0},  // BC .. BF

    {"CPY", AM_M, IM},   {"CMP", AM_IZX, R_},
    {"nop", AM_M, IM},   {"nop", 0, 0},  // C0 .. C3
    {"CPY", AM_Z, R_},   {"CMP", AM_Z, R_},
    {"DEC", AM_Z, RW},   {"nop", 0, 0},  // C4 .. C7
    {"INY", 0, 0},       {"CMP", AM_M, IM},
    {"DEX", 0, 0},       {"nop", 0, 0},  // C8 .. CB
    {"CPY", AM_A, R_},   {"CMP", AM_A, R_},
    {"DEC", AM_A, RW},   {"nop", 0, 0},  // CC .. CF
    {"BNE", AM_R, 0},    {"CMP", AM_NZY, R_},
    {"CMP", AM_NZ, 0},   {"nop", 0, 0},  // D0 .. D3
    {"nop", AM_ZX, 0},   {"CMP", AM_ZX, R_},
    {"DEC", AM_ZX, RW},  {"nop", 0, 0},  // D4 .. D7
    {"CLD", 0, 0},       {"CMP", AM_AY, R_},
    {"PHX", 0, SW},      {"nop", 0, 0},  // D8 .. DB
    {"nop", AM_AX, 0},   {"CMP", AM_AX, R_},
    {"DEC", AM_AX, RW},  {"nop", 0, 0},  // DC .. DF

    {"CPX", AM_M, IM},   {"SBC", AM_IZX, R_},
    {"nop", AM_M, IM},   {"nop", 0, 0},  // E0 .. E3
    {"CPX", AM_Z, R_},   {"SBC", AM_Z, R_},
    {"INC", AM_Z, RW},   {"nop", 0, 0},  // E4 .. E7
    {"INX", 0, 0},       {"SBC", AM_M, R_},
    {"NOP", 0, 0},       {"nop", 0, 0},  // E8 .. EB
    {"CPX", AM_A, R_},   {"SBC", AM_A, R_},
    {"INC", AM_A, RW},   {"nop", 0, 0},  // EC .. EF
    {"BEQ", AM_R, 0},    {"SBC", AM_NZY, R_},
    {"SBC", AM_NZ, 0},   {"nop", 0, 0},  // F0 .. F3
    {"nop", AM_ZX, 0},   {"SBC", AM_ZX, R_},
    {"INC", AM_ZX, RW},  {"nop", 0, 0},  // F4 .. F7
    {"SED", 0, 0},       {"SBC", AM_AY, R_},
    {"PLX", 0, SR},      {"nop", 0, 0},  // F8 .. FB
    {"nop", AM_AX, 0},   {"SBC", AM_AX, R_},
    {"INC", AM_AX, RW},  {"nop", 0, 0}  // FF .. FF
};

const Opcodes_t g_opcodes6502[NUM_OPCODES] = {
    // Should match Cpu.cpp InternalCpuExecute() switch
    // (*(mem+cpu_get_registers()->pc++)) !!

    /*
            Based on: http://axis.llx.com/~nparker/a2/opcodes.html

            If you really want to know what the undocumented --- (n/a)
    opcodes do, see CPU.cpp

            x0     x1         x2       x3   x4       x5       x6 x7   x8
    x9       xA      xB   xC        xD       xE      	xF 0x	BRK ORA
    (d,X)  ---      ---  tsb z    ORA d    ASL z    ---  PHP  ORA # ASL A  ---
    tsb a      ORA a    ASL a   	--- 1x	BPL r  ORA (d),Y ora (z)  ---
    trb d    ORA d,X  ASL z,X  ---  CLC  ORA a,Y  ina A
    ---  trb a      ORA a,X  ASL a,X 	--- 2x	JSR a  AND (d,X)  ---
    ---  BIT d    AND d    ROL z    ---  PLP  AND #    ROL A  ---  BIT a
    AND a    ROL a   	--- 3x	BMI r  AND (d),Y  and (z)  ---  bit d,X
    AND d,X  ROL z,X  ---  SEC  AND a,Y  dea A  ---  bit a,X    AND a,X
    ROL a,X 	--- 4x	RTI    EOR (d,X)  ---      ---  ---      EOR d
    LSR z    ---  PHA  EOR #    LSR A  ---  JMP a      EOR a    LSR a
    --- 5x	BVC r  EOR (d),Y  eor (z)  ---  ---      EOR d,X  LSR
    z,X  ---  CLI  EOR a,Y  phy    ---  ---        EOR a,X  LSR a,X
    --- 6x	RTS    ADC (d,X)  ---      ---  stz d    ADC d    ROR z
    ---  PLA  ADC #    ROR A  ---  JMP (a)    ADC a    ROR a   	--- 7x	BVS r
    ADC (d),Y  adc (z)  ---  stz d,X  ADC d,X  ROR z,X  ---  SEI  ADC a,Y  ply
    ---  jmp (a,X)  ADC a,X  ROR a,X 	--- 8x	bra r  STA (d,X)  ---      ---
    STY d    STA d    STX z    --- DEY  bit #    TXA    ---  STY a      STA a
    STX a   	--- 9x	BCC r  STA (d),Y  sta (z)  ---  STY d,X  STA d,X  STX
    z,Y  ---  TYA  STA a,Y  TXS    ---  Stz a      STA a,X  stz a,X 	--- Ax
    LDY #  LDA (d,X)  LDX #    ---  LDY d    LDA d    LDX z    --- TAY  LDA #
    TAX    ---  LDY a      LDA a    LDX a   	--- Bx	BCS r  LDA (d),Y  lda
    (z)  ---  LDY d,X  LDA d,X  LDX z,Y  ---  CLV  LDA a,Y  TSX    ---  LDY a,X
    LDA a,X  LDX a,Y 	--- Cx	CPY #  CMP (d,X)  ---      ---  CPY d    CMP d
    DEC z    --- INY  CMP #    DEX    ---  CPY a      CMP a    DEC a   	--- Dx
    BNE r  CMP (d),Y  cmp (z)  ---  ---      CMP d,X  DEC z,X  ---  CLD  CMP a,Y
    phx    ---  ---        CMP a,X  DEC a,X 	--- Ex	CPX #  SBC (d,X)  ---
    ---  CPX d    SBC d    INC z    --- INX  SBC #    NOP    ---  CPX a      SBC
    a    INC a   	--- Fx	BEQ r  SBC (d),Y  sbc (z)  ---  ---      SBC d,X
    INC z,X  ---  SED  SBC a,Y  plx    ---  ---        SBC a,X  INC a,X
    ---

            Legend:
            --- illegal instruction
                    UPPERCASE 6502
                    lowercase 65C02
                            80
                            12, 32, 52, 72, 92, B2, D2, F2
                            04, 14, 34, 64, 74
                            89
                            1A, 3A, 5A, 7A, DA, FA
                            0C, 1C, 3C, 7C, 9C;
                    # Immediate
                    A Accumulator (implicit for mnemonic)
                    a absolute
                    r Relative
                    d Destination 16-bit Address
                    z Destination Zero Page Address
                    z,x Base=Zero-Page, Offset=X
                    d,x
                    (d,X)
                    (d),Y

    */
    {"BRK", 0, SW},     {"ORA", AM_IZX, R_},
    {"hlt", 0, 0},      {"aso", AM_IZX, RW},  // 00 .. 03
    {"nop", AM_Z, R_},  {"ORA", AM_Z, R_},
    {"ASL", AM_Z, RW},  {"aso", AM_Z, RW},  // 04 .. 07
    {"PHP", 0, SW},     {"ORA", AM_M, IM},
    {"ASL", 0, 0},      {"anc", AM_M, IM},  // 08 .. 0B
    {"nop", AM_AX, 0},  {"ORA", AM_A, R_},
    {"ASL", AM_A, RW},  {"aso", AM_A, RW},  // 0C .. 0F
    {"BPL", AM_R, 0},   {"ORA", AM_NZY, R_},
    {"hlt", 0, 0},      {"aso", AM_NZY, RW},  // 10 .. 13
    {"nop", AM_ZX, 0},  {"ORA", AM_ZX, R_},
    {"ASL", AM_ZX, RW}, {"aso", AM_ZX, RW},  // 14 .. 17
    {"CLC", 0, 0},      {"ORA", AM_AY, R_},
    {"nop", 0, 0},      {"aso", AM_AY, RW},  // 18 .. 1B
    {"nop", AM_AX, 0},  {"ORA", AM_AX, R_},
    {"ASL", AM_AX, RW}, {"aso", AM_AX, RW},  // 1C .. 1F

    {"JSR", AM_A, SW},  {"AND", AM_IZX, R_},
    {"hlt", 0, 0},      {"rla", AM_IZX, RW},  // 20 .. 23
    {"BIT", AM_Z, R_},  {"AND", AM_Z, R_},
    {"ROL", AM_Z, RW},  {"rla", AM_Z, RW},  // 24 .. 27
    {"PLP", 0, SR},     {"AND", AM_M, IM},
    {"ROL", 0, 0},      {"anc", AM_M, IM},  // 28 .. 2B
    {"BIT", AM_A, R_},  {"AND", AM_A, R_},
    {"ROL", AM_A, RW},  {"rla", AM_A, RW},  // 2C .. 2F
    {"BMI", AM_R, 0},   {"AND", AM_NZY, R_},
    {"hlt", 0, 0},      {"rla", AM_NZY, RW},  // 30 .. 33
    {"nop", AM_ZX, 0},  {"AND", AM_ZX, R_},
    {"ROL", AM_ZX, RW}, {"rla", AM_ZX, RW},  // 34 .. 37
    {"SEC", 0, 0},      {"AND", AM_AY, R_},
    {"nop", 0, 0},      {"rla", AM_AY, RW},  // 38 .. 3B
    {"nop", AM_AX, 0},  {"AND", AM_AX, R_},
    {"ROL", AM_AX, RW}, {"rla", AM_AX, RW},  // 3C .. 3F

    {"RTI", 0, SR},     {"EOR", AM_IZX, R_},
    {"hlt", 0, 0},      {"lse", AM_IZX, RW},  // 40 .. 43
    {"nop", AM_Z, 0},   {"EOR", AM_Z, R_},
    {"LSR", AM_Z, RW},  {"lse", AM_Z, RW},  // 44 .. 47
    {"PHA", 0, SW},     {"EOR", AM_M, IM},
    {"LSR", 0, 0},      {"alr", AM_M, IM},  // 48 .. 4B
    {"JMP", AM_A, 0},   {"EOR", AM_A, R_},
    {"LSR", AM_A, RW},  {"lse", AM_A, RW},  // 4C .. 4F
    {"BVC", AM_R, 0},   {"EOR", AM_NZY, R_},
    {"hlt", 0, 0},      {"lse", AM_NZY, RW},  // 50 .. 53
    {"nop", AM_ZX, 0},  {"EOR", AM_ZX, R_},
    {"LSR", AM_ZX, RW}, {"lse", AM_ZX, RW},  // 54 .. 57
    {"CLI", 0, 0},      {"EOR", AM_AY, R_},
    {"nop", 0, 0},      {"lse", AM_AY, RW},  // 58 .. 5B
    {"nop", AM_AX, 0},  {"EOR", AM_AX, R_},
    {"LSR", AM_AX, RW}, {"lse", AM_AX, RW},  // 5C .. 5F

    {"RTS", 0, SR},     {"ADC", AM_IZX, R_},
    {"hlt", 0, 0},      {"rra", AM_IZX, RW},  // 60 .. 63
    {"nop", AM_Z, 0},   {"ADC", AM_Z, R_},
    {"ROR", AM_Z, RW},  {"rra", AM_Z, RW},  // 64 .. 67
    {"PLA", 0, SR},     {"ADC", AM_M, IM},
    {"ROR", 0, 0},      {"arr", AM_M, IM},  // 68 .. 6B
    {"JMP", AM_NA, R_}, {"ADC", AM_A, R_},
    {"ROR", AM_A, RW},  {"rra", AM_A, RW},  // 6C .. 6F
    {"BVS", AM_R, 0},   {"ADC", AM_NZY, R_},
    {"hlt", 0, 0},      {"rra", AM_NZY, RW},  // 70 .. 73
    {"nop", AM_ZX, 0},  {"ADC", AM_ZX, R_},
    {"ROR", AM_ZX, RW}, {"rra", AM_ZX, RW},  // 74 .. 77
    {"SEI", 0, 0},      {"ADC", AM_AY, R_},
    {"nop", 0, 0},      {"rra", AM_AY, RW},  // 78 .. 7B
    {"nop", AM_AX, 0},  {"ADC", AM_AX, R_},
    {"ROR", AM_AX, RW}, {"rra", AM_AX, RW},  // 7C .. 7F

    {"nop", AM_M, IM},  {"STA", AM_IZX, _W},
    {"nop", AM_M, IM},  {"axs", AM_IZX, _W},  // 80 .. 83
    {"STY", AM_Z, _W},  {"STA", AM_Z, _W},
    {"STX", AM_Z, _W},  {"axs", AM_Z, _W},  // 84 .. 87
    {"DEY", 0, 0},      {"nop", AM_M, IM},
    {"TXA", 0, 0},      {"xaa", AM_M, IM},  // 88 .. 8B
    {"STY", AM_A, _W},  {"STA", AM_A, _W},
    {"STX", AM_A, _W},  {"axs", AM_A, _W},  // 8C .. 8F
    {"BCC", AM_R, 0},   {"STA", AM_NZY, _W},
    {"hlt", 0, 0},      {"axa", AM_NZY, _W},  // 90 .. 93
    {"STY", AM_ZX, _W}, {"STA", AM_ZX, _W},
    {"STX", AM_ZY, _W}, {"axs", AM_ZY, _W},  // 94 .. 97
    {"TYA", 0, 0},      {"STA", AM_AY, _W},
    {"TXS", 0, 0},      {"tas", AM_AY, _W},  // 98 .. 9B
    {"say", AM_AX, _W}, {"STA", AM_AX, _W},
    {"xas", AM_AX, _W}, {"axa", AM_AY, _W},  // 9C .. 9F

    {"LDY", AM_M, IM},  {"LDA", AM_IZX, R_},
    {"LDX", AM_M, IM},  {"lax", AM_IZX, R_},  // A0 .. A3
    {"LDY", AM_Z, R_},  {"LDA", AM_Z, R_},
    {"LDX", AM_Z, R_},  {"lax", AM_Z, R_},  // A4 .. A7
    {"TAY", 0, 0},      {"LDA", AM_M, IM},
    {"TAX", 0, 0},      {"oal", AM_M, IM},  // A8 .. AB
    {"LDY", AM_A, R_},  {"LDA", AM_A, R_},
    {"LDX", AM_A, R_},  {"lax", AM_A, R_},  // AC .. AF
    {"BCS", AM_R, 0},   {"LDA", AM_NZY, R_},
    {"hlt", 0, 0},      {"lax", AM_NZY, R_},  // B0 .. B3
    {"LDY", AM_ZX, R_}, {"LDA", AM_ZX, R_},
    {"LDX", AM_ZY, R_}, {"lax", AM_ZY, 0},  // B4 .. B7
    {"CLV", 0, 0},      {"LDA", AM_AY, R_},
    {"TSX", 0, 0},      {"las", AM_AY, R_},  // B8 .. BB
    {"LDY", AM_AX, R_}, {"LDA", AM_AX, R_},
    {"LDX", AM_AY, R_}, {"lax", AM_AY, R_},  // BC .. BF

    {"CPY", AM_M, IM},  {"CMP", AM_IZX, R_},
    {"nop", AM_M, IM},  {"dcm", AM_IZX, RW},  // C0 .. C3
    {"CPY", AM_Z, R_},  {"CMP", AM_Z, R_},
    {"DEC", AM_Z, RW},  {"dcm", AM_Z, RW},  // C4 .. C7
    {"INY", 0, 0},      {"CMP", AM_M, IM},
    {"DEX", 0, 0},      {"sax", AM_M, IM},  // C8 .. CB
    {"CPY", AM_A, R_},  {"CMP", AM_A, R_},
    {"DEC", AM_A, RW},  {"dcm", AM_A, RW},  // CC .. CF
    {"BNE", AM_R, 0},   {"CMP", AM_NZY, R_},
    {"hlt", 0, 0},      {"dcm", AM_NZY, RW},  // D0 .. D3
    {"nop", AM_ZX, 0},  {"CMP", AM_ZX, R_},
    {"DEC", AM_ZX, RW}, {"dcm", AM_ZX, RW},  // D4 .. D7
    {"CLD", 0, 0},      {"CMP", AM_AY, R_},
    {"nop", 0, 0},      {"dcm", AM_AY, RW},  // D8 .. DB
    {"nop", AM_AX, 0},  {"CMP", AM_AX, R_},
    {"DEC", AM_AX, RW}, {"dcm", AM_AX, RW},  // DC .. DF

    {"CPX", AM_M, IM},  {"SBC", AM_IZX, R_},
    {"nop", AM_M, IM},  {"ins", AM_IZX, RW},  // E0 .. E3
    {"CPX", AM_Z, R_},  {"SBC", AM_Z, R_},
    {"INC", AM_Z, RW},  {"ins", AM_Z, RW},  // E4 .. E7
    {"INX", 0, 0},      {"SBC", AM_M, IM},
    {"NOP", 0, 0},      {"sbc", AM_M, IM},  // E8 .. EB
    {"CPX", AM_A, R_},  {"SBC", AM_A, R_},
    {"INC", AM_A, RW},  {"ins", AM_A, RW},  // EC .. EF
    {"BEQ", AM_R, 0},   {"SBC", AM_NZY, R_},
    {"hlt", 0, 0},      {"ins", AM_NZY, RW},  // F0 .. F3
    {"nop", AM_ZX, 0},  {"SBC", AM_ZX, R_},
    {"INC", AM_ZX, RW}, {"ins", AM_ZX, RW},  // F4 .. F7
    {"SED", 0, 0},      {"SBC", AM_AY, R_},
    {"nop", 0, 0},      {"ins", AM_AY, RW},  // F8 .. FB
    {"nop", AM_AX, 0},  {"SBC", AM_AX, R_},
    {"INC", AM_AX, RW}, {"ins", AM_AX, RW}  // FF .. FF
};

#undef R_
#undef _W
#undef RW
#undef _S
#undef IM
#undef SW
#undef SR

// @reference: http://www.textfiles.com/apple/DOCUMENTATION/merlin.docs1

// Private __________________________________________________________________

// NOTE: Keep in sync AsmDirectives_e g_assembler_directives !
AssemblerDirective_t g_assembler_directives[NUM_ASM_DIRECTIVES] = {
    // nullptr n/a
    {"", 0},
    // Origin, Target Address, EndProg, Equate, Data, AsciiString,HexString
    // Acme
    {"???", 0},
    // Big Mac
    {"???", 0},
    // DOS Tool Kit
    {"???", 0},
    // Lisa
    {"???", 0},
    // Merlin
    {"ASC", 0},  // ASC "postive" 'negative'
    {"DDB", 0},  // Define Double Byte (Define uint16_t)
    {"DFB", 0},  // DeFine Byte
    {"DS", 0},   // Define Storage
    {"HEX", 0},  // HEX ###### or HEX ##,##,...
    {"ORG", 0},  // Origin
                 // MicroSparc
    {"???", 0},
    // ORCA/M
    {"???", 0},
    // SC ...
    {".OR", 0},  //    ORigin
    {".TA", 0},  //    Target Address
    {".EN", 0},  //    ENd of program
    {".EQ", 0},  //    EQuate
    {".DA", 0},  //    DAta
    {".AS", 0},  //    Ascii String
    {".HS", 0},  //    Hex String
                 // Ted II
    {"???", 0},
    // Weller
    {"???", 0},
    // User-Defined
    // NOTE: Keep in sync AsmCustomDirective_e g_assembler_directives !
    {"db", 0},  // ASM_DEFINE_BYTE
    {"dw", 0},  // ASM_DEFINE_WORD
    {"da", 0},  // ASM_DEFINE_ADDRESS_16
                // d    memory Dump
                // da   Memory Ascii, Define Address
                // ds   S = Ascii (Low),
                // dt   T = Apple (High)
                // dm   M = Mixed (Low,High=EndofString)
    {"ds", 0},  // ASM_DEFINE_ASCII_TEXT
    {"dt", 0},  // ASM_DEFINE_APPLE_TEXT
    {"dm", 0},  // ASM_DEFINE_TEXT_HI_LO

    {"df", 0},   // ASM_DEFINE_FLOAT
    {"dfx", 0},  // ASM_DEFINE_FLOAT_X
};

int g_assembler_syntax = ASM_CUSTOM;  // Which assembler syntax to use
int g_assembler_first_directive[NUM_ASSEMBLERS] = {
    FIRST_A_DIRECTIVE, FIRST_B_DIRECTIVE, FIRST_D_DIRECTIVE, FIRST_L_DIRECTIVE,
    FIRST_M_DIRECTIVE, FIRST_u_DIRECTIVE, FIRST_O_DIRECTIVE, FIRST_S_DIRECTIVE,
    FIRST_T_DIRECTIVE, FIRST_W_DIRECTIVE, FIRST_Z_DIRECTIVE};

// Assemblers

enum AssemblerFlags_e {
  AF_HaveLabel = (1 << 0),
  AF_HaveComma = (1 << 1),
  AF_HaveHash = (1 << 2),
  AF_HaveImmediate = (1 << 3),
  AF_HaveDollar = (1 << 4),
  AF_HaveLeftParen = (1 << 5),
  AF_HaveRightParen = (1 << 6),
  AF_HaveEitherParen = (1 << 7),
  AF_HaveBothParen = (1 << 8),
  AF_HaveRegisterX = (1 << 9),
  AF_HaveRegisterY = (1 << 10),
  AF_HaveZeroPage = (1 << 11),
  AF_HaveTarget = (1 << 12),
};

enum AssemblerState_e {
  AS_GET_MNEMONIC,
  AS_GET_MNEMONIC_PARM,
  AS_GET_HASH,
  AS_GET_TARGET,
  AS_GET_PAREN,
  AS_GET_INDEX,
  AS_DONE
};

int g_asm_flags;
std::vector<int> g_asm_opcodes;
int g_asm_address_mode = AM_IMPLIED;

struct DelayedTarget_t {
  char address_str[MAX_SYMBOLS_LEN + 1];
  uint16_t base_address;  // mem address to store symbol at
  int opcode;
  int opmode;  // AddressingMode_e
};

std::vector<DelayedTarget_t> g_delayed_targets;
bool g_delayed_targets_dirty = false;

int g_asm_bytes = 0;
uint16_t g_asm_base_address = 0;
uint16_t g_asm_target_address = 0;
uint16_t g_asm_target_value = 0;

// Private
void AssemblerHashOpcodes();
void AssemblerHashDirectives();

// Implementation ___________________________________________________________

//===========================================================================
auto _6502_CalcRelativeOffset(int nOpcode, int nBaseAddress, int nTargetAddress,
                              uint16_t* pTargetOffset_) -> bool {
  if (_6502_IsOpcodeBranch(nOpcode)) {
    // Branch is
    //   a) relative to address+2
    //   b) in 2's compliment
    //
    // i.e.
    //   300: D0 7F -> BNE $381   0x381 - 0x300 = 0x81 +129
    //   300: D0 80 -> BNE $282   0x282 - 0x300 =      -126
    //
    // 300: D0 7E BNE $380
    // ^    ^   ^      ^
    // |    |   |      TargetAddress
    // |    |   TargetOffset
    // |    Opcode
    // BaseAddress
    int nDistance = nTargetAddress - nBaseAddress;
    if (pTargetOffset_) {
      *pTargetOffset_ = static_cast<uint8_t>(nDistance - 2);
    }

    if ((nDistance - 2) > _6502_BRANCH_POS) {
      g_asm_address_mode = NUM_OPMODES;  // signal bad
    }

    if ((nDistance - 2) < _6502_BRANCH_NEG) {
      g_asm_address_mode = NUM_OPMODES;  // signal bad
    }

    return true;
  }

  return false;
}

//===========================================================================
auto _6502_GetOpmodeOpbyte(const int nBaseAddress, int& iOpmode_, int& nOpbyte_,
                           const DisasmData_t** pData_) -> int {
#if _DEBUG
  if (!g_opcodes) {
    fprintf(stderr, "%s: %s\n", "ERROR", "Debugger not properly initialized");

    g_opcodes = &g_opcodes65_c02[0];  // Enhanced Apple //e
    g_opmodes[AM_2].bytes = 2;
    g_opmodes[AM_3].bytes = 3;
  }
#endif

  if (!g_opcodes) {
    iOpmode_ = 0;
    nOpbyte_ = 1;
    return 0;
  }

  if (!mem) {
    iOpmode_ = 0;
    nOpbyte_ = 1;
    return 0;
  }

  int iOpcode_ = *(mem + nBaseAddress);
  if (iOpcode_ >= NUM_OPCODES) {
    iOpmode_ = 0;
    nOpbyte_ = 1;
    return 0;
  }
  iOpmode_ = g_opcodes[iOpcode_].nAddressMode;
  if (iOpmode_ >= NUM_ADDRESSING_MODES) {
    iOpmode_ = 0;
    nOpbyte_ = 1;
    return 0;
  }
  nOpbyte_ = g_opmodes[iOpmode_].bytes;

  // 2.6.2.25 Fixed: DB DW custom data byte sizes weren't scrolling properly in
  // the disasm view.
  //          Changed _6502_GetOpmodeOpbyte() to be aware of data bytes.
  //
  // NOTE: _6502_GetOpmodeOpbyte() needs to (effectively) call
  // Disassembly_GetData()
  //    a) the CmdCursorLineUp() calls us to calc for -X bytes back up how to
  //    reach the cursor (address) line below b) The disassembler view needs to
  //    know how many bytes each line is.
  int nSlack = 0;

  // 2.7.0.0 TODO: FIXME: Opcode length that over-lap data, should be shortened
  // ... if (nOpbyte_ > 1) if Disassembly_IsDataAddress( nBaseAddress + 1 )
  // nOpbyte_ = 1;
  DisasmData_t* data = Disassembly_IsDataAddress(nBaseAddress);
  if (data) {
    if (pData_) {
      *pData_ = data;
    }

    nSlack =
        data->nEndAddress - data->nStartAddress +
        1;  // *inclusive* KEEP IN SYNC: _CmdDefineByteRange()
            // CmdDisasmDataList() _6502_GetOpmodeOpbyte() FormatNopcodeBytes()

    // Data Disassembler
    // Smart Disassembly - Data Section
    // Assemblyer Directives - Psuedo Mnemonics
    switch (data->eElementType) {
      case NOP_BYTE_1:
        nOpbyte_ = 1;
        iOpmode_ = AM_M;
        break;
      case NOP_BYTE_2:
        nOpbyte_ = 2;
        iOpmode_ = AM_M;
        break;
      case NOP_BYTE_4:
        nOpbyte_ = 4;
        iOpmode_ = AM_M;
        break;
      case NOP_BYTE_8:
        nOpbyte_ = 8;
        iOpmode_ = AM_M;
        break;
      case NOP_WORD_1:
        nOpbyte_ = 2;
        iOpmode_ = AM_M;
        break;
      case NOP_WORD_2:
        nOpbyte_ = 4;
        iOpmode_ = AM_M;
        break;
      case NOP_WORD_4:
        nOpbyte_ = 8;
        iOpmode_ = AM_M;
        break;
      case NOP_ADDRESS:
        nOpbyte_ = 2;
        iOpmode_ = AM_A;  // BUGFIX: 2.6.2.33 Define Address should be shown as
                          // Absolute mode, not Indirect Absolute mode. DA
                          // BASIC.FPTR D000:D080 // was showing as "da (END-1)"
                          // now shows as "da END-1"
        data->nTargetAddress = *reinterpret_cast<uint16_t*>(mem + nBaseAddress);
        break;
      case NOP_STRING_APPLE:
        iOpmode_ = AM_DATA;
        nOpbyte_ = nSlack;
        break;
      case NOP_STRING_APPLESOFT:
        // TODO: FIXME: scan memory for high byte
        nOpbyte_ = 8;
        iOpmode_ = AM_DATA;
        break;
      default:
#if _DEBUG  // not implemented!
        int* fatal = 0;
        *fatal = 0xDEADC0DE;
#endif
        break;
    }
    /*
                    // REMOVED in v1.25 ... because of AppleSoft Basic:  DW
       NEXT1 801  DW LINE1 803
                    // Check if we are not element aligned ...
                    nSlack = (nOpbyte_ > 1) ? (nBaseAddress & nOpbyte_-1 ) : 0;
                    if (nSlack)
                    {
                            nOpbyte_ = nSlack;
                            iOpmode_ = AM_M;
                    }
    */
    // iOpcode_ = NUM_OPCODES; // Don't have valid opcodes ... we have data !
    //  iOpcode_ = (int)( data ); // HACK: pass data back to caller ...
    iOpcode_ = OPCODE_NOP;
  }

#if _DEBUG
  if (iOpcode_ >= NUM_OPCODES) {
    bool bStop = true;
  }
#endif

  return iOpcode_;
}

//===========================================================================
void _6502_GetOpcodeOpmodeOpbyte(int& iOpcode_, int& iOpmode_, int& nOpbyte_) {
  iOpcode_ = _6502_GetOpmodeOpbyte(cpu_get_registers()->pc, iOpmode_, nOpbyte_);
}

//===========================================================================
auto _6502_GetStackReturnAddress(uint16_t& nAddress_) -> bool {
  unsigned nStack = cpu_get_registers()->sp;
  nStack++;

  if (nStack <= (_6502_STACK_END - 1)) {
    nAddress_ = static_cast<unsigned>(*(mem + nStack));
    nStack++;

    nAddress_ += (static_cast<unsigned>(*(mem + nStack))) << 8;
    nAddress_++;
    return true;
  }
  return false;
}

//===========================================================================
auto _6502_GetTargets(uint16_t address, int* pTargetPartial_,
                      int* pTargetPartial2_, int* pTargetPointer_,
                      int* pTargetBytes_, bool bIgnoreBranch /*= true*/,
                      bool bIncludeNextOpcodeAddress /*= true*/) -> bool {
  if (!pTargetPartial_) {
    return false;
  }

  if (!pTargetPartial2_) {
    return false;
  }

  if (!pTargetPointer_) {
    return false;
  }

  //	if (! pTargetBytes_)
  //		return false;

  *pTargetPartial_ = NO_6502_TARGET;
  *pTargetPartial2_ = NO_6502_TARGET;
  *pTargetPointer_ = NO_6502_TARGET;

  if (pTargetBytes_) {
    *pTargetBytes_ = 0;
  }

  uint8_t nOpcode = mem[address];
  uint8_t nTarget8 = mem[(address + 1) & 0xFFFF];
  uint16_t nTarget16 = (mem[(address + 2) & 0xFFFF] << 8) | nTarget8;

  int eMode = g_opcodes[nOpcode].nAddressMode;

  // We really need to use the values that are code and data assembler
  // TODO: FIXME: _6502_GetOpmodeOpbyte( iAddress, iOpmode, nOpbytes );

  switch (eMode) {
    case AM_IMPLIED:
      if (g_opcodes[nOpcode].nMemoryAccess & MEM_S)  // Stack R/W?
      {
        if (nOpcode == OPCODE_RTI || nOpcode == OPCODE_RTS)  // RTI or RTS?
        {
          uint16_t sp = cpu_get_registers()->sp;

          if (nOpcode == OPCODE_RTI) {
            //*pTargetPartial3_ = _6502_STACK_BEGIN + ((sp+1) & 0xFF);	// TODO:
            // PLP
            ++sp;
          }

          *pTargetPartial_ =
              static_cast<int>(_6502_STACK_BEGIN + ((sp + 1) & 0xFF));
          *pTargetPartial2_ =
              static_cast<int>(_6502_STACK_BEGIN + ((sp + 2) & 0xFF));
          nTarget16 = static_cast<uint16_t>(mem[*pTargetPartial_] +
                                            (mem[*pTargetPartial2_] << 8));

          if (nOpcode == OPCODE_RTS) {
            ++nTarget16;
          }
        } else if (nOpcode == OPCODE_BRK)  // BRK?
        {
          *pTargetPartial_ = static_cast<int>(
              _6502_STACK_BEGIN + ((cpu_get_registers()->sp + 0) & 0xFF));
          *pTargetPartial2_ = static_cast<int>(
              _6502_STACK_BEGIN + ((cpu_get_registers()->sp - 1) & 0xFF));
          //*pTargetPartial3_ = _6502_STACK_BEGIN + ((cpu_get_registers()->sp-2)
          //& 0xFF);	// TODO: PHP *pTargetPartial4_ = _6502_BRK_VECTOR + 0;
          //// TODO *pTargetPartial5_ = _6502_BRK_VECTOR + 1;	// TODO
          nTarget16 = *reinterpret_cast<uint16_t*>(mem + _6502_BRK_VECTOR);
        } else  // PHn/PLn
        {
          if (g_opcodes[nOpcode].nMemoryAccess & MEM_WI) {
            nTarget16 = static_cast<uint16_t>(
                _6502_STACK_BEGIN + ((cpu_get_registers()->sp + 0) & 0xFF));
          } else {
            nTarget16 = static_cast<uint16_t>(
                _6502_STACK_BEGIN + ((cpu_get_registers()->sp + 1) & 0xFF));
          }
        }

        if (bIncludeNextOpcodeAddress ||
            (nOpcode != OPCODE_RTI && nOpcode != OPCODE_RTS &&
             nOpcode != OPCODE_BRK)) {
          *pTargetPointer_ = static_cast<int>(nTarget16);
        }

        if (pTargetBytes_) {
          *pTargetBytes_ = 1;
        }
      }
      break;

    case AM_A:  // Absolute
      if (nOpcode == OPCODE_JSR) {
        *pTargetPartial_ = static_cast<int>(
            _6502_STACK_BEGIN + ((cpu_get_registers()->sp + 0) & 0xFF));
        *pTargetPartial2_ = static_cast<int>(
            _6502_STACK_BEGIN + ((cpu_get_registers()->sp - 1) & 0xFF));
      }

      if (bIncludeNextOpcodeAddress ||
          (nOpcode != OPCODE_JSR && nOpcode != OPCODE_JMP_A)) {
        *pTargetPointer_ = static_cast<int>(nTarget16);
      }

      if (pTargetBytes_) {
        *pTargetBytes_ = 2;
      }
      break;

    case AM_IAX:  // Indexed (Absolute) Indirect - ie. JMP (abs,x)
      assert(nOpcode == OPCODE_JMP_IAX);
      nTarget16 += cpu_get_registers()->x;
      *pTargetPartial_ = static_cast<int>(nTarget16);
      *pTargetPartial2_ = static_cast<int>(nTarget16 + 1);
      if (bIncludeNextOpcodeAddress) {
        *pTargetPointer_ =
            static_cast<int>(*reinterpret_cast<uint16_t*>(mem + nTarget16));
      }
      if (pTargetBytes_) {
        *pTargetBytes_ = 2;
      }
      break;

    case AM_AX:  // Absolute, X
      nTarget16 += cpu_get_registers()->x;
      *pTargetPointer_ = nTarget16;
      if (pTargetBytes_) {
        *pTargetBytes_ = 2;
      }
      break;

    case AM_AY:  // Absolute, Y
      nTarget16 += cpu_get_registers()->y;
      *pTargetPointer_ = nTarget16;
      if (pTargetBytes_) {
        *pTargetBytes_ = 2;
      }
      break;

    case AM_NA:  // Indirect (Absolute) - ie. JMP (abs)
      assert(nOpcode == OPCODE_JMP_NA);
      *pTargetPartial_ = nTarget16;
      *pTargetPartial2_ = nTarget16 + 1;
      if (bIncludeNextOpcodeAddress) {
        *pTargetPointer_ = *reinterpret_cast<uint16_t*>(mem + nTarget16);
      }
      if (pTargetBytes_) {
        *pTargetBytes_ = 2;
      }
      break;

    case AM_IZX:  // Indexed (Zeropage Indirect, X)
      nTarget8 += cpu_get_registers()->x;
      *pTargetPartial_ = static_cast<int>(nTarget8);
      *pTargetPointer_ =
          static_cast<int>(*reinterpret_cast<uint16_t*>(mem + nTarget8));
      if (pTargetBytes_) {
        *pTargetBytes_ = 2;
      }
      break;

    case AM_NZY:  // Indirect (Zeropage) Indexed, Y
      *pTargetPartial_ = static_cast<int>(nTarget8);
      *pTargetPointer_ =
          static_cast<int>(((*reinterpret_cast<uint16_t*>(mem + nTarget8)) +
                            cpu_get_registers()->y) &
                           _6502_MEM_END);  // Bugfix:
      if (pTargetBytes_) {
        *pTargetBytes_ = 1;
      }
      break;

    case AM_NZ:  // Indirect (Zeropage)
      *pTargetPartial_ = static_cast<int>(nTarget8);
      *pTargetPointer_ =
          static_cast<int>(*reinterpret_cast<uint16_t*>(mem + nTarget8));
      if (pTargetBytes_) {
        *pTargetBytes_ = 2;
      }
      break;

    case AM_R:
      if (!bIgnoreBranch) {
        *pTargetPartial_ = nTarget8;
        *pTargetPointer_ = address + 2;

        if (nTarget8 <= _6502_BRANCH_POS) {
          *pTargetPointer_ += nTarget8;  // +
        } else {
          *pTargetPointer_ -= nTarget8;  // -
        }

        *pTargetPointer_ &= _6502_MEM_END;

        if (pTargetBytes_) {
          *pTargetBytes_ = 1;
        }
      }
      break;

    case AM_Z:  // Zeropage
      *pTargetPointer_ = nTarget8;
      if (pTargetBytes_) {
        *pTargetBytes_ = 1;
      }
      break;

    case AM_ZX:  // Zeropage, X
      *pTargetPointer_ = (nTarget8 + cpu_get_registers()->x) &
                         0xFF;  // .21 Bugfix: shouldn't this wrap around? Yes.
      if (pTargetBytes_) {
        *pTargetBytes_ = 1;
      }
      break;

    case AM_ZY:  // Zeropage, Y
      *pTargetPointer_ = (nTarget8 + cpu_get_registers()->y) &
                         0xFF;  // .21 Bugfix: shouldn't this wrap around? Yes.
      if (pTargetBytes_) {
        *pTargetBytes_ = 1;
      }
      break;

    default:
      if (pTargetBytes_) {
        *pTargetBytes_ = 0;
      }
      break;
  }

  return true;
}

//===========================================================================
auto _6502_GetTargetAddress(const uint16_t& address, uint16_t& nTarget_)
    -> bool {
  int opcode = 0;
  int iOpmode = 0;
  int nOpbytes = 0;
  opcode = _6502_GetOpmodeOpbyte(address, iOpmode, nOpbytes);
  (void)opcode;

  // Composite string that has the target address

  if ((iOpmode != AM_IMPLIED) && (iOpmode != AM_1) && (iOpmode != AM_2) &&
      (iOpmode != AM_3)) {
    int nTargetPartial = 0;
    int nTargetPartial2 = 0;
    int nTargetPointer = 0;
    int nTargetBytes = 0;
    _6502_GetTargets(address, &nTargetPartial, &nTargetPartial2,
                     &nTargetPointer, &nTargetBytes, false);

    //		if (nTargetPointer == NO_6502_TARGET)
    //		{
    //			if (_6502_IsOpcodeBranch( nOpcode )
    //			{
    //				return true;
    //			}
    //		}
    if (nTargetPointer != NO_6502_TARGET)
    //		else
    {
      nTarget_ = nTargetPointer & _6502_MEM_END;
      return true;
    }
  }
  return false;
}

//===========================================================================
auto _6502_IsOpcodeBranch(int opcode) -> bool {
  // 76543210 Bit
  // xxx10000 Branch
  if (opcode == OPCODE_BRA) {
    return true;
  }

  if ((opcode & 0x1F) != 0x10) {  // low nibble not zero?
    return false;
  }

  if ((opcode >> 4) & 1) {
    return true;
  }

  //		(nOpcode == 0x10) || // BPL
  //		(nOpcode == 0x30) || // BMI
  //		(nOpcode == 0x50) || // BVC
  //		(nOpcode == 0x70) || // BVS
  //		(nOpcode == 0x90) || // BCC
  //		(nOpcode == 0xB0) || // BCS
  //		(nOpcode == 0xD0) || // BNE
  //		(nOpcode == 0xF0) || // BEQ
  return false;
}

//===========================================================================
auto _6502_IsOpcodeValid(int opcode) -> bool {
  if ((opcode & 0x3) == 0x3) {
    return false;
  }

  if (islower(g_opcodes6502[opcode].sMnemonic[0])) {
    return false;
  }

  return true;
}

// Assembler ________________________________________________________________

auto AssemblerHashMnemonic(const char* pMnemonic) -> uint32_t {
  const char* text = pMnemonic;
  int nMnemonicHash = 0;
  int iHighBits = 0;

  const int NUM_LOW_BITS = 19;  // 24 -> 19 prime
  const int NUM_MSK_BITS = 5;   //  4 ->  5 prime
  const Hash_t BIT_MSK_HIGH = ((1 << NUM_MSK_BITS) - 1) << NUM_LOW_BITS;

#if DEBUG_ASSEMBLER
  int nLen = strlen(pMnemonic);
  static int nMaxLen = 0;
  if (nMaxLen < nLen) {
    nMaxLen = nLen;
    char sText[CONSOLE_WIDTH * 3];
    ConsolePrintFormat(sText, "New Max Len: %d  %s", nMaxLen, pMnemonic);
  }
#endif

  while (*text)
  //	for( int iChar = 0; iChar < 4; iChar++ )
  {
    char c = static_cast<char>(tolower(static_cast<unsigned char>(
        *text)));  // TODO: based on ALLOW_INPUT_LOWERCASE ??

    nMnemonicHash = (nMnemonicHash << NUM_MSK_BITS) +
                    static_cast<unsigned int>(static_cast<unsigned char>(c));
    iHighBits = (nMnemonicHash & BIT_MSK_HIGH);
    if (iHighBits) {
      nMnemonicHash =
          (nMnemonicHash ^ (iHighBits >> NUM_LOW_BITS)) & ~BIT_MSK_HIGH;
    }
    text++;
  }

  return nMnemonicHash;
}

//===========================================================================
void AssemblerHashOpcodes() {
  Hash_t nMnemonicHash = 0;
  int opcode = 0;

  for (opcode = 0; opcode < NUM_OPCODES; opcode++) {
    const char* pMnemonic = g_opcodes65_c02[opcode].sMnemonic;
    nMnemonicHash = AssemblerHashMnemonic(pMnemonic);
    g_opcodes_hash[opcode] = nMnemonicHash;
#if DEBUG_ASSEMBLER
    // OutputDebugString( "" );
    char sText[128];
    ConsolePrintFormat(sText, "%s : %08X  ", pMnemonic, nMnemonicHash);
    // CLC: 002B864
#endif
  }
  ConsoleUpdate();
}

//===========================================================================
auto CmdAssemble(int nArgs) -> Update_t {
  if (!g_assembler_opcodes_hashed) {
    AssemblerStartup();
    g_assembler_opcodes_hashed = true;
  }

  // 0 : A
  // 1 : A address
  // 2+: A address mnemonic...

  if (!nArgs) {
    //    return Help_Arg_1( CMD_ASSEMBLE );

    // Start assembler, continue with last assembled address
    AssemblerOn();
    return UPDATE_CONSOLE_DISPLAY;
  }

  g_assembler_address = g_args[1].nValue;

  if (nArgs == 1) {
    int iArg = 1;

    // undocumented ASM *
    if ((!strcmp(g_args[iArg].sArg, g_parameters[PARAM_WILDSTAR].name)) ||
        (!strcmp(g_args[iArg].sArg,
                 g_parameters[PARAM_MEM_SEARCH_WILD].name))) {
      _CmdAssembleHashDump();
    }

    AssemblerOn();
    return UPDATE_CONSOLE_DISPLAY;

    //    return Help_Arg_1( CMD_ASSEMBLE );
  }

  if (nArgs > 1) {
    return _CmdAssemble(g_assembler_address, 2,
                        nArgs);  // disasm, memory, watches, zeropage
  }

  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
auto CmdSource(int nArgs) -> Update_t {
  if (!nArgs) {
    g_source_level_debugging = false;
  } else {
    g_source_add_memory = false;
    g_source_add_symbols = false;

    for (int iArg = 1; iArg <= nArgs; iArg++) {
      const std::string pFileName = g_args[iArg].sArg;

      int iParam = 0;
      bool bFound = FindParam(pFileName.c_str(), MATCH_EXACT, iParam,
                              _PARAM_SOURCE_BEGIN, _PARAM_SOURCE_END) > 0;
      if (bFound && (iParam == PARAM_SRC_SYMBOLS)) {
        g_source_add_symbols = true;
      } else if (bFound && (iParam == PARAM_SRC_MEMORY)) {
        g_source_add_memory = true;
      } else {
        const std::string sFileName =
            std::string(g_state.program_dir.data()) + pFileName;

        const int MAX_MINI_FILENAME = 20;
        const std::string sMiniFileName =
            sFileName.substr(0, MIN(MAX_MINI_FILENAME, sFileName.size()));

        char buffer[path_max_len] = {0};

        if (BufferAssemblyListing(sFileName)) {
          g_source_file_name = pFileName;

          if (!ParseAssemblyListing(g_source_add_memory,
                                    g_source_add_symbols)) {
            ConsoleBufferPushFormat(buffer, "Couldn't load filename: %s",
                                    sMiniFileName.c_str());
          } else {
            g_source_level_debugging = true;
            ConsoleBufferPushFormat(buffer, "Loaded filename: %s",
                                    sMiniFileName.c_str());
          }
        } else {
          ConsoleBufferPushFormat(buffer, "Couldn't load filename: %s",
                                  sMiniFileName.c_str());
        }
        ConsoleBufferToDisplay();
      }
    }
  }

  return UPDATE_ALL;
}

//===========================================================================
auto CmdSync(int nArgs) -> Update_t {
  (void)nArgs;
  // TODO
  return UPDATE_CONSOLE_DISPLAY;
}

//===========================================================================
void AssemblerHashDirectives() {
  Hash_t nMnemonicHash = 0;
  int opcode = 0;

  for (opcode = 0; opcode < NUM_ASM_M_DIRECTIVES; opcode++) {
    int iNopcode = FIRST_M_DIRECTIVE + opcode;
    const char* pMnemonic = g_assembler_directives[iNopcode].mnemonic;
    nMnemonicHash = AssemblerHashMnemonic(pMnemonic);
    g_assembler_directives[iNopcode].hash = nMnemonicHash;
  }
}

#include <cstring>
#include <map>
#include <string>

#include "Debugger_Console.h"
#include "Debugger_Help.h"
#include "Debugger_Parser.h"
#include "Debugger_Symbols.h"

// Implementation helpers originally from Debug.cpp
bool g_source_level_debugging = false;
bool g_source_add_symbols = false;
bool g_source_add_memory = false;

std::string g_source_file_name;

MemoryTextFile_t g_assembler_source_buffer;

int g_source_display_start = 0;
int g_source_assemble_bytes = 0;
int g_source_assembly_symbols = 0;

// TODO: Support multiple source filenames
SourceAssembly_t g_source_debug;

auto _GetFileSize(FILE* hFile) -> size_t {
  fseek(hFile, 0, SEEK_END);
  size_t nFileBytes = ftell(hFile);
  fseek(hFile, 0, SEEK_SET);

  return nFileBytes;
}

auto _CmdAssemble(uint16_t address, int iArg, int nArgs) -> Update_t {
  // if AlphaNumeric
  ArgToken_e iTokenSrc = NO_TOKEN;
  ParserFindToken(g_console_input_ptr, g_tokens, NUM_TOKENS, &iTokenSrc);

  if (iTokenSrc == NO_TOKEN) {  // is TOKEN_ALPHANUMERIC
    if (g_console_input_ptr[0] != CHAR_SPACE) {
      // Symbol
      char* pSymbolName = g_args[iArg].sArg;  // pArg->sArg;
      SymbolUpdate(SYMBOLS_ASSEMBLY, pSymbolName, address, false,
                   true);  // bool bRemoveSymbol, bool bUpdateSymbol )

      iArg++;
    }
  }

  bool bStatus = Assemble(iArg, nArgs, address);
  if (bStatus) {
    return UPDATE_ALL;
  }

  return UPDATE_CONSOLE_DISPLAY;  // UPDATE_NOTHING;
}

//===========================================================================
auto BufferAssemblyListing(const std::string& pFileName) -> bool {
  bool bStatus = false;  // true = loaded

  if (pFileName.empty()) {
    return bStatus;
  }

  g_assembler_source_buffer.Reset();
  g_assembler_source_buffer.Read(pFileName);

  if (g_assembler_source_buffer.GetNumLines()) {
    g_source_level_debugging = true;
    bStatus = true;
  }

  return bStatus;
}

//===========================================================================
auto FindSourceLineFromAddress(uint16_t address) -> int {
  int iAddress = 0;
  int iLine = 0;
  int iSourceLine = NO_SOURCE_LINE;

  auto iSource = g_source_debug.begin();
  while (iSource != g_source_debug.end()) {
    iAddress = iSource->first;
    iLine = iSource->second;

    if (iAddress == address) {
      iSourceLine = iLine;
      break;
    }

    iSource++;
  }

  return iSourceLine;
}

//===========================================================================
auto FindAddressFromSourceLine(int nLine) -> int {
  int iAddress = NO_SOURCE_LINE;  // Reuse constant for "not found"

  auto iSource = g_source_debug.begin();
  while (iSource != g_source_debug.end()) {
    if (iSource->second == nLine) {
      iAddress = iSource->first;
      break;
    }
    iSource++;
  }

  return iAddress;
}

//===========================================================================
auto ParseAssemblyListing(bool bBytesToMemory, bool bAddSymbols) -> bool {
  bool bStatus = false;  // true = loaded

  char sName[MAX_SYMBOLS_LEN];

  const int MAX_LINE = 256;
  char sLine[MAX_LINE];
  char sText[MAX_LINE];

  g_source_assemble_bytes = 0;
  g_source_assembly_symbols = 0;

  const uint32_t INVALID_ADDRESS = _6502_MEM_END + 1;

  int nLines = g_assembler_source_buffer.GetNumLines();
  for (int iLine = 0; iLine < nLines; iLine++) {
    g_assembler_source_buffer.GetLine(iLine, sText, MAX_LINE - 1);

    uint32_t address = INVALID_ADDRESS;

    strcpy(sLine, sText);
    char* p = sLine;
    p = strstr(sLine, ":");
    if (p) {
      *p = 0;
      sscanf(sLine, "%X", &address);

      if (address >= INVALID_ADDRESS) {
        continue;
      }

      if (bBytesToMemory) {
        char* pEnd = p + 1;
        char* start = nullptr;
        int byte = 0;
        for (byte = 0; byte < 4; byte++) {
          start = pEnd + 1;
          pEnd = const_cast<char*>(skip_until_white_space(start));
          int nLen = static_cast<int>(pEnd - start);
          if (nLen != 2) {
            break;
          }
          *pEnd = 0;
          if (text_is_hex_byte(start)) {
            uint8_t nByte = text_convert_2_chars_to_byte(start);
            *(mem + (static_cast<uint16_t>(address)) + byte) = nByte;
          }
        }
        g_source_assemble_bytes += byte;
      }

      g_source_debug[static_cast<uint16_t>(address)] = iLine;
    }

    strcpy(sLine, sText);
    if (bAddSymbols) {
      char* pEQU = strstr(sLine, "EQU");
      char* pDFB = strstr(sLine, "DFB");
      char* pLabel = nullptr;

      if (pEQU) {
        pLabel = pEQU;
      }
      if (pDFB) {
        pLabel = pDFB;
      }

      if (pLabel) {
        char* pLabelEnd = pLabel - 1;
        pLabelEnd =
            const_cast<char*>(skip_white_space_reverse(pLabelEnd, &sLine[0]));
        char* pLabelStart = nullptr;
        if (pLabelEnd) {
          pLabelStart = const_cast<char*>(
              skip_until_white_space_reverse(pLabelEnd, &sLine[0]));
          pLabelEnd++;
          pLabelStart++;

          int nLen = static_cast<int>(pLabelEnd - pLabelStart);
          nLen = MIN(nLen, MAX_SYMBOLS_LEN);
          Util_SafeStrCpy(sName, pLabelStart, nLen);

          char* pAddressEQU = strstr(pLabel, "$");
          char* pAddressDFB = strstr(sLine, ":");
          char* pAddress = nullptr;

          if (pAddressEQU) {
            pAddress = pAddressEQU + 1;
          }
          if (pAddressDFB) {
            *pAddressDFB = 0;
            pAddress = sLine;
          }

          if (pAddress) {
            char* pAddressEnd = nullptr;
            address = static_cast<uint32_t>(strtol(pAddress, &pAddressEnd, 16));
            g_symbols[SYMBOLS_SRC_2][static_cast<uint16_t>(address)] = sName;
            g_source_assembly_symbols++;
          }
        }
      }
    }
  }  // for

  bStatus = true;

  return bStatus;
}

//===========================================================================
void AssemblerStartup()

{
  g_opcodes = &g_opcodes65_c02[0];
  AssemblerHashOpcodes();
  AssemblerHashDirectives();
}

//===========================================================================
void _CmdAssembleHashDump() {
  // #if DEBUG_ASM_HASH
  std::vector<HashOpcode_t> vHashes;
  HashOpcode_t tHash{};
  char sText[CONSOLE_WIDTH];

  int opcode = 0;
  for (opcode = 0; opcode < NUM_OPCODES; opcode++) {
    tHash.opcode = opcode;
    tHash.value = g_opcodes_hash[opcode];
    vHashes.push_back(tHash);
  }

  std::sort(vHashes.begin(), vHashes.end(), HashOpcode_t());

  for (opcode = 0; opcode < NUM_OPCODES; opcode++) {
    tHash = vHashes.at(opcode);

    Hash_t iThisHash = tHash.value;
    int nOpcode = tHash.opcode;
    int nOpmode = g_opcodes[nOpcode].nAddressMode;

    ConsoleBufferPushFormat(sText, "%08X %02X %s %s", iThisHash, nOpcode,
                            g_opcodes65_c02[nOpcode].sMnemonic,
                            g_opmodes[nOpmode].name);
  }

  ConsoleUpdate();
}

//===========================================================================
auto AssemblerPokeAddress(const int Opcode, const int nOpmode,
                          const uint16_t nBaseAddress,
                          const uint16_t nTargetOffset) -> int {
  (void)Opcode;
  int nOpbytes = g_opmodes[nOpmode].bytes;

  *(memdirty + (nBaseAddress >> 8)) |= 1;

  if (nOpbytes > 1) {
    *(mem + nBaseAddress + 1) = static_cast<uint8_t>(nTargetOffset >> 0);
  }

  if (nOpbytes > 2) {
    *(mem + nBaseAddress + 2) = static_cast<uint8_t>(nTargetOffset >> 8);
  }

  return nOpbytes;
}

//===========================================================================
auto AssemblerPokeOpcodeAddress(const uint16_t nBaseAddress) -> bool {
  int iAddressMode = g_asm_address_mode;  // opmode detected from input
  int nTargetValue = g_asm_target_value;

  int opcode = 0;
  int nOpcodes = static_cast<int>(g_asm_opcodes.size());

  for (opcode = 0; opcode < nOpcodes; opcode++) {
    int nOpcode = g_asm_opcodes.at(opcode);
    int nOpmode = g_opcodes[nOpcode].nAddressMode;

    if (nOpmode == iAddressMode) {
      *(mem + nBaseAddress) = static_cast<uint8_t>(nOpcode);
      int nOpbytes =
          AssemblerPokeAddress(nOpcode, nOpmode, nBaseAddress, nTargetValue);

      if (g_delayed_targets_dirty) {
        int nDelayedTargets = static_cast<int>(g_delayed_targets.size());
        DelayedTarget_t* pTarget =
            &g_delayed_targets.at(static_cast<size_t>(nDelayedTargets - 1));

        pTarget->opcode = nOpcode;
        pTarget->opmode = nOpmode;
      }

      g_assembler_address += nOpbytes;
      return true;
    }
  }

  return false;
}

//===========================================================================
auto TestFlag(AssemblerFlags_e eFlag) -> bool {
  return (g_asm_flags & eFlag) != 0;
}

//===========================================================================
void SetFlag(AssemblerFlags_e eFlag, bool bValue = true) {
  if (bValue) {
    g_asm_flags |= eFlag;
  } else {
    g_asm_flags &= ~eFlag;
  }
}

/*
        Output
                AM_IMPLIED
                AM_M
                AM_A
                AM_Z
                AM_I // indexed or indirect
*/
//===========================================================================
auto AssemblerGetArgs(int iArg, int nArgs, uint16_t nBaseAddress) -> bool {
  (void)nArgs;
  g_asm_address_mode = AM_IMPLIED;
  AssemblerState_e eNextState = AS_GET_MNEMONIC;

  g_asm_flags = 0;
  g_asm_target_address = 0;

  int nBase = 10;

  // Sync up to Raw Args for matching mnemonic
  // Process them instead of the cooked args, since we need the orginal tokens
  Arg_t* pArg = &g_arg_raw[iArg];

  while (iArg < g_arg_raw_count) {
    int iToken = pArg->eToken;

    if (iToken == TOKEN_HASH) {
      if (eNextState != AS_GET_MNEMONIC_PARM) {
        ConsoleBufferPush(" Syntax error: '#'");
        return false;
      }
      if (TestFlag(AF_HaveHash)) {
        ConsoleBufferPush(
            " Syntax error: Extra '#'");  // No thanks, we already have one
        return false;
      }
      SetFlag(AF_HaveHash);

      g_asm_address_mode = AM_M;  // Immediate
      eNextState = AS_GET_TARGET;
      g_asm_bytes = 1;
    } else if (iToken == TOKEN_DOLLAR) {
      if (TestFlag(AF_HaveDollar)) {
        ConsoleBufferPush(
            " Syntax error: Extra '$'");  // No thanks, we already have one
        return false;
      }

      nBase = 16;  // switch to hex

      if (!TestFlag(AF_HaveHash)) {
        SetFlag(AF_HaveDollar);
        g_asm_address_mode = AM_A;  // Absolute
      }
      eNextState = AS_GET_TARGET;
      g_asm_bytes = 2;
    } else if (iToken == TOKEN_PAREN_L) {
      if (TestFlag(AF_HaveLeftParen)) {
        ConsoleBufferPush(
            " Syntax error: Extra '('");  // No thanks, we already have one
        return false;
      }
      SetFlag(AF_HaveLeftParen);

      // Indexed or Indirect
      g_asm_address_mode = AM_I;
    } else if (iToken == TOKEN_PAREN_R) {
      if (TestFlag(AF_HaveRightParen)) {
        ConsoleBufferPush(
            " Syntax error: Extra ''");  // No thanks, we already have one
        return false;
      }
      SetFlag(AF_HaveRightParen);

      // Indexed or Indirect
      g_asm_address_mode = AM_I;
    } else if (iToken == TOKEN_COMMA) {
      if (TestFlag(AF_HaveComma)) {
        ConsoleBufferPush(
            " Syntax error: Extra ','");  // No thanks, we already have one
        return false;
      }
      SetFlag(AF_HaveComma);
      eNextState = AS_GET_INDEX;
      // We should have address by now
    } else if (iToken == TOKEN_LESS_THAN) {
    } else if (iToken == TOKEN_GREATER_THAN) {
    } else if (iToken == TOKEN_SEMI)  // comment
    {
      break;
    } else if (iToken == TOKEN_ALPHANUMERIC) {
      if (eNextState == AS_GET_MNEMONIC) {
        eNextState = AS_GET_MNEMONIC_PARM;
      } else if (eNextState == AS_GET_MNEMONIC_PARM) {
        eNextState = AS_GET_TARGET;
      }

      if (eNextState == AS_GET_TARGET) {
        SetFlag(AF_HaveTarget);

        ArgsGetValue(pArg, &g_asm_target_address, nBase);

        // Do Symbol Lookup
        uint16_t nSymbolAddress = 0;
        bool bExists = FindAddressFromSymbol(pArg->sArg, &nSymbolAddress);
        if (bExists) {
          g_asm_target_address = nSymbolAddress;

          if (g_asm_address_mode == AM_IMPLIED) {
            g_asm_address_mode = AM_A;
          }
        } else {
          // if valid hex address, don't have delayed target
          char sAddress[32];
          snprintf(sAddress, sizeof(sAddress), "%X", g_asm_target_address);
          if (strcmp(sAddress, pArg->sArg)) {
            DelayedTarget_t tDelayedTarget{};

            tDelayedTarget.base_address = nBaseAddress;
            Util_SafeStrCpy(tDelayedTarget.address_str, pArg->sArg,
                            MAX_SYMBOLS_LEN);

            // Flag this target that we need to update it when we have the
            // relevent info
            g_delayed_targets_dirty = true;

            tDelayedTarget.opcode = 0;
            tDelayedTarget.opmode = g_asm_address_mode;

            g_delayed_targets.push_back(tDelayedTarget);

            g_asm_target_address = 0;
          }
        }

        if ((g_asm_address_mode != AM_M) &&
            (g_asm_address_mode != AM_IMPLIED) && (!g_delayed_targets_dirty)) {
          if (g_asm_target_address <= _6502_ZEROPAGE_END) {
            g_asm_address_mode = AM_Z;
            g_asm_bytes = 1;
          }
        }
      }
      if (eNextState == AS_GET_INDEX) {
        if (pArg->nArgLen == 1) {
          if (pArg->sArg[0] == 'X') {
            if (!TestFlag(AF_HaveComma)) {
              ConsoleBufferPush(" Syntax error: Missing ','");
              return false;
            }
            SetFlag(AF_HaveRegisterX);
          }
          if (pArg->sArg[0] == 'Y') {
            if (!(TestFlag(AF_HaveComma))) {
              ConsoleBufferPush(" Syntax error: Missing ','");
              return false;
            }
            SetFlag(AF_HaveRegisterY);
          }
        }
      }
    }

    iArg++;
    pArg++;
  }

  return true;
}

//===========================================================================
auto AssemblerUpdateAddressingMode() -> bool {
  SetFlag(AF_HaveEitherParen,
          TestFlag(AF_HaveLeftParen) || TestFlag(AF_HaveRightParen));
  SetFlag(AF_HaveBothParen,
          TestFlag(AF_HaveLeftParen) && TestFlag(AF_HaveRightParen));

  if ((TestFlag(AF_HaveLeftParen)) && (!TestFlag(AF_HaveRightParen))) {
    ConsoleBufferPush(" Syntax error: Missing ''");
    return false;
  }

  if ((!TestFlag(AF_HaveLeftParen)) && (TestFlag(AF_HaveRightParen))) {
    ConsoleBufferPush(" Syntax error: Missing '('");
    return false;
  }

  if (TestFlag(AF_HaveComma)) {
    if ((!TestFlag(AF_HaveRegisterX)) && (!TestFlag(AF_HaveRegisterY))) {
      ConsoleBufferPush(" Syntax error: Index 'X' or 'Y'");
      return false;
    }
  }

  if (TestFlag(AF_HaveBothParen)) {
    if (TestFlag(AF_HaveComma)) {
      if (TestFlag(AF_HaveRegisterX)) {
        g_asm_address_mode = AM_AX;
        g_asm_bytes = 2;
        if (g_asm_target_address <= _6502_ZEROPAGE_END) {
          g_asm_address_mode = AM_ZX;
          g_asm_bytes = 1;
        }
      }
      if (TestFlag(AF_HaveRegisterY)) {
        g_asm_address_mode = AM_AY;
        g_asm_bytes = 2;
        if (g_asm_target_address <= _6502_ZEROPAGE_END) {
          g_asm_address_mode = AM_ZY;
          g_asm_bytes = 1;
        }
      }
    }
  }

  if ((g_asm_address_mode == AM_A) || (g_asm_address_mode == AM_Z)) {
    if (!TestFlag(AF_HaveEitherParen))  // if no paren
    {
      if (TestFlag(AF_HaveComma) && TestFlag(AF_HaveRegisterX)) {
        if (g_asm_address_mode == AM_Z) {
          g_asm_address_mode = AM_ZX;
        } else {
          g_asm_address_mode = AM_AX;
        }
      }
      if (TestFlag(AF_HaveComma) && TestFlag(AF_HaveRegisterY)) {
        if (g_asm_address_mode == AM_Z) {
          g_asm_address_mode = AM_ZY;
        } else {
          g_asm_address_mode = AM_AY;
        }
      }
    }
  }

  if (g_asm_address_mode == AM_I) {
    if (!TestFlag(AF_HaveEitherParen))  // if no paren
    {
      // Indirect Zero Page
      // Indirect Absolute
    }
  }

  g_asm_target_value = g_asm_target_address;

  int nOpcode = g_asm_opcodes.at(
      0);  // branch opcodes don't vary (only 1 Addressing Mode)
  if (_6502_CalcRelativeOffset(nOpcode, g_asm_base_address,
                               g_asm_target_address, &g_asm_target_value)) {
    if (g_asm_address_mode == NUM_OPMODES) {
      return false;
    }

    g_asm_address_mode = AM_R;
  }

  return true;
}

//===========================================================================
auto AssemblerDelayedTargetsSize() -> int {
  int nSize = static_cast<int>(g_delayed_targets.size());
  return nSize;
}

// The Assembler was terminated, with Symbol(s) declared, but not (yet) defined.
// i.e.
// A 300
//     BNE $DONE
// <enter>
//===========================================================================
void AssemblerProcessDelayedSymols() {
  g_delayed_targets_dirty =
      false;  // assembler set signal if new symbol was added

  bool bModified = false;
  while (!bModified) {
    bModified = false;

    std::vector<DelayedTarget_t>::iterator iSymbol;
    for (iSymbol = g_delayed_targets.begin();
         iSymbol != g_delayed_targets.end(); ++iSymbol) {
      DelayedTarget_t* pTarget = &(*iSymbol);

      uint16_t nTargetAddress = 0;
      bool bExists =
          FindAddressFromSymbol(pTarget->address_str, &nTargetAddress);
      if (bExists) {
        // TODO: need to handle #<symbol, #>symbol, symbol+n, symbol-n

        bModified = true;

        int nOpcode = pTarget->opcode;
        int nOpmode = g_opcodes[nOpcode].nAddressMode;

        // 300: D0 7E BNE $380
        // ^       ^      ^
        // |       |      TargetAddress
        // |       TargetValue
        // BaseAddress
        uint16_t nTargetValue = nTargetAddress;

        if (_6502_CalcRelativeOffset(nOpcode, pTarget->base_address,
                                     nTargetAddress, &nTargetValue)) {
          if (g_asm_address_mode == NUM_OPMODES) {
            nTargetValue = 0;
            bModified = false;
          }
        }

        if (bModified) {
          AssemblerPokeAddress(nOpcode, nOpmode, pTarget->base_address,
                               nTargetValue);
          *(memdirty + (pTarget->base_address >> 8)) |= 1;

          g_delayed_targets.erase(iSymbol);

          // iterators are invalid after the point of deletion
          // need to restart enumeration
          break;
        }
      }
    }

    if (!bModified) {
      break;
    }
  }
}

auto Assemble(int iArg, int nArgs, uint16_t address) -> bool {
  bool bGotArgs = false;
  bool bGotMode = false;
  bool bGotByte = false;

  // Since, making 2-passes is not an option,
  // we need to buffer the target address fix-ups.
  AssemblerProcessDelayedSymols();

  g_asm_base_address = address;

  char* pMnemonic = g_args[iArg].sArg;
  uint32_t nMnemonicHash = AssemblerHashMnemonic(pMnemonic);

#if DEBUG_ASSEMBLER
  char sText[CONSOLE_WIDTH * 2];
  ConsolePrintFormat(sText, "%s%04X%s: %s%s%s -> %s%08X", CHC_ADDRESS, address,
                     CHC_DEFAULT, CHC_STRING, pMnemonic, CHC_DEFAULT,
                     CHC_NUM_HEX, nMnemonicHash);
#endif

  g_asm_opcodes.clear();  // Candiate opcodes
  int opcode = 0;

  // Ugh! Linear search.
  for (opcode = 0; opcode < NUM_OPCODES; opcode++) {
    if (nMnemonicHash == g_opcodes_hash[opcode]) {
      g_asm_opcodes.push_back(opcode);
    }
  }

  int nOpcodes = static_cast<int>(g_asm_opcodes.size());
  if (!nOpcodes) {
    // Check for assembler directive

    ConsoleBufferPush(" Syntax error: Invalid mnemonic");
    return false;
  } else {
    bGotArgs = AssemblerGetArgs(iArg, nArgs, address);
    if (bGotArgs) {
      bGotMode = AssemblerUpdateAddressingMode();
      if (bGotMode) {
        bGotByte = AssemblerPokeOpcodeAddress(address);
        (void)bGotByte;
      }
    }
  }

  return true;
}

//===========================================================================
void AssemblerOn() {
  g_assembler_input = true;
  g_console_prompt_str[0] = g_console_prompt[PROMPT_ASSEMBLER];
}

//===========================================================================
void AssemblerOff() {
  g_assembler_input = false;
  g_console_prompt_str[0] = g_console_prompt[PROMPT_COMMAND];
}

// Window
// _________________________________________________________________________________________
extern int g_window_last;
extern int g_window_this;
extern WindowSplit_t g_window_config[NUM_WINDOWS];

// Zero Page Pointers
// _____________________________________________________________________________
extern int g_zero_page_pointers_count;
extern ZeroPagePointers_t g_zero_page_pointers[MAX_ZEROPAGE_POINTERS];
