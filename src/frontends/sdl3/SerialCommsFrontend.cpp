#include "SerialCommsFrontend.h"
#include "apple2/peripherals/SerialCommsCommands.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <string>
#include <array>

static int g_hCommHandle = -1;
static std::string g_sSerialPortPath = "";
static bool g_bSerialLoopback = false;
static uint32_t g_dwCommInactivity = 0;
static pthread_mutex_t g_CriticalSection = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_CommThread;
static volatile bool g_bThreadRunning = false;
static volatile bool g_bThreadTerminate = false;

void SSCFrontend_UpdateCommState(uint32_t baud, uint32_t bits, SscParity parity, SscStopBits stop) {
  if (g_hCommHandle == -1) {
    return;
  }

  struct termios dcb{};
  int l_databits = CS8;
  tcgetattr(g_hCommHandle, &dcb);

  cfsetispeed(&dcb, baud);
  cfsetospeed(&dcb, baud);
  dcb.c_cflag |= (CLOCAL | CREAD);

  switch (parity) {
    case SSC_PARITY_NONE:
      dcb.c_cflag &= ~PARENB;
      break;
    case SSC_PARITY_EVEN:
      dcb.c_cflag |= PARENB;
      dcb.c_cflag &= ~PARODD;
      break;
    case SSC_PARITY_ODD:
      dcb.c_cflag |= PARENB;
      dcb.c_cflag |= PARODD;
      break;
    case SSC_PARITY_MARK:
#ifdef CMSPAR
      dcb.c_cflag |= PARENB | CMSPAR | PARODD;
#else
      dcb.c_cflag |= PARENB | PARODD;
#endif
      break;
    case SSC_PARITY_SPACE:
#ifdef CMSPAR
      dcb.c_cflag |= PARENB | CMSPAR;
      dcb.c_cflag &= ~PARODD;
#else
      dcb.c_cflag |= PARENB;
      dcb.c_cflag &= ~PARODD;
#endif
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
    case SSC_STOP_BITS_1_5:
    case SSC_STOP_BITS_1:
      dcb.c_cflag &= ~CSTOPB;
      break;
    case SSC_STOP_BITS_2:
      dcb.c_cflag |= CSTOPB;
      break;
  }
  dcb.c_cflag &= ~CRTSCTS;
  dcb.c_lflag &= ~(ICANON | ECHO | ISIG);
  tcsetattr(g_hCommHandle, TCSANOW, &dcb);
}

static auto SerialPollingThread(void* arg) -> void* {
    (void)arg;
    std::array<uint8_t, 256> buffer;

    while (!g_bThreadTerminate) {
        if (g_hCommHandle != -1) {
            int n = static_cast<int>(read(g_hCommHandle, buffer.data(), buffer.size()));
            if (n > 0) {
                pthread_mutex_lock(&g_CriticalSection);
                for (int i = 0; i < n; ++i) {
                    Peripheral_Command(2, SSC_CMD_PUSH_RX_BYTE, &buffer[static_cast<size_t>(i)], 1);
                }
                pthread_mutex_unlock(&g_CriticalSection);
            }
        }
        usleep(1000); // Poll every 1ms
    }
    return nullptr;
}

auto SSCFrontend_IsActive() -> bool {
  if (g_bSerialLoopback) return true;

  if ((g_hCommHandle == -1) && !g_sSerialPortPath.empty()) {
    g_hCommHandle = open(g_sSerialPortPath.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (g_hCommHandle != -1) {
        // Start polling thread if not already running
        if (!g_bThreadRunning) {
            g_bThreadTerminate = false;
            if (pthread_create(&g_CommThread, nullptr, SerialPollingThread, nullptr) == 0) {
                g_bThreadRunning = true;
            }
        }
    }
  }
  return (g_hCommHandle != -1);
}

void SSCFrontend_UpdateState(uint32_t baud, uint32_t bits, int parity, int stop) {
  SSCFrontend_UpdateCommState(baud, bits, static_cast<SscParity>(parity), static_cast<SscStopBits>(stop));
}

void SSCFrontend_Close() {
  if (g_bThreadRunning) {
      g_bThreadTerminate = true;
      pthread_join(g_CommThread, nullptr);
      g_bThreadRunning = false;
  }

  if (g_hCommHandle != -1) {
    close(g_hCommHandle);
  }
  g_hCommHandle = -1;
  g_dwCommInactivity = 0;
}

void SSCFrontend_Update(SuperSerialCard* pSSC, uint32_t totalcycles) {
  (void)pSSC;
  (void)totalcycles;
  if (!SSCFrontend_IsActive()) {
    return;
  }

  if ((g_dwCommInactivity += totalcycles) > 1000000) {
    if (Peripheral_IsAnyActive()) {
      g_dwCommInactivity = 0;
    }
  }
}

auto SSCFrontend_TransmitByte(uint8_t byte) -> bool {
  if (g_bSerialLoopback) {
      pthread_mutex_lock(&g_CriticalSection);
      Peripheral_Command(2, SSC_CMD_PUSH_RX_BYTE, &byte, 1);
      pthread_mutex_unlock(&g_CriticalSection);
      return true;
  }

  if (!SSCFrontend_IsActive()) return false;
  return write(g_hCommHandle, &byte, 1) == 1;
}

auto SSCFrontend_CheckReceive(SuperSerialCard* pSSC) -> bool {
  (void)pSSC;
  // Now handled by SerialPollingThread
  return false;
}

void SSCFrontend_SetSerialPortPath(const char* path) {
  if (g_hCommHandle == -1) {
    g_sSerialPortPath = path ? path : "";
  } else {
    fprintf(stderr, "You cannot change the serial port while it is in use!\n");
  }
}

void SSCFrontend_SetLoopback(bool enable) {
    g_bSerialLoopback = enable;
}

// These are called by core to send data out
void SSCFrontend_SendByte(uint8_t byte) {
    SSCFrontend_TransmitByte(byte);
}
