// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/super_serial_card/SuperSerial.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>

#include "EmbeddedRoms.h"
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"

#ifndef VERSIONSTRING
#define VERSIONSTRING "2.0.0"
#endif

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;

namespace {

static_assert(sizeof(SuperSerialSaveState_t) == 56,
              "SuperSerialSaveState_t must be exactly 56 bytes");

constexpr std::array<uint32_t, 16> k_baud_table = {{
    0,     // 0: 16x External clock
    50,    // 1: 50 baud
    75,    // 2: 75 baud
    110,   // 3: 109.92 (110) baud
    135,   // 4: 134.58 (135) baud
    150,   // 5: 150 baud
    300,   // 6: 300 baud
    600,   // 7: 600 baud
    1200,  // 8: 1200 baud
    1800,  // 9: 1800 baud
    2400,  // 10: 2400 baud
    3600,  // 11: 3600 baud
    4800,  // 12: 4800 baud
    7200,  // 13: 7200 baud
    9600,  // 14: 9600 baud
    19200  // 15: 19200 baud
}};

struct SuperSerialCard_t {
  SuperSerialDipSwConfig_t config{};

  uint8_t control_byte = 0;
  uint8_t command_byte = 0;

  std::array<uint8_t, SUPER_SERIAL_FIFO_SIZE> rx_buffer{};
  uint32_t rx_count = 0;

  bool is_tx_irq_enabled = false;
  bool is_rx_irq_enabled = false;
  bool was_tx_written = false;
  bool is_irq_pending = false;

  HostInterface_t* host = nullptr;
  int slot = 0;
  mutable std::mutex fifo_mutex;

  SuperSerialCard_t() = default;

  void reset_hardware_state() {
    control_byte = 0;
    command_byte = 0;
    rx_count = 0;
    is_tx_irq_enabled = false;
    is_rx_irq_enabled = false;
    was_tx_written = false;
    is_irq_pending = false;
    rx_buffer.fill(0);
  }
};

auto super_serial_notify_host_state(SuperSerialCard_t* ssc) -> void {
  if (ssc == nullptr || ssc->host == nullptr ||
      ssc->host->SerialUpdateState == nullptr) {
    return;
  }
  const uint8_t baud_index = ssc->control_byte & 0x0F;
  const uint32_t baud = k_baud_table.at(baud_index);
  const uint32_t bits = 8 - ((ssc->control_byte >> 5) & 0x03);

  int stop = SUPER_SERIAL_STOP_BITS_1;
  if ((ssc->control_byte & 0x80) != 0) {
    if (bits == 5 && (ssc->command_byte & 0x20) == 0) {
      stop = SUPER_SERIAL_STOP_BITS_1_5;
    } else {
      stop = SUPER_SERIAL_STOP_BITS_2;
    }
  }

  int parity = SUPER_SERIAL_PARITY_NONE;
  if ((ssc->command_byte & 0x20) != 0) {
    switch ((ssc->command_byte >> 6) & 0x03) {
      case 0:
        parity = SUPER_SERIAL_PARITY_ODD;
        break;
      case 1:
        parity = SUPER_SERIAL_PARITY_EVEN;
        break;
      case 2:
        parity = SUPER_SERIAL_PARITY_MARK;
        break;
      case 3:
      default:
        parity = SUPER_SERIAL_PARITY_SPACE;
        break;
    }
  }

  ssc->host->SerialUpdateState(ssc, baud, bits, parity, stop);
}

auto super_serial_initialize(SuperSerialCard_t* ssc) -> void {
  if (ssc == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(ssc->fifo_mutex);
  if (ssc->is_irq_pending) {
    ssc->is_irq_pending = false;
    if (ssc->host != nullptr && ssc->host->AssertIrq != nullptr) {
      ssc->host->AssertIrq(ssc->slot, false);
    }
  }
  ssc->reset_hardware_state();
}

auto super_serial_io_read(void* instance, uint16_t program_counter,
                          uint16_t memory_address, uint8_t is_write,
                          uint8_t data_value, uint32_t remaining_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)data_value;

  if (instance == nullptr || is_write != 0) {
    return mem_read_floating_bus(remaining_cycles);
  }
  auto* ssc = static_cast<SuperSerialCard_t*>(instance);

  const uint16_t offset = memory_address & 0x0F;
  switch (offset) {
    case 8: {
      std::lock_guard<std::mutex> lock(ssc->fifo_mutex);
      uint8_t byte = 0;
      if (ssc->rx_count > 0) {
        byte = ssc->rx_buffer.at(0);
        std::copy(ssc->rx_buffer.begin() + 1, ssc->rx_buffer.end(),
                  ssc->rx_buffer.begin());
        ssc->rx_count--;
      }
      if (ssc->is_irq_pending) {
        ssc->is_irq_pending = false;
        if (ssc->host != nullptr && ssc->host->AssertIrq != nullptr) {
          ssc->host->AssertIrq(ssc->slot, false);
        }
      }
      return byte;
    }
    case 9: {
      std::lock_guard<std::mutex> lock(ssc->fifo_mutex);
      uint8_t status = 0x10;
      if (ssc->rx_count > 0) {
        status |= 0x08;
      }
      if (ssc->is_irq_pending) {
        status |= 0x80;
      }
      return status;
    }
    case 10: {
      std::lock_guard<std::mutex> lock(ssc->fifo_mutex);
      return ssc->command_byte;
    }
    case 11: {
      std::lock_guard<std::mutex> lock(ssc->fifo_mutex);
      return ssc->control_byte;
    }
    default:
      break;
  }

  return mem_read_floating_bus(remaining_cycles);
}

auto super_serial_io_write(void* instance, uint16_t program_counter,
                           uint16_t memory_address, uint8_t is_write,
                           uint8_t data_value, uint32_t remaining_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)remaining_cycles;

