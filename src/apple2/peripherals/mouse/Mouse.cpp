// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/mouse/Mouse.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "EmbeddedRoms.h"
#include "apple2/chips/6821.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/mouse/MouseCommands.h"

namespace {

constexpr int min_slot = 1;
constexpr int max_slot = 7;
constexpr size_t rom_size = 2048;
constexpr size_t rom_page_size = 256;
// Power-on clamps (AppleMouse II User's Manual p. 48).
constexpr uint32_t default_clamp_max = 1023;
constexpr uint32_t io_register_mask = 0x03;

// The byte the 6502 firmware writes to the 6805 first; the high nibble names
// the command (manual pp. 47-49).
namespace command {
constexpr uint8_t set_mouse = 0x00;
constexpr uint8_t read_mouse = 0x10;
constexpr uint8_t serve_mouse = 0x20;
constexpr uint8_t clear_mouse = 0x30;
constexpr uint8_t pos_mouse = 0x40;
constexpr uint8_t init_mouse = 0x50;
constexpr uint8_t clamp_mouse = 0x60;
constexpr uint8_t home_mouse = 0x70;
constexpr uint8_t time_data = 0x90;
constexpr uint8_t group_mask = 0xF0;
constexpr uint8_t axis_bit = 0x01;
constexpr uint8_t time_data_length_mask = 0x0C;
constexpr uint8_t time_data_two_bytes = 0x08;
constexpr uint8_t time_data_three_bytes = 0x04;
constexpr uint8_t time_data_four_bytes = 0x0C;
}  // namespace command

// Mode byte bits (manual p. 44).
namespace mode {
constexpr uint8_t tracking = 0x01;
constexpr uint8_t movement_interrupts = 0x02;
constexpr uint8_t button_interrupts = 0x04;
constexpr uint8_t refresh_interrupts = 0x08;
constexpr uint8_t mask = 0x0F;
}  // namespace mode

// Status byte bits (manual p. 45).
namespace status {
constexpr uint8_t movement_interrupt = 0x02;
constexpr uint8_t button_interrupt = 0x04;
constexpr uint8_t refresh_interrupt = 0x08;
constexpr uint8_t moved = 0x20;
constexpr uint8_t button_at_last_read = 0x40;
constexpr uint8_t button_down = 0x80;
constexpr uint8_t interrupt_sources = 0x0E;
}  // namespace status

// PIA port B: PB1-PB3 drive the ROM's A8-A10, PB4 and PB5 are the read and
// write strobes to the 6805, PB6 and PB7 its "byte ready" and "busy" replies
// (schematic 050-0101-A zones B3, C2).
namespace port_b {
constexpr uint8_t bank_mask = 0x0E;
constexpr uint8_t bank_shift = 1;
constexpr uint8_t read_strobe = 0x10;
constexpr uint8_t write_strobe = 0x20;
constexpr uint8_t byte_ready = 0x40;
constexpr uint8_t busy = 0x80;
constexpr uint8_t card_driven = 0x3E;
}  // namespace port_b

constexpr int read_mouse_bytes = 6;
constexpr int five_byte_command = 5;
constexpr int byte_shift = 8;
constexpr uint8_t byte_mask = 0xFF;

static_assert(sizeof(MouseSaveState_t) == 92,
              "the mouse card's state frame is part of the plugin ABI");
static_assert(offsetof(MouseSaveState_t, version) == 0,
              "the frame header is version then size");
static_assert(offsetof(MouseSaveState_t, struct_size) == 4,
              "the frame header is version then size");
static_assert(offsetof(MouseSaveState_t, position_x) == 8,
              "the position sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, position_y) == 12,
              "the position sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, min_x) == 16,
              "the clamps sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, max_x) == 20,
              "the clamps sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, min_y) == 24,
              "the clamps sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, max_y) == 28,
              "the clamps sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, tick_phase) == 32,
              "the phase takes the word older frames held the host width in");
static_assert(offsetof(MouseSaveState_t, reserved0) == 36,
              "the host height's word is reserved");
static_assert(offsetof(MouseSaveState_t, read_x) == 40,
              "the last reading sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, read_y) == 44,
              "the last reading sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, parser_pos) == 48,
              "the parser sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, parser_out_len) == 52,
              "the parser sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, pia_ora) == 56,
              "the PIA's six registers sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, pia_port_a_in) == 62,
              "the PIA's inputs sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, pia_port_b_in) == 63,
              "the PIA's inputs sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, rate_50hz) == 64,
              "the rate takes a byte older frames held as zero");
static_assert(offsetof(MouseSaveState_t, pending) == 65,
              "the pending sources take a byte older frames held as zero");
static_assert(offsetof(MouseSaveState_t, irq_asserted) == 66,
              "the IRQ level takes a byte older frames held as zero");
static_assert(offsetof(MouseSaveState_t, reserved1) == 67,
              "one of the PIA's unconnected pins stays reserved");
static_assert(offsetof(MouseSaveState_t, parser_in_len) == 68,
              "the reply length takes a byte older frames held as zero");
static_assert(offsetof(MouseSaveState_t, parser_reply_pos) == 69,
              "the reply cursor takes a byte older frames held as zero");
static_assert(offsetof(MouseSaveState_t, reserved2) == 70,
              "the PIA's two IRQ outputs stay reserved");
static_assert(offsetof(MouseSaveState_t, pia_port_a_shadow) == 72,
              "the shadows sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, pia_port_b_shadow) == 73,
              "the shadows sit where every frame written has them");
static_assert(offsetof(MouseSaveState_t, mode) == 74,
              "the mode sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, reserved3) == 75,
              "the retired VBL edge byte is reserved");
static_assert(offsetof(MouseSaveState_t, status) == 76,
              "the status byte sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, button_at_last_read) == 77,
              "the button memory sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, reserved4) == 78,
              "the second button's memory is reserved");
static_assert(offsetof(MouseSaveState_t, button) == 79,
              "the button sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, reserved5) == 80,
              "the second button's level is reserved");
static_assert(offsetof(MouseSaveState_t, buffer) == 81,
              "the command buffer sits where every frame written has it");
static_assert(offsetof(MouseSaveState_t, padding) == 89,
              "three bytes of padding close the frame");

struct MouseCard_t {
  HostInterface_t* host = nullptr;
  int slot = 0;

