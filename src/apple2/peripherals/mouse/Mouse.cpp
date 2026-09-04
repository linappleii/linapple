// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-owning-memory, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-type-const-cast, bugprone-easily-swappable-parameters, modernize-make-unique)
// Justification: This module implements low-level
// hardware emulation using procedural C-style patterns for performance and ABI
// compatibility. Pointer arithmetic and C-style arrays are required for ROM
// data manipulation and hardware state representation. swappable-parameters is
// mandated by the project-wide Peripheral ABI signatures.

#include "apple2/peripherals/mouse/Mouse.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

#include "EmbeddedRoms.h"
#include "apple2/Memory.h"
#include "apple2/chips/6821.h"
#include "apple2/peripherals/mouse/MouseCommands.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"

namespace {

namespace physical {
constexpr size_t rom_size = 2048;
constexpr uint32_t rom_page_size = 256;
constexpr uint32_t default_max_coord = 1023;
constexpr int default_slot = 4;
}  // namespace physical

namespace regs {
// Mouse modes
constexpr uint8_t mouse_set = 0x00;
constexpr uint8_t mouse_read = 0x10;
constexpr uint8_t mouse_serv = 0x20;
constexpr uint8_t mouse_clear = 0x30;
constexpr uint8_t mouse_pos = 0x40;
constexpr uint8_t mouse_init = 0x50;
constexpr uint8_t mouse_clamp = 0x60;
constexpr uint8_t mouse_home = 0x70;
constexpr uint8_t mouse_time = 0x90;

// Mouse status bits
constexpr uint8_t stat_prev_btn1 = 0x01;
constexpr uint8_t stat_move_int = 0x02;
constexpr uint8_t stat_btn_int = 0x04;
constexpr uint8_t stat_vbl_int = 0x08;
constexpr uint8_t stat_curr_btn1 = 0x10;
constexpr uint8_t stat_movement = 0x20;
constexpr uint8_t stat_prev_btn0 = 0x40;
constexpr uint8_t stat_curr_btn0 = 0x80;

constexpr uint8_t bit4 = 0x10;
constexpr uint8_t bit5 = 0x20;
constexpr uint8_t bit6 = 0x40;
constexpr uint8_t bit7 = 0x80;

constexpr uint8_t cmd_mask = 0xF0;
constexpr uint8_t mode_mask = 0x0F;
constexpr uint8_t pb_data_mask = 0x3E;

constexpr uint8_t rom_bank_mask = 0x0E;
constexpr uint8_t rom_bank_shift = 1;

constexpr int data_len_6 = 6;
constexpr int data_len_5 = 5;
constexpr uint8_t byte_mask = 0xFF;
constexpr int coord_shift_8 = 8;
constexpr uint8_t status_mode_mask = 0x0C;
constexpr uint8_t status_mode_8 = 0x08;
constexpr uint8_t status_mode_c = 0x0C;
}  // namespace regs

struct MousePeripheral_t {
  // --- Host Context ---
  HostInterface_t* host = nullptr;
  uint32_t slot = 0;
  bool is_active = false;

  // --- Hardware Emulation (PIA & Registers) ---
  Pia6821_t pia{};
  uint8_t pia_port_a = 0;
  uint8_t pia_port_b = 0;
  uint8_t mode = 0;
  bool vblank_rising = false;

  // --- Protocol State ---
  std::array<uint8_t, 8> buffer{};
  int32_t buffer_pos = 0;
  int32_t data_len = 0;
  uint8_t status_state = 0;  // Latched status bits for firmware communication

  // --- Live Coordinate State (Internal) ---
  uint32_t internal_x = 0;
  uint32_t internal_y = 0;
  uint32_t min_x = 0;
  uint32_t max_x = 0;
  uint32_t min_y = 0;
  uint32_t max_y = 0;
  uint32_t range_x = 0;
  uint32_t range_y = 0;