  if (instance == nullptr || is_write == 0) {
    return 0;
  }
  auto* ssc = static_cast<SuperSerialCard_t*>(instance);

  const uint16_t offset = memory_address & 0x0F;
  switch (offset) {
    case 8:
      if (ssc->host != nullptr && ssc->host->SerialTransmitByte != nullptr) {
        ssc->host->SerialTransmitByte(ssc, data_value);
      }
      ssc->was_tx_written = true;
      return 0;

    case 9:
      super_serial_initialize(ssc);
      return 0;

    case 10: {
      std::lock_guard<std::mutex> lock(ssc->fifo_mutex);
      ssc->command_byte = data_value;
      ssc->is_rx_irq_enabled = ((data_value & 0x02) == 0);
      ssc->is_tx_irq_enabled = ((data_value & 0x0C) == 0x04);
      if (!ssc->is_rx_irq_enabled && ssc->is_irq_pending) {
        ssc->is_irq_pending = false;
        if (ssc->host != nullptr && ssc->host->AssertIrq != nullptr) {
          ssc->host->AssertIrq(ssc->slot, false);
        }
      }
      super_serial_notify_host_state(ssc);
      return 0;
    }

    case 11: {
      std::lock_guard<std::mutex> lock(ssc->fifo_mutex);
      ssc->control_byte = data_value;
      super_serial_notify_host_state(ssc);
      return 0;
    }

    default:
      break;
  }

  return 0;
}

auto super_serial_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr || host->RegisterIO == nullptr) {
    return nullptr;
  }
  auto ssc = std::unique_ptr<SuperSerialCard_t>(new SuperSerialCard_t());
  ssc->host = host;
  ssc->slot = slot;
  super_serial_initialize(ssc.get());

#if ENABLE_ROM_SSC
  if (host->RegisterCxROM != nullptr) {
    host->RegisterCxROM(slot, const_cast<uint8_t*>(g_rom_ssc));
  }
