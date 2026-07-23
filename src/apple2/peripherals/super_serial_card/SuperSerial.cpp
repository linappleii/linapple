// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//             cppcoreguidelines-owning-memory)
#include "apple2/peripherals/super_serial_card/SuperSerial.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/peripherals/super_serial_card/SuperSerialCommands.h"
#include "core/Peripheral.h"

namespace {

struct SuperSerialCard_t {
  SuperSerialDipSwConfig_t config{};

  uint8_t control_byte = 0;
  uint8_t command_byte = 0;

  std::array<uint8_t, SUPER_SERIAL_FIFO_SIZE> rx_buffer{};
  std::atomic<uint32_t> rx_count{0};

  bool is_tx_irq_enabled = false;
  bool is_rx_irq_enabled = false;
  bool was_tx_written = false;
  std::atomic<bool> is_irq_pending{false};

  HostInterface_t* host = nullptr;
  int slot = 0;

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

auto super_serial_initialize(SuperSerialCard_t* ssc) -> void {
  if (ssc == nullptr) {
    return;
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
    return MemReadFloatingBus(remaining_cycles);
  }
  auto* ssc = static_cast<SuperSerialCard_t*>(instance);

  const uint16_t offset = memory_address & 0x0F;
  switch (offset) {
    case 8: {
      if (ssc->rx_count > 0) {
        uint8_t byte = ssc->rx_buffer.at(0);
        std::copy(ssc->rx_buffer.begin() + 1, ssc->rx_buffer.end(),
                  ssc->rx_buffer.begin());
        ssc->rx_count--;
        return byte;
      }
      return 0;
    }
    case 9: {
      uint8_t status = 0x10;
      if (ssc->rx_count > 0) {
        status |= 0x08;
      }
      if (ssc->is_irq_pending) {
        status |= 0x80;
      }
      return status;
    }
    case 10:
      return ssc->command_byte;
    case 11:
      return ssc->control_byte;
    default:
      break;
  }

  return MemReadFloatingBus(remaining_cycles);
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

    case 10:
      ssc->command_byte = data_value;
      ssc->is_rx_irq_enabled = (data_value & 0x01) != 0;
      ssc->is_tx_irq_enabled = ((data_value & 0x0C) == 0x04);
      return 0;

    case 11:
      ssc->control_byte = data_value;
      return 0;

    default:
      break;
  }

  return 0;
}

auto super_serial_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }
  auto ssc = std::unique_ptr<SuperSerialCard_t>(new SuperSerialCard_t());
  ssc->host = host;
  ssc->slot = slot;
  super_serial_initialize(ssc.get());

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
  std::unique_ptr<SuperSerialCard_t> ssc(
      static_cast<SuperSerialCard_t*>(instance));
}

auto super_serial_abi_command(void* instance, uint32_t cmd, const void* data,
                             size_t size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  auto* ssc = static_cast<SuperSerialCard_t*>(instance);

  switch (static_cast<SuperSerialCmd_t>(cmd)) {
    case SUPER_SERIAL_CMD_PUSH_RX_BYTE: {
      if (size < sizeof(uint8_t)) {
        return peripheral_error;
      }
      uint8_t byte = *static_cast<const uint8_t*>(data);
      if (ssc->rx_count < SUPER_SERIAL_FIFO_SIZE) {
        ssc->rx_buffer.at(ssc->rx_count++) = byte;
        if (ssc->is_rx_irq_enabled && ssc->host != nullptr &&
            ssc->host->AssertIrq != nullptr) {
          ssc->is_irq_pending = true;
          ssc->host->AssertIrq(ssc->slot, true);
        }
      }
      return peripheral_ok;
    }
    case SUPER_SERIAL_CMD_SET_CONFIG: {
      if (size < sizeof(SuperSerialDipSwConfig_t)) {
        return peripheral_error;
      }
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
      *static_cast<SuperSerialDipSwConfig_t*>(output_buffer) = ssc->config;
      *buffer_size = req_size;
      return peripheral_ok;
    }
    case SUPER_SERIAL_QUERY_RX_READY: {
      const size_t req_size = sizeof(bool);
      if (output_buffer == nullptr) {
        *buffer_size = req_size;
        return peripheral_ok;
      }
      if (*buffer_size < req_size) {
        return peripheral_error;
      }
      *static_cast<bool*>(output_buffer) = (ssc->rx_count > 0);
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
  const size_t required_size = sizeof(SS_IO_Comms);
  if (state_buffer == nullptr) {
    *buffer_size = required_size;
    return peripheral_ok;
  }
  if (instance == nullptr || *buffer_size < required_size) {
    return peripheral_error;
  }

  auto* super_serial = static_cast<SuperSerialCard_t*>(instance);
  auto* save_state_ptr = static_cast<SS_IO_Comms*>(state_buffer);

  save_state_ptr->control_byte = super_serial->control_byte;
  save_state_ptr->command_byte = super_serial->command_byte;
  save_state_ptr->recv_bytes = super_serial->rx_count;
  std::copy_n(super_serial->rx_buffer.begin(), SUPER_SERIAL_FIFO_SIZE,
              save_state_ptr->recv_buffer);

  *buffer_size = required_size;
  return peripheral_ok;
}

auto super_serial_load_state(void* instance, const void* state_buffer,
                           size_t buffer_size) -> PeripheralStatus_t {
  const size_t required_size = sizeof(SS_IO_Comms);
  if (instance == nullptr || state_buffer == nullptr ||
      buffer_size < required_size) {
    return peripheral_error;
  }
  auto* super_serial = static_cast<SuperSerialCard_t*>(instance);
  const auto* save_state_ptr = static_cast<const SS_IO_Comms*>(state_buffer);

  super_serial->control_byte = save_state_ptr->control_byte;
  super_serial->command_byte = save_state_ptr->command_byte;
  super_serial->rx_count = save_state_ptr->recv_bytes;
  std::copy_n(save_state_ptr->recv_buffer, SUPER_SERIAL_FIFO_SIZE,
              super_serial->rx_buffer.begin());

  return peripheral_ok;
}

static Peripheral_t g_ssc_peripheral = {
    .AbiVersion_t = LINAPPLE_ABI_VERSION,
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

auto super_serial_get_descriptor() -> Peripheral_t* { return &g_ssc_peripheral; }

PERIPHERAL_REGISTER(g_ssc_peripheral)
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//           cppcoreguidelines-owning-memory)