  Pia6821_t pia{};
  uint8_t port_a_shadow = 0;
  uint8_t port_b_shadow = 0;
  uint8_t mode = 0;
  bool vblank_rising = false;

  std::array<uint8_t, 8> buffer{};
  int32_t buffer_pos = 0;
  int32_t data_len = 0;
  uint8_t status = 0;

  uint32_t position_x = 0;
  uint32_t position_y = 0;
  uint32_t min_x = 0;
  uint32_t max_x = 0;
  uint32_t min_y = 0;
  uint32_t max_y = 0;
  uint32_t range_x = 0;
  uint32_t range_y = 0;

  int32_t read_x = 0;
  int32_t read_y = 0;
  bool button_at_last_read = false;
  bool second_button_at_last_read = false;
  std::array<bool, 2> buttons{false, false};

  std::array<uint8_t, rom_size> slot_rom{};
};

auto mouse_update_slot_rom(MouseCard_t* card) -> void {
  if (card == nullptr) {
    return;
  }
  const uint32_t bank =
      (static_cast<uint32_t>(card->port_b_shadow) & port_b::bank_mask) >>
      port_b::bank_shift;
  card->host->RegisterCxROM(card->slot,
                            card->slot_rom.data() + bank * rom_page_size);
}

auto pia_listener_a(void* obj, uint8_t data) -> void {
  if (obj == nullptr) {
    return;
  }
  auto* card = static_cast<MouseCard_t*>(obj);
  card->port_a_shadow = data;
}

auto mouse_clamp_x(MouseCard_t* card, int min_val, int max_val) -> void {
  if (card == nullptr) {
    return;
  }
  if (min_val < 0 || min_val > max_val) {
    return;
  }
  card->max_x = static_cast<uint32_t>(max_val);
  card->min_x = static_cast<uint32_t>(min_val);
  card->position_x =
      std::min(std::max(card->position_x, card->min_x), card->max_x);
}

auto mouse_clamp_y(MouseCard_t* card, int min_val, int max_val) -> void {
  if (card == nullptr) {
    return;
  }
  if (min_val < 0 || min_val > max_val) {
    return;
  }
  card->max_y = static_cast<uint32_t>(max_val);
  card->min_y = static_cast<uint32_t>(min_val);
  card->position_y =
      std::min(std::max(card->position_y, card->min_y), card->max_y);
}

auto mouse_set_position_internal(MouseCard_t* card, int x, int y) -> void {
  if (card == nullptr) {
    return;
  }
  if (card->range_x == 0 || card->range_y == 0) {
    return;
  }
  const uint32_t scaled_x =
      (static_cast<uint32_t>(x) * default_clamp_max) / card->range_x;
  const uint32_t scaled_y =
      (static_cast<uint32_t>(y) * default_clamp_max) / card->range_y;
  card->position_x = std::min(std::max(scaled_x, card->min_x), card->max_x);
  card->position_y = std::min(std::max(scaled_y, card->min_y), card->max_y);
}

auto mouse_reset_internal(MouseCard_t* card) -> void {
  if (card == nullptr) {
    return;
  }
  card->buffer_pos = 0;
  card->data_len = 1;
  card->mode = 0;
  card->status = 0;
  card->read_x = 0;
  card->read_y = 0;
  card->button_at_last_read = false;
  card->second_button_at_last_read = false;
  mouse_clamp_x(card, 0, static_cast<int>(default_clamp_max));
  mouse_clamp_y(card, 0, static_cast<int>(default_clamp_max));
  mouse_set_position_internal(card, 0, 0);
}

auto mouse_on_mouse_event(MouseCard_t* card) -> void {
  if (card == nullptr) {
    return;
  }

  uint8_t state = 0;
  if (static_cast<uint32_t>(card->read_x) != card->position_x ||
      static_cast<uint32_t>(card->read_y) != card->position_y) {
    state |= status::moved;
    if ((card->mode & mode::tracking) != 0 &&
        (card->mode & mode::movement_interrupts) != 0) {
      state |= status::movement_interrupt;
    }
  }

  if ((card->mode & mode::tracking) != 0) {
    if (card->button_at_last_read != card->buttons.at(0) ||
        card->second_button_at_last_read != card->buttons.at(1)) {
      if ((card->mode & mode::button_interrupts) != 0) {
        state |= status::button_interrupt;
      }
    }
    if (card->vblank_rising && (card->mode & mode::refresh_interrupts) != 0) {
      state |= status::refresh_interrupt;
    }
  }

  if (state == 0) {
    return;
  }
  card->status |= state;
  if ((state & status::interrupt_sources) != 0) {
    card->host->AssertIrq(card->slot, true);
  }
}

auto mouse_on_command(MouseCard_t* card) -> void {
  if (card == nullptr) {
    return;
  }

  const uint8_t cmd = card->buffer.at(0) & command::group_mask;
  switch (cmd) {
    case command::set_mouse:
      card->data_len = 1;
      card->mode = card->buffer.at(0) & mode::mask;
      mouse_on_mouse_event(card);
      break;

    case command::read_mouse:
      card->data_len = read_mouse_bytes;
      card->status &= status::moved;
      card->read_x = static_cast<int32_t>(card->position_x);
      card->read_y = static_cast<int32_t>(card->position_y);
      if (card->button_at_last_read) {
        card->status |= status::button_at_last_read;
      }
      if (card->second_button_at_last_read) {
        card->status |= 0x01;
      }
      card->button_at_last_read = card->buttons.at(0);
      card->second_button_at_last_read = card->buttons.at(1);
      if (card->button_at_last_read) {
        card->status |= status::button_down;
      }
      if (card->second_button_at_last_read) {
        card->status |= 0x10;
      }
      card->buffer.at(1) = static_cast<uint8_t>(card->read_x & byte_mask);
      card->buffer.at(2) =
          static_cast<uint8_t>((card->read_x >> byte_shift) & byte_mask);
      card->buffer.at(3) = static_cast<uint8_t>(card->read_y & byte_mask);
      card->buffer.at(4) =
          static_cast<uint8_t>((card->read_y >> byte_shift) & byte_mask);
      card->buffer.at(5) = card->status;
      card->status &= static_cast<uint8_t>(~status::moved);
      break;

    case command::serve_mouse:
      card->data_len = 2;
      card->buffer.at(1) = card->status & static_cast<uint8_t>(~status::moved);
      card->host->AssertIrq(card->slot, false);
      break;

    case command::clear_mouse:
      mouse_reset_internal(card);
      card->data_len = 1;
      break;

    case command::pos_mouse:
      card->data_len = five_byte_command;
      break;

    case command::init_mouse:
      card->data_len = 3;
      card->buffer.at(1) = byte_mask;
      break;

    case command::clamp_mouse:
      card->data_len = five_byte_command;
      break;

    case command::home_mouse:
      card->data_len = 1;
      mouse_set_position_internal(card, 0, 0);
      mouse_on_mouse_event(card);
      break;

    case command::time_data:
      switch (card->buffer.at(0) & command::time_data_length_mask) {
        case command::time_data_three_bytes:
          card->data_len = 3;
          break;
        case command::time_data_two_bytes:
          card->data_len = 2;
          break;
        case command::time_data_four_bytes:
          card->data_len = 4;
          break;
        default:
          card->data_len = 1;
          break;
      }
      break;

    default:
      card->data_len = 1;
      break;
  }
  const uint8_t val = card->buffer.at(1);
  card->pia.ora = val;
  pia_6821_set_port_a(&card->pia, val);
}

auto mouse_on_write(MouseCard_t* card) -> void {
  if (card == nullptr) {
    return;
  }

  int val_min = 0;
  int val_max = 0;
  switch (card->buffer.at(0) & command::group_mask) {
    case command::clamp_mouse:
      val_min = (card->buffer.at(2) << byte_shift) | card->buffer.at(1);
      val_max = (card->buffer.at(4) << byte_shift) | card->buffer.at(3);
      if ((card->buffer.at(0) & command::axis_bit) != 0) {
        mouse_clamp_y(card, val_min, val_max);
      } else {
        mouse_clamp_x(card, val_min, val_max);
      }
      break;

    case command::pos_mouse:
      card->read_x = (card->buffer.at(2) << byte_shift) | card->buffer.at(1);
      card->read_y = (card->buffer.at(4) << byte_shift) | card->buffer.at(3);
      mouse_set_position_internal(card, card->read_x, card->read_y);
      mouse_on_mouse_event(card);
      break;

    case command::init_mouse:
      card->read_x = 0;
      card->read_y = 0;
      mouse_clamp_x(card, 0, static_cast<int>(default_clamp_max));
      mouse_clamp_y(card, 0, static_cast<int>(default_clamp_max));
      mouse_set_position_internal(card, 0, 0);
      mouse_on_mouse_event(card);
      break;

    default:
      break;
  }
}

auto mouse_on_clock_write(MouseCard_t* card, uint8_t data) -> void {
  if (card == nullptr) {
    return;
  }

  if ((data & port_b::write_strobe) != 0) {
    card->port_b_shadow |= port_b::busy;
    return;
  }

  if (card->buffer_pos >= 0 &&
      static_cast<size_t>(card->buffer_pos) < card->buffer.size()) {
    card->buffer.at(static_cast<size_t>(card->buffer_pos++)) =
        card->port_a_shadow;
  }

  if (card->buffer_pos == 1) {
    mouse_on_command(card);
  }

  if (card->buffer_pos == card->data_len ||
      static_cast<size_t>(card->buffer_pos) >= card->buffer.size()) {
    mouse_on_write(card);
    card->buffer_pos = 0;
  }

  card->port_b_shadow &= static_cast<uint8_t>(~port_b::busy);
  pia_6821_set_port_b(&card->pia, card->port_b_shadow);
}

auto mouse_on_clock_read(MouseCard_t* card, uint8_t data) -> void {
  if (card == nullptr) {
    return;
  }

  if ((data & port_b::read_strobe) != 0) {
    card->port_b_shadow &= static_cast<uint8_t>(~port_b::byte_ready);
    return;
  }

  if (card->buffer_pos != 0) {
    card->buffer_pos++;
  }

  if (card->buffer_pos == card->data_len ||
      static_cast<size_t>(card->buffer_pos) >= card->buffer.size()) {
    card->buffer_pos = 0;
  } else {
    const uint8_t val = card->buffer.at(static_cast<size_t>(card->buffer_pos));
    card->pia.ora = val;
    pia_6821_set_port_a(&card->pia, val);
  }

  card->port_b_shadow |= port_b::byte_ready;
}

auto pia_listener_b(void* obj, uint8_t data) -> void {
  if (obj == nullptr) {
    return;
  }
  auto* card = static_cast<MouseCard_t*>(obj);

  const uint8_t diff = (card->port_b_shadow ^ data) & port_b::card_driven;
  if (diff == 0) {
    return;
  }

  card->port_b_shadow &= static_cast<uint8_t>(~port_b::card_driven);
  card->port_b_shadow |= (data & port_b::card_driven);

  if ((diff & port_b::write_strobe) != 0) {
    mouse_on_clock_write(card, data);
  }
  if ((diff & port_b::read_strobe) != 0) {
    mouse_on_clock_read(card, data);
  }

  pia_6821_set_port_b(&card->pia, card->port_b_shadow);
  mouse_update_slot_rom(card);
}

auto mouse_io(void* instance, uint16_t pc, uint16_t addr, uint8_t write,
              uint8_t val, uint32_t cycles) -> uint8_t {
  (void)pc;
  (void)cycles;
  if (instance == nullptr) {
    return 0;
  }
  auto* card = static_cast<MouseCard_t*>(instance);
  // Only A0, A1 and DEVICE SELECT' reach the PIA (schematic zone C3), so its
  // four registers repeat through $C0n4-$C0nF.
  const auto rs = static_cast<uint8_t>(addr & io_register_mask);
  if (write != 0) {
    pia_6821_write(&card->pia, rs, val);
    return 0;
  }
  return pia_6821_read(&card->pia, rs);
}

// Better no card than a phantom one; the log names the member. Log itself is
// the one refusal nothing can report.
auto missing_host_member(const HostInterface_t* host) -> const char* {
  if (host->AssertIrq == nullptr) {
    return "AssertIrq";
  }
  if (host->RegisterIO == nullptr) {
    return "RegisterIO";
  }
  if (host->RegisterCxROM == nullptr) {
    return "RegisterCxROM";
  }
  return nullptr;
}

auto mouse_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr || host->Log == nullptr) {
    return nullptr;
  }
  const char* missing = missing_host_member(host);
  if (missing != nullptr) {
    host->Log(nullptr, log_error,
              "Mouse Interface in slot %d: the host offers no %s\n", slot,
              missing);
    return nullptr;
  }
  if (slot < min_slot || slot > max_slot) {
    host->Log(nullptr, log_error,
              "Mouse Interface in slot %d: an expansion card sits in slots 1 "
              "to 7\n",
              slot);
    return nullptr;
  }

  auto card = std::unique_ptr<MouseCard_t>(new (std::nothrow) MouseCard_t());
  if (!card) {
    return nullptr;
  }
  card->host = host;
  card->slot = slot;

  pia_6821_reset(&card->pia);
  pia_6821_set_listener_a(&card->pia, card.get(), pia_listener_a);
  pia_6821_set_listener_b(&card->pia, card.get(), pia_listener_b);

  card->port_a_shadow = 0;
  card->port_b_shadow = port_b::byte_ready;
  pia_6821_set_port_b(&card->pia, card->port_b_shadow);

  card->min_x = 0;
  card->max_x = default_clamp_max;
  card->min_y = 0;
  card->max_y = default_clamp_max;
  mouse_reset_internal(card.get());