  // --- Last Reported State (Used for movement detection) ---
  int32_t pos_x = 0;  // Coordinate at last MOUSE_READ
  int32_t pos_y = 0;  // Coordinate at last MOUSE_READ
  bool btn0_prev = false;
  bool btn1_prev = false;
  std::array<bool, 2> buttons{false, false};

  // --- ROM Handling ---
  std::array<uint8_t, physical::rom_size> slot_rom{};

  MousePeripheral_t() = default;
};

// --- Forward Declarations ---

static auto mouse_update_slot_rom(MousePeripheral_t* mp) -> void;
static auto mouse_on_mouse_event(MousePeripheral_t* mp) -> void;
static auto mouse_on_command(MousePeripheral_t* mp) -> void;
static auto mouse_on_write(MousePeripheral_t* mp) -> void;
static auto mouse_reset_internal(MousePeripheral_t* mp) -> void;
static auto mouse_set_position_internal(MousePeripheral_t* mp, int x, int y)
    -> void;
static auto mouse_clamp_x(MousePeripheral_t* mp, int min_x, int max_x) -> void;
static auto mouse_clamp_y(MousePeripheral_t* mp, int min_y, int max_y) -> void;

// --- State Persistence ---

struct MouseSaveState_t {
  // --- 32-bit types (4-byte alignment) ---
  uint32_t internal_x;
  uint32_t internal_y;
  uint32_t min_x;
  uint32_t max_x;
  uint32_t min_y;
  uint32_t max_y;
  uint32_t range_x;
  uint32_t range_y;
  int32_t pos_x;
  int32_t pos_y;
  int32_t buffer_pos;
  int32_t data_len;

  // --- 8-bit types (1-byte alignment) ---
  uint8_t pia_ora;
  uint8_t pia_orb;
  uint8_t pia_ddra;
  uint8_t pia_ddrb;
  uint8_t pia_cra;
  uint8_t pia_crb;
  uint8_t pia_port_a_in;
  uint8_t pia_port_b_in;
  uint8_t pia_ca1_in;
  uint8_t pia_ca2_in;
  uint8_t pia_cb1_in;
  uint8_t pia_cb2_in;
  uint8_t pia_oca2;
  uint8_t pia_ocb2;
  uint8_t pia_irqa;
  uint8_t pia_irqb;
  uint8_t pia_port_a_shadow;
  uint8_t pia_port_b_shadow;
  uint8_t mode;
  uint8_t vblank_rising;
  uint8_t status_state;
  uint8_t btn0_prev;
  uint8_t btn1_prev;
  uint8_t buttons[2];
  uint8_t buffer[8];

