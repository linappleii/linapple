// SPDX-License-Identifier: GPL-2.0-only
#include "SuperSerialFrontend.h"

#include <fcntl.h>
#include <pthread.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"

namespace {

static int g_hCommHandle = -1;
static std::string g_sSerialPortPath = "";
static bool g_bSerialLoopback = false;
static uint32_t g_dwCommInactivity = 0;
static pthread_mutex_t g_CriticalSection = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_CommThread;
static volatile bool g_bThreadRunning = false;
static volatile bool g_bThreadTerminate = false;

auto SuperSerialFrontend_UpdateCommState(uint32_t baud, uint32_t bits,
                                         SuperSerialParity_e parity,
                                         SuperSerialStopBits_e stop) -> void {
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
    case SUPER_SERIAL_PARITY_NONE:
      dcb.c_cflag &= ~PARENB;
      break;
    case SUPER_SERIAL_PARITY_EVEN:
      dcb.c_cflag |= PARENB;
      dcb.c_cflag &= ~PARODD;
      break;
    case SUPER_SERIAL_PARITY_ODD:
      dcb.c_cflag |= PARENB;
      dcb.c_cflag |= PARODD;
      break;
    case SUPER_SERIAL_PARITY_MARK:
#ifdef CMSPAR
      dcb.c_cflag |= (PARENB | CMSPAR | PARODD);
#else
      dcb.c_cflag |= (PARENB | PARODD);
#endif
      break;
    case SUPER_SERIAL_PARITY_SPACE:
#ifdef CMSPAR
      dcb.c_cflag |= (PARENB | CMSPAR);
      dcb.c_cflag &= ~PARODD;
#else
      dcb.c_cflag |= PARENB;
      dcb.c_cflag &= ~PARODD;
#endif
      break;
  }

  switch (bits) {
    case 5:
      l_databits = CS5;
      break;
    case 6:
      l_databits = CS6;
      break;
    case 7:
      l_databits = CS7;
      break;
    case 8:
    default:
      l_databits = CS8;
      break;
  }
  dcb.c_cflag &= ~CSIZE;
  dcb.c_cflag |= l_databits;

  switch (stop) {
    case SUPER_SERIAL_STOP_BITS_1_5:
    case SUPER_SERIAL_STOP_BITS_1:
      dcb.c_cflag &= ~CSTOPB;
      break;
    case SUPER_SERIAL_STOP_BITS_2:
      dcb.c_cflag |= CSTOPB;
      break;
  }
  dcb.c_cflag &= ~CRTSCTS;
  dcb.c_lflag &= ~(ICANON | ECHO | ISIG);
  tcsetattr(g_hCommHandle, TCSANOW, &dcb);
}

auto SerialPollingThread(void* arg) -> void* {
  (void)arg;
  std::array<uint8_t, 256> buffer{};

  while (!g_bThreadTerminate) {
    if (g_hCommHandle != -1) {
      const ssize_t n = read(g_hCommHandle, buffer.data(), buffer.size());
      if (n > 0) {
        pthread_mutex_lock(&g_CriticalSection);
        for (ssize_t i = 0; i < n; ++i) {
          uint8_t byte = buffer.at(static_cast<size_t>(i));
          Peripheral_Command(2, SUPER_SERIAL_CMD_PUSH_RX_BYTE, &byte,
                             sizeof(uint8_t));
        }
        pthread_mutex_unlock(&g_CriticalSection);
      }
    }
    usleep(1000);  // Poll every 1ms
  }
  return nullptr;
}

auto SuperSerialFrontend_TransmitByte(uint8_t byte) -> bool {
  if (g_bSerialLoopback) {
    pthread_mutex_lock(&g_CriticalSection);
    Peripheral_Command(2, SUPER_SERIAL_CMD_PUSH_RX_BYTE, &byte,
                       sizeof(uint8_t));
    pthread_mutex_unlock(&g_CriticalSection);
    return true;
  }

  if (!SuperSerialFrontend_IsActive()) {
    return false;
  }
  return write(g_hCommHandle, &byte, 1) == 1;
}

}  // namespace

auto SuperSerialFrontend_IsActive() -> bool {
  if (g_bSerialLoopback) {
    return true;
  }

  if ((g_hCommHandle == -1) && !g_sSerialPortPath.empty()) {
    g_hCommHandle =
        open(g_sSerialPortPath.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (g_hCommHandle != -1) {
      if (!g_bThreadRunning) {
        g_bThreadTerminate = false;
        if (pthread_create(&g_CommThread, nullptr, SerialPollingThread,
                           nullptr) == 0) {
          g_bThreadRunning = true;
        }
      }
    }
  }
  return (g_hCommHandle != -1);
}

auto SuperSerialFrontend_UpdateState(uint32_t baud, uint32_t bits, int parity,
                                     int stop) -> void {
  SuperSerialFrontend_UpdateCommState(baud, bits,
                                      static_cast<SuperSerialParity_e>(parity),
                                      static_cast<SuperSerialStopBits_e>(stop));
}

auto SuperSerialFrontend_Close() -> void {
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

auto SuperSerialFrontend_SetSerialPortPath(const char* path) -> void {
  if (g_hCommHandle == -1) {
    g_sSerialPortPath = path ? path : "";
  } else {
    fprintf(stderr, "You cannot change the serial port while it is in use!\n");
  }
}

auto SuperSerialFrontend_SetLoopback(bool enable) -> void {
  g_bSerialLoopback = enable;
}

auto SuperSerialFrontend_SendByte(uint8_t byte) -> void {
  SuperSerialFrontend_TransmitByte(byte);
}
