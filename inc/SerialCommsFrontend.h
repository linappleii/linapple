#ifndef SERIALCOMMSFRONTEND_H
#define SERIALCOMMSFRONTEND_H

#include <cstdint>
#include "SerialComms.h"

// Initialize the host-side serial port
bool SSCFrontend_Initialize(unsigned int serialPort);

// Close the host-side serial port
void SSCFrontend_Close();

// Update host-side serial communication
void SSCFrontend_Update(SuperSerialCard* pSSC, uint32_t totalCycles);

// Send a byte to the host-side serial port (called by core)
bool SSCFrontend_TransmitByte(uint8_t byte);

// Check for and receive a byte from the host-side serial port
// (called by frontend update, which then pushes to core)
bool SSCFrontend_CheckReceive(SuperSerialCard* pSSC);

// Set the serial port number
void SSCFrontend_SetSerialPort(unsigned int serialPort);

// Get the serial port number
unsigned int SSCFrontend_GetSerialPort();

#endif // SERIALCOMMSFRONTEND_H
