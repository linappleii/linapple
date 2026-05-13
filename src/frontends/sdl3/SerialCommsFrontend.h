#ifndef SERIALCOMMSFRONTEND_H
#define SERIALCOMMSFRONTEND_H

#include "apple2/peripherals/ssc/SerialComms.h"

// Initialize the host-side serial port
auto SSCFrontend_Initialize(const char* serialPortPath) -> bool;

// Close the host-side serial port
void SSCFrontend_Close();

// Update host-side serial communication
void SSCFrontend_Update(SuperSerialCard* pSSC, uint32_t totalCycles);

// Send a byte to the host-side serial port (called by core)
void SSCFrontend_SendByte(uint8_t byte);

// Check for and receive a byte from the host-side serial port
auto SSCFrontend_CheckReceive(SuperSerialCard* pSSC) -> bool;

// Set the serial port path
void SSCFrontend_SetSerialPortPath(const char* serialPortPath);

// Set loopback mode
void SSCFrontend_SetLoopback(bool enable);

#endif // SERIALCOMMSFRONTEND_H
