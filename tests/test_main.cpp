#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "Common.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <cstdint>
#include "doctest.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "Common_Globals.h"

// The emulator currently uses a global state.
// To make it testable, we provide a clean initialization for every test.
void reset_machine() {
    static bool sdl_init = false;
    if (!sdl_init) {
        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK);
        sdl_init = true;
    }
    
    g_Apple2Type = A2TYPE_APPLE2EENHANCED;
    MemInitialize();
    CpuInitialize();
}

// Helper to run a 6502 snippet
void machine_execute(const uint8_t* code, size_t size, uint32_t max_cycles = 1000) {
    memcpy(mem + 0x0300, code, size);
    regs.pc = 0x0300;
    
    uint32_t cycles = 0;
    while (mem[regs.pc] != 0x60 && cycles < max_cycles) {
        CpuExecute(1);
        cycles++;
    }
}

TEST_CASE("Legacy: [MEM-01] Language Card RAM Banking") {
    reset_machine();

    // 1. Double-read $C081 to enable RAM Write
    // 2. Write $55 to $D000
    // 3. Single-read $C080 to enable RAM Read
    // 4. Read $D000 into Accumulator
    // 5. RTS
    uint8_t code[] = {
        0xAD, 0x81, 0xC0, // LDA $C081
        0xAD, 0x81, 0xC0, // LDA $C081 (Write Enable ON)
        0xA9, 0x55,       // LDA #$55
        0x8D, 0x00, 0xD0, // STA $D000
        0xAD, 0x80, 0xC0, // LDA $C080 (Read Enable ON)
        0xAD, 0x00, 0xD0, // LDA $D000
        0x60              // RTS
    };

    machine_execute(code, sizeof(code));
    CHECK(regs.a == 0x55);
}

TEST_CASE("Legacy: [ROM-01] Firmware Integrity (Autostart ROM)") {
    reset_machine();
    
    // Requirement: entry point $FF65 must exist (Monitor)
    // We check the memory directly
    CHECK(mem[0xFF65] != 0x00);
}