  // --- Explicit Padding (maintain 4-byte boundary) ---
  uint8_t padding[3];
};

// --- Helper Functions ---

static auto mouse_update_slot_rom(MousePeripheral_t* mp) -> void {
  if (mp == nullptr) {
    return;
  }

  if (mp->host == nullptr) {
    return;
  }

  if (mp->host->RegisterCxROM == nullptr) {
    return;
  }

  // Bits 1-3 of Port B select the 256-byte ROM bank to map into $Cn00
  uint32_t bank_index =
      (static_cast<uint32_t>(mp->pia_port_b) & regs::rom_bank_mask) >>
      regs::rom_bank_shift;
  uint32_t offset = bank_index * physical::rom_page_size;

  // Modernized: Register ROM page from the encapsulated slot_rom
  mp->host->RegisterCxROM(static_cast<int>(mp->slot), &mp->slot_rom.at(offset));
}

static auto pia_listener_a(void* obj, uint8_t data) -> void {
  if (obj == nullptr) {
    return;
  }

  auto* mp = static_cast<MousePeripheral_t*>(obj);
  mp->pia_port_a = data;
}

static auto mouse_on_clock_write(MousePeripheral_t* mp, uint8_t data) -> void {
  if (mp == nullptr) {
    return;
  }

  if ((data & regs::bit5) != 0) {
    // Rising edge: Signal ready to read from MC6821 (Port B bit 7)
    mp->pia_port_b |= regs::bit7;
    return;
  }

  // Falling edge: Clock active. Data from Port A is written into the buffer.
  if (mp->buffer_pos >= 0 &&
      static_cast<size_t>(mp->buffer_pos) < mp->buffer.size()) {
    mp->buffer.at(static_cast<size_t>(mp->buffer_pos++)) = mp->pia_port_a;
  }

  if (mp->buffer_pos == 1) {
    mouse_on_command(mp);
  }

  if (mp->buffer_pos == mp->data_len ||
      static_cast<size_t>(mp->buffer_pos) >= mp->buffer.size()) {
    mouse_on_write(mp);
    mp->buffer_pos = 0;
  }

  // Signal completion by clearing Port B bit 7
  mp->pia_port_b &= ~regs::bit7;
  pia_6821_set_port_b(&mp->pia, mp->pia_port_b);
}

static auto mouse_on_clock_read(MousePeripheral_t* mp, uint8_t data) -> void {
  if (mp == nullptr) {
    return;
  }

  if ((data & regs::bit4) != 0) {
    // Rising edge: Prepare next value, clear acknowledge bit (Port B bit 6)
    mp->pia_port_b &= ~regs::bit6;
    return;
  }

  // Falling edge: Clock active. Step through response buffer.
  if (mp->buffer_pos != 0) {
    mp->buffer_pos++;
  }

  if (mp->buffer_pos == mp->data_len ||
      static_cast<size_t>(mp->buffer_pos) >= mp->buffer.size()) {
    mp->buffer_pos = 0;
  } else {
    uint8_t val = mp->buffer.at(static_cast<size_t>(mp->buffer_pos));
    mp->pia.ora = val;
    pia_6821_set_port_a(&mp->pia, val);
  }

  // Set acknowledge bit (Port B bit 6)
  mp->pia_port_b |= regs::bit6;
}

static auto pia_listener_b(void* obj, uint8_t data) -> void {
  if (obj == nullptr) {
    return;
  }

  auto* mp = static_cast<MousePeripheral_t*>(obj);

  // Only respond to changes in bits 1-5 (Banking and Clocking)
  uint8_t diff = (mp->pia_port_b ^ data) & regs::pb_data_mask;
  if (diff == 0) {
    return;
  }

  // Update internal shadow
  mp->pia_port_b &= ~regs::pb_data_mask;
  mp->pia_port_b |= (data & regs::pb_data_mask);

  if ((diff & regs::bit5) != 0) {
    mouse_on_clock_write(mp, data);
  }

  if ((diff & regs::bit4) != 0) {
    mouse_on_clock_read(mp, data);
  }

  pia_6821_set_port_b(&mp->pia, mp->pia_port_b);
  mouse_update_slot_rom(mp);
}

static auto mouse_on_command(MousePeripheral_t* mp) -> void {
  if (mp == nullptr) {
    return;
  }

  uint8_t cmd = mp->buffer.at(0) & regs::cmd_mask;
  switch (cmd) {
    case regs::mouse_set:
      mp->data_len = 1;
      mp->mode = mp->buffer.at(0) & regs::mode_mask;
      mouse_on_mouse_event(mp);
      break;

    case regs::mouse_read:
      mp->data_len = regs::data_len_6;
      mp->status_state &= regs::stat_movement;
      mp->pos_x = static_cast<int>(mp->internal_x);
      mp->pos_y = static_cast<int>(mp->internal_y);

      if (mp->btn0_prev) {
        mp->status_state |= regs::stat_prev_btn0;
      }
      if (mp->btn1_prev) {
        mp->status_state |= regs::stat_prev_btn1;
      }

      mp->btn0_prev = mp->buttons.at(0);
      mp->btn1_prev = mp->buttons.at(1);

      if (mp->btn0_prev) {
        mp->status_state |= regs::stat_curr_btn0;
      }
      if (mp->btn1_prev) {
        mp->status_state |= regs::stat_curr_btn1;
      }

      mp->buffer.at(1) = static_cast<uint8_t>(mp->pos_x & regs::byte_mask);
      mp->buffer.at(2) = static_cast<uint8_t>(
          (mp->pos_x >> regs::coord_shift_8) & regs::byte_mask);
      mp->buffer.at(3) = static_cast<uint8_t>(mp->pos_y & regs::byte_mask);
      mp->buffer.at(4) = static_cast<uint8_t>(
          (mp->pos_y >> regs::coord_shift_8) & regs::byte_mask);
      mp->buffer.at(5) = mp->status_state;
      mp->status_state &= ~regs::stat_movement;
      break;

    case regs::mouse_serv:
      mp->data_len = 2;
      mp->buffer.at(1) = mp->status_state & ~regs::stat_movement;
      if (mp->host != nullptr) {
        if (mp->host->AssertIrq != nullptr) {
          mp->host->AssertIrq(static_cast<int>(mp->slot), false);
        }
      }
      break;

    case regs::mouse_clear:
      mouse_reset_internal(mp);
      mp->data_len = 1;
      break;

    case regs::mouse_pos:
      mp->data_len = regs::data_len_5;
      break;

    case regs::mouse_init:
      mp->data_len = 3;
      mp->buffer.at(1) = regs::byte_mask;
      break;

    case regs::mouse_clamp:
      mp->data_len = regs::data_len_5;
      break;

    case regs::mouse_home:
      mp->data_len = 1;
      mouse_set_position_internal(mp, 0, 0);
      mouse_on_mouse_event(mp);
      break;

    case regs::mouse_time:
      switch (mp->buffer.at(0) & regs::status_mode_mask) {
        case 0x00:
          mp->data_len = 1;
          break;
        case 0x04:
          mp->data_len = 3;
          break;
        case regs::status_mode_8:
          mp->data_len = 2;
          break;
        case regs::status_mode_c:
          mp->data_len = 4;
          break;
        default:
          mp->data_len = 1;
          break;
      }
      break;

    default:
      mp->data_len = 1;
      break;
  }
  uint8_t val = mp->buffer.at(1);
  mp->pia.ora = val;
  pia_6821_set_port_a(&mp->pia, val);
}

static auto mouse_on_write(MousePeripheral_t* mp) -> void {
  if (mp == nullptr) {
    return;
  }

  int val_min = 0;
  int val_max = 0;
  switch (mp->buffer.at(0) & regs::cmd_mask) {
    case regs::mouse_clamp:
      val_min = (mp->buffer.at(2) << regs::coord_shift_8) | mp->buffer.at(1);
      val_max = (mp->buffer.at(4) << regs::coord_shift_8) | mp->buffer.at(3);
      if ((mp->buffer.at(0) & 1) != 0) {
        mouse_clamp_y(mp, val_min, val_max);
      } else {
        mouse_clamp_x(mp, val_min, val_max);
      }
      break;

    case regs::mouse_pos:
      mp->pos_x = (mp->buffer.at(2) << regs::coord_shift_8) | mp->buffer.at(1);
      mp->pos_y = (mp->buffer.at(4) << regs::coord_shift_8) | mp->buffer.at(3);
      mouse_set_position_internal(mp, mp->pos_x, mp->pos_y);
      mouse_on_mouse_event(mp);
      break;

    case regs::mouse_init:
      mp->pos_x = 0;
      mp->pos_y = 0;
      mouse_clamp_x(mp, 0, static_cast<int>(physical::default_max_coord));
      mouse_clamp_y(mp, 0, static_cast<int>(physical::default_max_coord));
      mouse_set_position_internal(mp, 0, 0);
      mouse_on_mouse_event(mp);
      break;

    default:
      break;
  }
}

static auto mouse_on_mouse_event(MousePeripheral_t* mp) -> void {
  if (mp == nullptr) {
    return;
  }

  uint8_t state = 0;
  if (static_cast<uint32_t>(mp->pos_x) != mp->internal_x ||
      static_cast<uint32_t>(mp->pos_y) != mp->internal_y) {
    state |= regs::stat_movement;
    if ((mp->mode & 1) != 0 && (mp->mode & 0x02) != 0) {
      state |= regs::stat_move_int;
    }
  }

  if ((mp->mode & 1) != 0) {
    if (mp->btn0_prev != mp->buttons.at(0) ||
        mp->btn1_prev != mp->buttons.at(1)) {
      if ((mp->mode & 0x04) != 0) {
        state |= regs::stat_btn_int;
      }
    }

    if (mp->vblank_rising) {
      if ((mp->mode & 0x08) != 0) {
        state |= regs::stat_vbl_int;
      }
    }
  }

  if (state != 0) {
    mp->status_state |= state;
    if ((state & 0x0E) != 0) {
      if (mp->host != nullptr) {
        if (mp->host->AssertIrq != nullptr) {
          mp->host->AssertIrq(static_cast<int>(mp->slot), true);
        }
      }
    }
  }
}

static auto mouse_reset_internal(MousePeripheral_t* mp) -> void {
  if (mp == nullptr) {
    return;
  }

  mp->buffer_pos = 0;
  mp->data_len = 1;
  mp->mode = 0;
  mp->status_state = 0;
  mp->pos_x = 0;
  mp->pos_y = 0;
  mp->btn0_prev = false;
  mp->btn1_prev = false;
  mouse_clamp_x(mp, 0, static_cast<int>(physical::default_max_coord));
  mouse_clamp_y(mp, 0, static_cast<int>(physical::default_max_coord));
  mouse_set_position_internal(mp, 0, 0);
}

static auto mouse_clamp_x(MousePeripheral_t* mp, int min_val, int max_val)
    -> void {
  if (mp == nullptr) {
    return;
  }

  if (min_val < 0 || min_val > max_val) {
    return;
  }

  mp->max_x = static_cast<uint32_t>(max_val);
  mp->min_x = static_cast<uint32_t>(min_val);
  mp->internal_x = std::min(std::max(mp->internal_x, mp->min_x), mp->max_x);
}

static auto mouse_clamp_y(MousePeripheral_t* mp, int min_val, int max_val)
    -> void {
  if (mp == nullptr) {
    return;
  }

  if (min_val < 0 || min_val > max_val) {
    return;
  }

  mp->max_y = static_cast<uint32_t>(max_val);
  mp->min_y = static_cast<uint32_t>(min_val);
  mp->internal_y = std::min(std::max(mp->internal_y, mp->min_y), mp->max_y);
}

static auto mouse_set_position_internal(MousePeripheral_t* mp, int x, int y)
    -> void {
  if (mp == nullptr) {
    return;
  }

  if (mp->range_x == 0 || mp->range_y == 0) {
    return;
  }

  // Scale host coordinates to internal 0..1023 range
  uint32_t scaled_x =
      (static_cast<uint32_t>(x) * physical::default_max_coord) / mp->range_x;
  uint32_t scaled_y =
      (static_cast<uint32_t>(y) * physical::default_max_coord) / mp->range_y;

  mp->internal_x = std::min(std::max(scaled_x, mp->min_x), mp->max_x);
  mp->internal_y = std::min(std::max(scaled_y, mp->min_y), mp->max_y);
}

// --- ABI Implementation ---

static auto peripheral_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }

  if (host->RegisterIO == nullptr) {
    return nullptr;
  }

  auto mp = std::unique_ptr<MousePeripheral_t>(new MousePeripheral_t{});
  mp->host = host;
  mp->slot = static_cast<uint32_t>(slot);

  pia_6821_reset(&mp->pia);
  pia_6821_set_listener_a(&mp->pia, mp.get(), pia_listener_a);
  pia_6821_set_listener_b(&mp->pia, mp.get(), pia_listener_b);

  mp->pia_port_a = 0;
  mp->pia_port_b = regs::bit6;
  pia_6821_set_port_b(&mp->pia, mp->pia_port_b);

  mp->min_x = 0;
  mp->max_x = physical::default_max_coord;
  mp->min_y = 0;
  mp->max_y = physical::default_max_coord;

  mouse_reset_internal(mp.get());