#endif
  host->RegisterIO(slot, super_serial_io_read, super_serial_io_write, nullptr,
                   nullptr);

  return ssc.release();
}

auto super_serial_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  super_serial_initialize(static_cast<SuperSerialCard_t*>(instance));
}

auto super_serial_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* ssc = static_cast<SuperSerialCard_t*>(instance);
  if (ssc->is_irq_pending) {
    ssc->is_irq_pending = false;
    if (ssc->host != nullptr && ssc->host->AssertIrq != nullptr) {
      ssc->host->AssertIrq(ssc->slot, false);
    }
  }
  delete ssc;
}

auto super_serial_abi_command(void* instance, uint32_t cmd, const void* data,
                              size_t size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  auto* ssc = static_cast<SuperSerialCard_t*>(instance);

  switch (static_cast<SuperSerialCmd_t>(cmd)) {
    case SUPER_SERIAL_CMD_PUSH_RX_BYTE: {
      if (data == nullptr || size < sizeof(uint8_t)) {
        return peripheral_error;
      }
      std::lock_guard<std::mutex> lock(ssc->fifo_mutex);
      const uint8_t byte = *static_cast<const uint8_t*>(data);
      if (ssc->rx_count < SUPER_SERIAL_FIFO_SIZE) {
        ssc->rx_buffer.at(ssc->rx_count++) = byte;
        if (ssc->is_rx_irq_enabled) {
          ssc->is_irq_pending = true;
          if (ssc->host != nullptr && ssc->host->AssertIrq != nullptr) {
            ssc->host->AssertIrq(ssc->slot, true);
          }
        }
      }
      return peripheral_ok;
    }
    case SUPER_SERIAL_CMD_SET_CONFIG: {
      if (data == nullptr || size < sizeof(SuperSerialDipSwConfig_t)) {
        return peripheral_error;
      }
      std::lock_guard<std::mutex> lock(ssc->fifo_mutex);
      ssc->config = *static_cast<const SuperSerialDipSwConfig_t*>(data);
      return peripheral_ok;
    }
    default:
      break;
  }
  return peripheral_incompatible;
}

auto super_serial_abi_query(void* instance, uint32_t cmd, void* output_buffer,
                            size_t* buffer_size) -> PeripheralStatus_t {
  if (instance == nullptr || buffer_size == nullptr) {
    return peripheral_error;
  }
  auto* ssc = static_cast<SuperSerialCard_t*>(instance);

  switch (static_cast<SuperSerialQuery_t>(cmd)) {
    case SUPER_SERIAL_QUERY_CONFIG: {
      const size_t req_size = sizeof(SuperSerialDipSwConfig_t);
      if (output_buffer == nullptr) {
        *buffer_size = req_size;
        return peripheral_ok;
      }
      if (*buffer_size < req_size) {
        return peripheral_error;
      }
      std::lock_guard<std::mutex> lock(ssc->fifo_mutex);
      *static_cast<SuperSerialDipSwConfig_t*>(output_buffer) = ssc->config;
      *buffer_size = req_size;
      return peripheral_ok;
    }
    case SUPER_SERIAL_QUERY_RX_READY: {
      const size_t req_size = sizeof(uint8_t);
      if (output_buffer == nullptr) {
        *buffer_size = req_size;
        return peripheral_ok;
      }
      if (*buffer_size < req_size) {
        return peripheral_error;
      }
      std::lock_guard<std::mutex> lock(ssc->fifo_mutex);
      *static_cast<uint8_t*>(output_buffer) = (ssc->rx_count > 0) ? 1 : 0;
      *buffer_size = req_size;
      return peripheral_ok;
    }
    default:
      break;
  }
  return peripheral_incompatible;
}

