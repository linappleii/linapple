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
#include "core/Util_Path.h"

namespace {

static int g_comm_handle = -1;
static std::string g_serial_port_path = "";
static bool g_serial_loopback = false;
static uint32_t g_comm_inactivity = 0;
static pthread_mutex_t g_critical_section = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_comm_thread;
static volatile bool g_thread_running = false;
static volatile bool g_thread_terminate = false;

auto super_serial_frontend_update_comm_state(uint32_t baud, uint32_t bits,
                                             SuperSerialParity_t parity,
                                             SuperSerialStopBits_t stop)
    -> void {
  if (g_comm_handle == -1) {
    return;
  }

  struct termios dcb{};
  int l_databits = CS8;
  tcgetattr(g_comm_handle, &dcb);

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
    default:
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
    default:
      break;
  }
  dcb.c_cflag &= ~CRTSCTS;
  dcb.c_lflag &= ~(ICANON | ECHO | ISIG);
  tcsetattr(g_comm_handle, TCSANOW, &dcb);
}

auto serial_polling_thread(void* arg) -> void* {
  (void)arg;
  std::array<uint8_t, 256> buffer{};

  while (!g_thread_terminate) {
    if (g_comm_handle != -1) {
      const ssize_t n = read(g_comm_handle, buffer.data(), buffer.size());
      if (n > 0) {
        pthread_mutex_lock(&g_critical_section);
        for (ssize_t i = 0; i < n; ++i) {
          uint8_t byte = buffer.at(static_cast<size_t>(i));
          peripheral_command(2, SUPER_SERIAL_CMD_PUSH_RX_BYTE, &byte,
                             sizeof(uint8_t));
        }
        pthread_mutex_unlock(&g_critical_section);
      }
    }
    usleep(1000);  // Poll every 1ms
  }
  return nullptr;
}

auto super_serial_frontend_transmit_byte(uint8_t byte) -> bool {
  if (g_serial_loopback) {
    pthread_mutex_lock(&g_critical_section);
    peripheral_command(2, SUPER_SERIAL_CMD_PUSH_RX_BYTE, &byte,
                       sizeof(uint8_t));
    pthread_mutex_unlock(&g_critical_section);
    return true;
  }

  if (!super_serial_frontend_is_active()) {
    return false;
  }
  return write(g_comm_handle, &byte, 1) == 1;
}

}  // namespace

auto super_serial_frontend_is_active() -> bool {
  if (g_serial_loopback) {
    return true;
  }

  if ((g_comm_handle == -1) && !g_serial_port_path.empty()) {
    g_comm_handle =
        open(g_serial_port_path.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (g_comm_handle != -1) {
      if (!g_thread_running) {
        g_thread_terminate = false;
        if (pthread_create(&g_comm_thread, nullptr, serial_polling_thread,
                           nullptr) == 0) {
          g_thread_running = true;
        }
      }
    }
  }
  return (g_comm_handle != -1);
}

auto super_serial_frontend_update_state(uint32_t baud, uint32_t bits,
                                        int parity, int stop) -> void {
  super_serial_frontend_update_comm_state(
      baud, bits, static_cast<SuperSerialParity_t>(parity),
      static_cast<SuperSerialStopBits_t>(stop));
}

auto super_serial_frontend_close() -> void {
  if (g_thread_running) {
    g_thread_terminate = true;
    pthread_join(g_comm_thread, nullptr);
    g_thread_running = false;
  }

  if (g_comm_handle != -1) {
    close(g_comm_handle);
  }
  g_comm_handle = -1;
  g_comm_inactivity = 0;
}

auto super_serial_frontend_set_serial_port_path(const char* path) -> void {
  if (g_comm_handle == -1) {
    g_serial_port_path = path ? path : "";
  } else {
    fprintf(stderr, "You cannot change the serial port while it is in use!\n");
  }
}

auto super_serial_frontend_set_loopback(bool enable) -> void {
  g_serial_loopback = enable;
}

auto super_serial_frontend_send_byte(uint8_t byte) -> void {
  super_serial_frontend_transmit_byte(byte);
}