#if ENABLE_ROM_MOUSE
  std::copy(g_rom_mouse_interface,
            g_rom_mouse_interface + g_rom_mouse_interface_size,
            mp->slot_rom.begin());
#endif

  mouse_update_slot_rom(mp.get());

  auto io_handler = [](void* instance, uint16_t pc, uint16_t addr,
                       uint8_t write, uint8_t val, uint32_t cycles) -> uint8_t {
    (void)pc;
    if (instance == nullptr) {
      return mem_read_floating_bus(cycles);
    }

    auto* mp_inner = static_cast<MousePeripheral_t*>(instance);
    uint8_t rs = static_cast<uint8_t>(addr & 3);
    if (write != 0) {
      pia_6821_write(&mp_inner->pia, rs, val);
      return 0;
    }
    return pia_6821_read(&mp_inner->pia, rs);
  };

  host->RegisterIO(slot, io_handler, io_handler, nullptr, nullptr);
  mp->is_active = true;

  return mp.release();
}

static auto peripheral_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }

  auto* mp = static_cast<MousePeripheral_t*>(instance);
  mouse_reset_internal(mp);
}

static auto peripheral_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }

  auto* mp = static_cast<MousePeripheral_t*>(instance);
  delete mp;
}

static auto peripheral_abi_on_vblank(void* instance, bool vblank) -> void {
  if (instance == nullptr) {
    return;
  }

  auto* mp = static_cast<MousePeripheral_t*>(instance);
  if (mp->vblank_rising != vblank) {
    mp->vblank_rising = vblank;
    if (mp->vblank_rising) {
      mouse_on_mouse_event(mp);
    }
  }
}

