#include <gtest/gtest.h>
#include <stdbool.h>
#include <string.h>

#include "Common.h"
#include "SerialComms.h"
#include "CPU.h"
#include <cstring>

// Mock CPU functions
void CpuIrqAssert(eIRQSRC source) {}
void CpuIrqDeassert(eIRQSRC source) {}
void CpuNmiAssert(eIRQSRC source) {}
void CpuNmiDeassert(eIRQSRC source) {}

// Mock Memory functions
void RegisterIoHandler(unsigned int slot, iofunction r, iofunction w, iofunction cr, iofunction cw, void* p, unsigned char* rom) {}
unsigned char IO_Null(unsigned short nPC, unsigned short nAddr, unsigned char nWriteFlag, unsigned char nWriteValue, uint32_t nCyclesLeft) { return 0; }

// Mock Frontend functions
static uint8_t g_lastSentByte = 0;
static bool g_sendCalled = false;
void SSCFrontend_SendByte(uint8_t byte) {
    g_lastSentByte = byte;
    g_sendCalled = true;
}
bool SSCFrontend_IsActive() { return true; }
void SSCFrontend_UpdateState(unsigned int, unsigned int, SscParity, SscStopBits) {}

// Helper for slot parameters
void* MemGetSlotParameters(unsigned int slot) {
    return &sg_SSC;
}

class SscTest : public ::testing::Test {
protected:
    void SetUp() override {
        memset(&sg_SSC, 0, sizeof(sg_SSC));
        SSC_Reset(&sg_SSC);
        g_sendCalled = false;
    }
};

TEST_F(SscTest, StatusRegisterBit4_TDRE_SetOnReset) {
    // Hardware Reset (SSC_Reset)
    uint8_t status = SSC_IORead(0, 0xC090 | (2 << 4) | 0x9, 0, 0, 0); // Slot 2, Offset 9
    EXPECT_TRUE(status & (1 << 4)); // TDRE should be 1 (Empty)
}

TEST_F(SscTest, TransmitSetsTDREInterrupt) {
    // Enable Transmit Interrupts (Command Reg bits 2-3 = 01)
    SSC_IOWrite(0, 0xC090 | (2 << 4) | 0xA, 1, 0x04, 0); // Slot 2, Offset A
    
    // Transmit a byte
    SSC_IOWrite(0, 0xC090 | (2 << 4) | 0x8, 1, 'A', 0); // Slot 2, Offset 8
    EXPECT_TRUE(g_sendCalled);
    EXPECT_EQ(g_lastSentByte, 'A');

    // Read status, bit 7 should be set (IRQ) and bit 4 (TDRE)
    uint8_t status = SSC_IORead(0, 0xC090 | (2 << 4) | 0x9, 0, 0, 0);
    EXPECT_TRUE(status & (1 << 7)); // IRQ
    EXPECT_TRUE(status & (1 << 4)); // TDRE
    
    // Second read, IRQ should be cleared (TDRE interrupt cleared)
    status = SSC_IORead(0, 0xC090 | (2 << 4) | 0x9, 0, 0, 0);
    EXPECT_FALSE(status & (1 << 7));
}

TEST_F(SscTest, ReceiveSetsRDRFAndIRQ) {
    // Enable Receive Interrupts (Command Reg bit 1 = 0)
    SSC_IOWrite(0, 0xC090 | (2 << 4) | 0xA, 1, 0x00, 0);
    
    // Push a byte from "frontend"
    SSC_PushRxByte(&sg_SSC, 'X');
    
    // Read status, bit 3 should be set (RDRF) and bit 7 (IRQ)
    uint8_t status = SSC_IORead(0, 0xC090 | (2 << 4) | 0x9, 0, 0, 0);
    EXPECT_TRUE(status & (1 << 3)); // RDRF
    EXPECT_TRUE(status & (1 << 7)); // IRQ
    
    // Read data
    uint8_t data = SSC_IORead(0, 0xC090 | (2 << 4) | 0x8, 0, 0, 0);
    EXPECT_EQ(data, 'X');
    
    // Read status again, RDRF and IRQ should be cleared
    status = SSC_IORead(0, 0xC090 | (2 << 4) | 0x9, 0, 0, 0);
    EXPECT_FALSE(status & (1 << 3));
    EXPECT_FALSE(status & (1 << 7));
}

TEST_F(SscTest, ProgrammedReset) {
    // Set some state
    SSC_IOWrite(0, 0xC090 | (2 << 4) | 0xA, 1, 0xFF, 0); // Command = FF
    SSC_PushRxByte(&sg_SSC, 'Y');
    
    // Write to Status offset (Programmed Reset)
    SSC_IOWrite(0, 0xC090 | (2 << 4) | 0x9, 1, 0, 0);
    
    // Command Register bits 0-4 should be cleared (FF -> E0)
    uint8_t cmd = SSC_IORead(0, 0xC090 | (2 << 4) | 0xA, 0, 0, 0);
    EXPECT_EQ(cmd, 0xE0);
    
    // Receive buffer should be empty
    uint8_t status = SSC_IORead(0, 0xC090 | (2 << 4) | 0x9, 0, 0, 0);
    EXPECT_FALSE(status & (1 << 3)); // RDRF = 0
    EXPECT_FALSE(status & (1 << 7)); // IRQ = 0
}
