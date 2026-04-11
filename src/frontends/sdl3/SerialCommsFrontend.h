#ifndef SERIALCOMMSFRONTEND_H
#define SERIALCOMMSFRONTEND_H

#include "apple2/SerialComms.h"

// Initialize the host-side serial port
bool SSCFrontend_Initialize(const char* serialPortPath);

// Close the host-side serial port
void SSCFrontend_Close();

// Update host-side serial communication
void SSCFrontend_Update(SuperSerialCard* pSSC, uint32_t totalCycles);

// Send a byte to the host-side serial port (called by core)
void SSCFrontend_SendByte(uint8_t byte);

// Check for and receive a byte from the host-side serial port
bool SSCFrontend_CheckReceive(SuperSerialCard* pSSC);

// Set the serial port path
void SSCFrontend_SetSerialPortPath(const char* serialPortPath);

// Set loopback mode
void SSCFrontend_SetLoopback(bool enable);

#endif // SERIALCOMMSFRONTEND_H