static auto peripheral_abi_save_state(void* instance, void* buffer,
                                      size_t* size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }

  if (size == nullptr) {
    return peripheral_error;
  }

  constexpr size_t required = sizeof(MouseSaveState_t);

  if (buffer == nullptr) {
    *size = required;
    return peripheral_ok;
  }

  if (*size < required) {
    *size = required;
    return peripheral_error;
  }

  auto* mp = static_cast<MousePeripheral_t*>(instance);
  auto* ss = static_cast<MouseSaveState_t*>(buffer);

  std::memset(ss, 0, required);
  // --- Hardware Emulation (PIA & Registers) ---
  ss->pia_ora = mp->pia.ora;
  ss->pia_orb = mp->pia.orb;
  ss->pia_ddra = mp->pia.ddra;
  ss->pia_ddrb = mp->pia.ddrb;
  ss->pia_cra = mp->pia.cra;
  ss->pia_crb = mp->pia.crb;
  ss->pia_port_a_in = mp->pia.port_a_in;
  ss->pia_port_b_in = mp->pia.port_b_in;
  ss->pia_ca1_in = mp->pia.ca1_in ? 1 : 0;
  ss->pia_ca2_in = mp->pia.ca2_in ? 1 : 0;
  ss->pia_cb1_in = mp->pia.cb1_in ? 1 : 0;
  ss->pia_cb2_in = mp->pia.cb2_in ? 1 : 0;
  ss->pia_oca2 = mp->pia.oca2;
  ss->pia_ocb2 = mp->pia.ocb2;
  ss->pia_irqa = mp->pia.irq_a_state;
  ss->pia_irqb = mp->pia.irq_b_state;
  ss->pia_port_a_shadow = mp->pia_port_a;
  ss->pia_port_b_shadow = mp->pia_port_b;
  ss->mode = mp->mode;
  ss->vblank_rising = mp->vblank_rising ? 1 : 0;

  // --- Protocol State ---
  std::copy(mp->buffer.begin(), mp->buffer.end(), ss->buffer);
  ss->buffer_pos = mp->buffer_pos;
  ss->data_len = mp->data_len;
  ss->status_state = mp->status_state;

  // --- Live Coordinate State (Internal) ---
  ss->internal_x = mp->internal_x;
  ss->internal_y = mp->internal_y;
  ss->min_x = mp->min_x;
  ss->max_x = mp->max_x;
  ss->min_y = mp->min_y;
  ss->max_y = mp->max_y;
  ss->range_x = mp->range_x;
  ss->range_y = mp->range_y;

  // --- Last Reported State ---
  ss->pos_x = mp->pos_x;
  ss->pos_y = mp->pos_y;
  ss->btn0_prev = mp->btn0_prev ? 1 : 0;
  ss->btn1_prev = mp->btn1_prev ? 1 : 0;
  ss->buttons[0] = mp->buttons.at(0) ? 1 : 0;
  ss->buttons[1] = mp->buttons.at(1) ? 1 : 0;

  *size = required;
  return peripheral_ok;
}