#if ENABLE_ROM_MOUSE
  std::copy(g_rom_mouse_interface,
            g_rom_mouse_interface + g_rom_mouse_interface_size,
            card->slot_rom.begin());
#endif
  mouse_update_slot_rom(card.get());

  host->RegisterIO(slot, mouse_io, mouse_io, nullptr, nullptr);
  return card.release();
}

auto mouse_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* card = static_cast<MouseCard_t*>(instance);
  mouse_reset_internal(card);
}

auto mouse_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  const std::unique_ptr<MouseCard_t> card(static_cast<MouseCard_t*>(instance));
}

auto mouse_abi_on_vblank(void* instance, bool vblank) -> void {
  if (instance == nullptr) {
    return;
  }
  auto* card = static_cast<MouseCard_t*>(instance);
  if (card->vblank_rising == vblank) {
    return;
  }
  card->vblank_rising = vblank;
  if (card->vblank_rising) {
    mouse_on_mouse_event(card);
  }
}

auto mouse_abi_save_state(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus_t {
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
  if (instance == nullptr) {
    return peripheral_error;
  }

  const auto* card = static_cast<const MouseCard_t*>(instance);
  MouseSaveState_t state{};
  state.version = MOUSE_STATE_VERSION;
  state.struct_size = static_cast<uint32_t>(required);
  state.position_x = card->position_x;
  state.position_y = card->position_y;
  state.min_x = card->min_x;
  state.max_x = card->max_x;
  state.min_y = card->min_y;
  state.max_y = card->max_y;
  state.read_x = static_cast<uint32_t>(card->read_x);
  state.read_y = static_cast<uint32_t>(card->read_y);
  state.parser_pos = static_cast<uint32_t>(card->buffer_pos);
  state.parser_out_len = static_cast<uint32_t>(card->data_len);
  state.pia_ora = card->pia.ora;
  state.pia_orb = card->pia.orb;
  state.pia_ddra = card->pia.ddra;
  state.pia_ddrb = card->pia.ddrb;
  state.pia_cra = card->pia.cra;
  state.pia_crb = card->pia.crb;
  state.pia_port_a_in = card->pia.port_a_in;
  state.pia_port_b_in = card->pia.port_b_in;
  state.pia_port_a_shadow = card->port_a_shadow;
  state.pia_port_b_shadow = card->port_b_shadow;
  state.mode = card->mode;
  state.status = card->status;
  state.button_at_last_read = card->button_at_last_read ? 1 : 0;
  state.button = card->buttons.at(0) ? 1 : 0;
  std::copy(card->buffer.begin(), card->buffer.end(), state.buffer);
  std::memcpy(buffer, &state, required);

  *size = required;
  return peripheral_ok;
}

auto mouse_abi_load_state(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus_t {
  if (instance == nullptr || buffer == nullptr ||
      size != sizeof(MouseSaveState_t)) {
    return peripheral_error;
  }
  MouseSaveState_t state{};
  std::memcpy(&state, buffer, sizeof(state));
  if (state.version != MOUSE_STATE_VERSION ||
      state.struct_size != sizeof(MouseSaveState_t)) {
    return peripheral_error;
  }

  auto* card = static_cast<MouseCard_t*>(instance);
  pia_6821_reset(&card->pia);
  card->pia.ora = state.pia_ora;
  card->pia.orb = state.pia_orb;
  card->pia.ddra = state.pia_ddra;
  card->pia.ddrb = state.pia_ddrb;
  card->pia.cra = state.pia_cra;
  card->pia.crb = state.pia_crb;
  card->pia.port_a_in = state.pia_port_a_in;
  card->pia.port_b_in = state.pia_port_b_in;
  card->port_a_shadow = state.pia_port_a_shadow;
  card->port_b_shadow = state.pia_port_b_shadow;
  card->mode = state.mode;

  std::copy(state.buffer, state.buffer + card->buffer.size(),
            card->buffer.begin());
  card->buffer_pos = state.parser_pos < card->buffer.size()
                         ? static_cast<int32_t>(state.parser_pos)
                         : 0;
  card->data_len = state.parser_out_len <= card->buffer.size()
                       ? static_cast<int32_t>(state.parser_out_len)
                       : static_cast<int32_t>(card->buffer.size());
  card->status = state.status;

  card->position_x = state.position_x;
  card->position_y = state.position_y;
  card->min_x = state.min_x;
  card->max_x = state.max_x;
  card->min_y = state.min_y;
  card->max_y = state.max_y;
  card->read_x = static_cast<int32_t>(state.read_x);
  card->read_y = static_cast<int32_t>(state.read_y);
  card->button_at_last_read = state.button_at_last_read != 0;
  card->buttons.at(0) = state.button != 0;

  pia_6821_set_listener_a(&card->pia, card, pia_listener_a);
  pia_6821_set_listener_b(&card->pia, card, pia_listener_b);
  mouse_update_slot_rom(card);
  return peripheral_ok;
}

auto mouse_abi_command(void* instance, uint32_t cmd_id, const void* data,
                       size_t size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  if (!peripheral_cmd_is_mine(cmd_id, PERIPHERAL_SUBSYSTEM_MOUSE)) {
    return peripheral_incompatible;  // another peripheral in the slot owns it
  }
  if (data == nullptr) {
    return peripheral_error;
  }

  auto* card = static_cast<MouseCard_t*>(instance);
  switch (static_cast<MouseCmd_t>(cmd_id)) {
    case mouse_cmd_move: {
      if (size != sizeof(MouseMovePayload_t)) {
        return peripheral_error;
      }
      return peripheral_ok;
    }
    case mouse_cmd_set_button: {
      if (size != sizeof(MouseButtonPayload_t)) {
        return peripheral_error;
      }
      MouseButtonPayload_t payload{};
      std::memcpy(&payload, data, sizeof(payload));
      if (payload.button < card->buttons.size()) {
        card->buttons.at(payload.button) = payload.down != 0;
        mouse_on_mouse_event(card);
      }
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

auto mouse_abi_query(void* instance, uint32_t query_id, void* out,
                     size_t* out_size) -> PeripheralStatus_t {
  if (instance == nullptr || out_size == nullptr) {
    return peripheral_error;
  }
  if (!peripheral_cmd_is_mine(query_id, PERIPHERAL_SUBSYSTEM_MOUSE)) {
    return peripheral_incompatible;  // another peripheral in the slot owns it
  }
  if (query_id != mouse_query_is_active) {
    return peripheral_incompatible;
  }

  const size_t required = sizeof(uint8_t);
  if (out == nullptr) {
    *out_size = required;
    return peripheral_ok;
  }
  if (*out_size < required) {
    *out_size = required;
    return peripheral_error;
  }
  *static_cast<uint8_t*>(out) = 1;
  *out_size = required;
  return peripheral_ok;
}

}  // namespace

static const Peripheral_t mouse_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.mouse",
    .name = "Mouse Interface",
    .description =
        "Apple AppleMouse II interface card (AppleMouse II User's Manual, "
        "1983)",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = 4,
    .init = mouse_abi_init,
    .reset = mouse_abi_reset,
    .shutdown = mouse_abi_shutdown,
    .think = nullptr,
    .on_vblank = mouse_abi_on_vblank,
    .save_state = mouse_abi_save_state,
    .load_state = mouse_abi_load_state,
    .command = mouse_abi_command,
    .query = mouse_abi_query};

// peripheral_register and ActivePeripheral_t::api take a mutable
// Peripheral_t*, so the immutable descriptor is cast the same way
// PERIPHERAL_REGISTER casts it.
auto mouse_get_descriptor() -> Peripheral_t* {
  return const_cast<Peripheral_t*>(&mouse_peripheral);
}

PERIPHERAL_REGISTER(mouse_peripheral)
