#include "apple2/SerialComms.h"
#include <cstdint>

// These functions are currently coupled to the frontend in LinAppleCore.cpp or hardware files
// but should eventually be part of the bridge or a specialized frontend component.

bool g_bDSAvailable = false;

void SSCFrontend_Update(struct SuperSerialCard* pSSC, uint32_t cycles) { (void)pSSC; (void)cycles; }
void PrinterFrontend_Update(uint32_t cycles) { (void)cycles; }
void UpdateDisplay(int flags) { (void)flags; }

// Printer Frontend stubs
unsigned char PrinterFrontend_CheckStatus() { return 0; }
void PrinterFrontend_SendChar(unsigned char c) { (void)c; }
void PrinterFrontend_Destroy() {}

// SSC Frontend stubs
void SSCFrontend_SendByte(uint8_t byte) { (void)byte; }
bool SSCFrontend_IsActive() { return false; }
void SSCFrontend_UpdateState(unsigned int baud, unsigned int bits, SscParity parity, SscStopBits stop) {
    (void)baud; (void)bits; (void)parity; (void)stop;
}

// UI stubs
void FrameRefreshStatus(int flags) { (void)flags; }
void DrawFrameWindow() {}
void DrawAppleContent() {}

// Legacy sound stubs
void DSInit() {}
void DSShutdown() {}
