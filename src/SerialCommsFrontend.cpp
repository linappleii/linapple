#include "stdafx.h"
#include "SerialCommsFrontend.h"
#include "SerialComms.h"
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <pthread.h>

static int g_hCommHandle = -1;
static unsigned int g_dwSerialPort = 0;
static unsigned int g_dwCommInactivity = 0;
static pthread_mutex_t g_CriticalSection = PTHREAD_MUTEX_INITIALIZER;

extern bool DiskIsSpinning(); // from Disk.cpp or elsewhere

void SSCFrontend_UpdateCommState(unsigned int baud, unsigned int bits, unsigned int parity, unsigned int stop) {
  if (g_hCommHandle == -1) {
    return;
  }

  struct termios dcb;
  int l_databits = CS8;
  tcgetattr(g_hCommHandle, &dcb);

  cfsetispeed(&dcb, baud);
  cfsetospeed(&dcb, baud);
  dcb.c_cflag |= (CLOCAL | CREAD);

  switch (parity) {
    case NOPARITY:
      dcb.c_cflag &= ~PARENB;
      break;
    case EVENPARITY:
      dcb.c_cflag |= PARENB;
      dcb.c_cflag &= ~PARODD;
      break;
    case ODDPARITY:
      dcb.c_cflag |= PARENB;
      dcb.c_cflag |= PARODD;
      break;
    case MARKPARITY:
      dcb.c_cflag |= PARENB | CMSPAR | PARODD;
      break;
    case SPACEPARITY:
      dcb.c_cflag |= PARENB | CMSPAR;
      dcb.c_cflag &= ~PARODD;
      break;
  }

  switch (bits) {
    case 5: l_databits = CS5; break;
    case 6: l_databits = CS6; break;
    case 7: l_databits = CS7; break;
    case 8: l_databits = CS8; break;
  }
  dcb.c_cflag &= ~CSIZE;
  dcb.c_cflag |= l_databits;

  switch (stop) {
    case ONE5STOPBITS:
    case ONESTOPBIT:
      dcb.c_cflag &= ~CSTOPB;
      break;
    case TWOSTOPBITS:
      dcb.c_cflag |= CSTOPB;
      break;
  }
  dcb.c_cflag &= ~CRTSCTS;
  dcb.c_lflag &= ~(ICANON | ECHO | ISIG);
  tcsetattr(g_hCommHandle, TCSANOW, &dcb);
}

bool SSCFrontend_IsActive() {
  if ((g_hCommHandle == -1) && g_dwSerialPort) {
    char portname[12];
    if (g_dwSerialPort > 99) {
      g_dwSerialPort = 1;
    }
    sprintf(portname, "/dev/ttyS%u", (unsigned int) (g_dwSerialPort - 1));
    g_hCommHandle = open(portname, O_RDWR | O_NOCTTY | O_NDELAY);
    // If it was opened, the first call to SSCFrontend_UpdateState will configure it
  }
  return (g_hCommHandle != -1);
}

void SSCFrontend_UpdateState(unsigned int baud, unsigned int bits, unsigned int parity, unsigned int stop) {
  SSCFrontend_UpdateCommState(baud, bits, parity, stop);
}

void SSCFrontend_Close() {
  if (g_hCommHandle != -1) {
    close(g_hCommHandle);
  }
  g_hCommHandle = -1;
  g_dwCommInactivity = 0;
}

void SSCFrontend_Update(SuperSerialCard* pSSC, uint32_t totalcycles) {
  if (g_hCommHandle == -1) {
    return;
  }

  if ((g_dwCommInactivity += totalcycles) > 1000000) {
    if (DiskIsSpinning())
      g_dwCommInactivity = 0;
  }

  // Poll for data
  SSCFrontend_CheckReceive(pSSC);
}

bool SSCFrontend_TransmitByte(uint8_t byte) {
  if (!SSCFrontend_IsActive()) return false;
  return write(g_hCommHandle, &byte, 1) == 1;
}

bool SSCFrontend_CheckReceive(SuperSerialCard* pSSC) {
  if (g_hCommHandle == -1) return false;

  uint8_t buffer[1];
  pthread_mutex_lock(&g_CriticalSection);
  int n = read(g_hCommHandle, buffer, 1);
  pthread_mutex_unlock(&g_CriticalSection);

  if (n > 0) {
    SSC_PushRxByte(pSSC, buffer[0]);
    return true;
  }
  return false;
}

void SSCFrontend_SetSerialPort(unsigned int serialPort) {
  if (g_hCommHandle == -1) {
    g_dwSerialPort = serialPort;
  } else {
    fprintf(stderr, "You cannot change the serial port while it is in use!\n");
  }
}

unsigned int SSCFrontend_GetSerialPort() {
  return g_dwSerialPort;
}

// These are called by core to send data out
void SSCFrontend_SendByte(uint8_t byte) {
    SSCFrontend_TransmitByte(byte);
}