static auto peripheral_abi_load_state(void* instance, const void* buffer,
                                      size_t size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }

  if (buffer == nullptr) {
    return peripheral_error;
  }

  if (size < sizeof(MouseSaveState_t)) {
    return peripheral_error;
  }

  auto* mp = static_cast<MousePeripheral_t*>(instance);
  const auto* ss = static_cast<const MouseSaveState_t*>(buffer);

  pia_6821_reset(&mp->pia);
  // --- Hardware Emulation (PIA & Registers) ---
  mp->pia.ora = ss->pia_ora;
  mp->pia.orb = ss->pia_orb;
  mp->pia.ddra = ss->pia_ddra;
  mp->pia.ddrb = ss->pia_ddrb;
  mp->pia.cra = ss->pia_cra;
  mp->pia.crb = ss->pia_crb;
  mp->pia.port_a_in = ss->pia_port_a_in;
  mp->pia.port_b_in = ss->pia_port_b_in;
  mp->pia.ca1_in = ss->pia_ca1_in != 0;
  mp->pia.ca2_in = ss->pia_ca2_in != 0;
  mp->pia.cb1_in = ss->pia_cb1_in != 0;
  mp->pia.cb2_in = ss->pia_cb2_in != 0;
  mp->pia.oca2 = ss->pia_oca2;
  mp->pia.ocb2 = ss->pia_ocb2;
  mp->pia.irq_a_state = ss->pia_irqa;
  mp->pia.irq_b_state = ss->pia_irqb;
  mp->pia_port_a = ss->pia_port_a_shadow;
  mp->pia_port_b = ss->pia_port_b_shadow;
  mp->mode = ss->mode;
  mp->vblank_rising = ss->vblank_rising != 0;

  // --- Protocol State ---
  std::copy(ss->buffer, ss->buffer + 8, mp->buffer.begin());
  mp->buffer_pos = (ss->buffer_pos >= 0 &&
                    static_cast<size_t>(ss->buffer_pos) < mp->buffer.size())
                       ? ss->buffer_pos
                       : 0;
  mp->data_len = (ss->data_len >= 0 &&
                  static_cast<size_t>(ss->data_len) <= mp->buffer.size())
                     ? ss->data_len
                     : static_cast<int32_t>(mp->buffer.size());
  mp->status_state = ss->status_state;

  // --- Live Coordinate State (Internal) ---
  mp->internal_x = ss->internal_x;
  mp->internal_y = ss->internal_y;
  mp->min_x = ss->min_x;
  mp->max_x = ss->max_x;
  mp->min_y = ss->min_y;
  mp->max_y = ss->max_y;
  mp->range_x = ss->range_x;
  mp->range_y = ss->range_y;

  // --- Last Reported State ---
  mp->pos_x = ss->pos_x;
  mp->pos_y = ss->pos_y;
  mp->btn0_prev = ss->btn0_prev != 0;
  mp->btn1_prev = ss->btn1_prev != 0;
  mp->buttons.at(0) = ss->buttons[0] != 0;
  mp->buttons.at(1) = ss->buttons[1] != 0;

  pia_6821_set_listener_a(&mp->pia, mp, pia_listener_a);
  pia_6821_set_listener_b(&mp->pia, mp, pia_listener_b);

  mouse_update_slot_rom(mp);

  return peripheral_ok;
}

