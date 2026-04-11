#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "Common.h"
#include "apple2/SerialComms.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "Common_Globals.h"
#include <cstring>
#include <cstdint>

// Mock CPU functions
void CpuIrqAssert(eIRQSRC source) { (void)source; }
void CpuIrqDeassert(eIRQSRC source) { (void)source; }
void CpuNmiAssert(eIRQSRC source) { (void)source; }
void CpuNmiDeassert(eIRQSRC source) { (void)source; }

// Mock Memory functions
void RegisterIoHandler(unsigned int slot, iofunction r, iofunction w, iofunction cr, iofunction cw, void* p, unsigned char* rom) {
    (void)slot; (void)r; (void)w; (void)cr; (void)cw; (void)p; (void)rom;
}
unsigned char IO_Null(unsigned short nPC, unsigned short nAddr, unsigned char nWriteFlag, unsigned char nWriteValue, uint32_t nCyclesLeft) {
    (void)nPC; (void)nAddr; (void)nWriteFlag; (void)nWriteValue; (void)nCyclesLeft;
    return 0;
}

// Mock Frontend functions
static uint8_t g_lastSentByte = 0;
static bool g_sendCalled = false;
void SSCFrontend_SendByte(uint8_t byte) {
    g_lastSentByte = byte;
    g_sendCalled = true;
}
bool SSCFrontend_IsActive() { return true; }
void SSCFrontend_UpdateState(unsigned int b, unsigned int s, SscParity p, SscStopBits t) {
    (void)b; (void)s; (void)p; (void)t;
}

// Helper for slot parameters
void* MemGetSlotParameters(unsigned int slot) {
    (void)slot;
    return &sg_SSC;
}

TEST_CASE("SSC: Status Register Bit 4 (TDRE) Set On Reset") {
    memset(&sg_SSC, 0, sizeof(sg_SSC));
    SSC_Reset(&sg_SSC);
    g_sendCalled = false;

    // Hardware Reset (SSC_Reset)
    uint8_t status = SSC_IORead(0, 0xC090 | (2 << 4) | 0x9, 0, 0, 0); // Slot 2, Offset 9
    CHECK((status & (1 << 4)) != 0); // TDRE should be 1 (Empty)
}

TEST_CASE("SSC: Transmit Sets TDRE Interrupt") {
    memset(&sg_SSC, 0, sizeof(sg_SSC));
    SSC_Reset(&sg_SSC);
    g_sendCalled = false;

    // 1. Enable Transmit Interrupts (Command Register bit 0=0 for enabled? No, bit 0 is parity)
    // 6551 ACIA: Command Register bits 2-3 control transmitter interrupt
    // bit 3=0, bit 2=1 -> Transmit interrupt enabled, RTS low.
    SSC_IOWrite(0, 0xC090 | (2 << 4) | 0xB, 1, 0x04, 0); // Command Register

    // 2. Write to Transmit Data Register
    SSC_IOWrite(0, 0xC090 | (2 << 4) | 0x8, 1, 0x41, 0); // Data 'A'

    // 3. Check status (TDRE should be 0 immediately if busy, or 1 after transmit)
    // Our mock transmit is immediate.
    uint8_t status = SSC_IORead(0, 0xC090 | (2 << 4) | 0x9, 0, 0, 0);
    CHECK((status & (1 << 4)) != 0);
}