auto super_serial_save_state(void* instance, void* state_buffer,
                             size_t* buffer_size) -> PeripheralStatus_t {
  if (buffer_size == nullptr) {
    return peripheral_error;
  }
  const size_t required_size = sizeof(SuperSerialSaveState_t);
  if (state_buffer == nullptr) {
    *buffer_size = required_size;
    return peripheral_ok;
  }
  if (instance == nullptr || *buffer_size < required_size) {
    return peripheral_error;
  }

  auto* ssc = static_cast<SuperSerialCard_t*>(instance);
  std::lock_guard<std::mutex> lock(ssc->fifo_mutex);
  auto* save_state = static_cast<SuperSerialSaveState_t*>(state_buffer);
  std::memset(save_state, 0, sizeof(SuperSerialSaveState_t));

  save_state->version = SUPER_SERIAL_STATE_VERSION;
  save_state->struct_size =
      static_cast<uint32_t>(sizeof(SuperSerialSaveState_t));
  save_state->rx_count = ssc->rx_count;
  save_state->control_byte = ssc->control_byte;
  save_state->command_byte = ssc->command_byte;
  save_state->is_irq_pending = ssc->is_irq_pending ? 1 : 0;
  save_state->is_rx_irq_enabled = ssc->is_rx_irq_enabled ? 1 : 0;
  save_state->is_tx_irq_enabled = ssc->is_tx_irq_enabled ? 1 : 0;
  save_state->was_tx_written = ssc->was_tx_written ? 1 : 0;
  std::copy_n(ssc->rx_buffer.begin(), SUPER_SERIAL_FIFO_SIZE,
              save_state->rx_buffer);
  save_state->config = ssc->config;

  *buffer_size = required_size;
  return peripheral_ok;
}

auto super_serial_load_state(void* instance, const void* state_buffer,
                             size_t buffer_size) -> PeripheralStatus_t {
  const size_t required_size = sizeof(SuperSerialSaveState_t);
  if (instance == nullptr || state_buffer == nullptr ||
      buffer_size != required_size) {
    return peripheral_error;
  }
  const auto* save_state =
      static_cast<const SuperSerialSaveState_t*>(state_buffer);
  if (save_state->version != SUPER_SERIAL_STATE_VERSION ||
      save_state->struct_size != required_size) {
    return peripheral_error;
  }

  auto* ssc = static_cast<SuperSerialCard_t*>(instance);
  std::lock_guard<std::mutex> lock(ssc->fifo_mutex);

  ssc->control_byte = save_state->control_byte;
  ssc->command_byte = save_state->command_byte;
  ssc->rx_count = std::min(save_state->rx_count,
                           static_cast<uint32_t>(SUPER_SERIAL_FIFO_SIZE));
  ssc->is_irq_pending = (save_state->is_irq_pending != 0);
  ssc->is_rx_irq_enabled = (save_state->is_rx_irq_enabled != 0);
  ssc->is_tx_irq_enabled = (save_state->is_tx_irq_enabled != 0);
  ssc->was_tx_written = (save_state->was_tx_written != 0);
  std::copy_n(save_state->rx_buffer, SUPER_SERIAL_FIFO_SIZE,
              ssc->rx_buffer.begin());
  ssc->config = save_state->config;

  if (ssc->host != nullptr && ssc->host->AssertIrq != nullptr) {
    ssc->host->AssertIrq(ssc->slot, ssc->is_irq_pending);
  }
  super_serial_notify_host_state(ssc);

  return peripheral_ok;
}

static Peripheral_t g_ssc_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.ssc",
    .name = "Super Serial Card",
    .description = "Apple II Super Serial Card (SSC) emulation",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = 2,
    .init = super_serial_abi_init,
    .reset = super_serial_reset,
    .shutdown = super_serial_shutdown,
    .think = nullptr,
    .on_vblank = nullptr,
    .save_state = super_serial_save_state,
    .load_state = super_serial_load_state,
    .command = super_serial_abi_command,
    .query = super_serial_abi_query};

}  // namespace

auto super_serial_get_descriptor() -> Peripheral_t* {
  return &g_ssc_peripheral;
}

PERIPHERAL_REGISTER(g_ssc_peripheral)