static auto peripheral_abi_command(void* instance, uint32_t cmd_id,
                                   const void* data, size_t size)
    -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }

  if (data == nullptr) {
    return peripheral_error;
  }

  auto* mp = static_cast<MousePeripheral_t*>(instance);

  switch (static_cast<MouseCmd_e>(cmd_id)) {
    case mouse_cmd_set_pos: {
      if (size < sizeof(MousePosPayload_t)) {
        return peripheral_error;
      }
      const auto* p = static_cast<const MousePosPayload_t*>(data);
      mp->range_x = static_cast<uint32_t>(p->x_range);
      mp->range_y = static_cast<uint32_t>(p->y_range);
      mouse_set_position_internal(mp, p->x, p->y);
      mouse_on_mouse_event(mp);
      return peripheral_ok;
    }
    case mouse_cmd_set_button: {
      if (size < sizeof(MouseButtonPayload_t)) {
        return peripheral_error;
      }
      const auto* p = static_cast<const MouseButtonPayload_t*>(data);
      if (p->button < 2) {
        mp->buttons.at(p->button) = p->down;
        mouse_on_mouse_event(mp);
      }
      return peripheral_ok;
    }
    default:
      return peripheral_error;
  }
  return peripheral_error;
}

static auto peripheral_abi_query(void* instance, uint32_t query_id, void* out,
                                 size_t* out_size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }

  if (out == nullptr) {
    return peripheral_error;
  }

  if (out_size == nullptr) {
    return peripheral_error;
  }

  auto* mp = static_cast<MousePeripheral_t*>(instance);

  switch (static_cast<MouseQuery_e>(query_id)) {
    case mouse_query_is_active: {
      if (*out_size < 1) {
        return peripheral_error;
      }
      *static_cast<uint8_t*>(out) = mp->is_active ? 1 : 0;
      *out_size = 1;
      return peripheral_ok;
    }
    default:
      return peripheral_error;
  }
  return peripheral_error;
}

}  // namespace

static Peripheral_t g_mouse_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.mouse",
    .name = "Mouse Interface",
    .description = "Apple II Mouse Card emulation",
    .author = "LinApple Contributors",
    .version = "3.1.0",
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = physical::default_slot,
    .init = peripheral_abi_init,
    .reset = peripheral_abi_reset,
    .shutdown = peripheral_abi_shutdown,
    .think = nullptr,
    .on_vblank = peripheral_abi_on_vblank,
    .save_state = peripheral_abi_save_state,
    .load_state = peripheral_abi_load_state,
    .command = peripheral_abi_command,
    .query = peripheral_abi_query};

extern "C" auto mouse_get_descriptor() -> Peripheral_t* {
  return &g_mouse_peripheral;
}

PERIPHERAL_REGISTER(g_mouse_peripheral)

// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-owning-memory, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-type-reinterpret-cast, cppcoreguidelines-pro-type-const-cast, bugprone-easily-swappable-parameters, modernize-make-unique)
