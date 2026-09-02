// SPDX-License-Identifier: GPL-2.0-only
#pragma once

// NOLINTBEGIN(modernize-use-using, cppcoreguidelines-use-enum-class,
// modernize-use-trailing-return-type, modernize-deprecated-headers,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays)
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct DbgRegisters_t {
  uint8_t a;
  uint8_t x;
  uint8_t y;
  uint8_t ps;
  uint16_t pc;
  uint16_t sp;
  uint8_t is_jammed;
};

struct DbgDisasmLine_t {
  uint16_t address;
  uint8_t length;
  uint8_t opcodes[4];
  char symbol[32];
  char mnemonic[64];
};

struct DbgBreakpoint_t {
  uint16_t address;
  uint8_t type;
  uint8_t enabled;
};

void dbg_initialize(void);
void dbg_shutdown(void);
void dbg_execute_command(const char* cmd_str);
int dbg_is_active(void);
void dbg_set_active(int active);

void dbg_step(void);
void dbg_continue(void);

void dbg_get_registers(struct DbgRegisters_t* out_regs);
void dbg_set_registers(const struct DbgRegisters_t* regs);
uint8_t dbg_peek_memory(uint16_t addr);
void dbg_poke_memory(uint16_t addr, uint8_t val);
size_t dbg_get_memory_range(uint16_t addr, uint8_t* out_buffer, size_t count);

size_t dbg_get_disassembly(uint16_t start_addr, size_t count,
                           struct DbgDisasmLine_t* out_lines);
size_t dbg_get_breakpoints(struct DbgBreakpoint_t* out_breakpoints,
                           size_t max_count);
void dbg_add_breakpoint(uint16_t addr, uint8_t type);
void dbg_remove_breakpoint(uint16_t addr);

size_t dbg_get_console_logs(char* out_buffer, size_t max_size);
void dbg_clear_console_logs(void);

#ifdef __cplusplus
}
#endif

// NOLINTEND(modernize-use-using, cppcoreguidelines-use-enum-class,
// modernize-use-trailing-return-type, modernize-deprecated-headers,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays)
