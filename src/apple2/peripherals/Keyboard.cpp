/*
 * Keyboard.cpp - LinApple Keyboard Peripheral Implementation
 */

// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables) Justification: This file
// implements the C11-compatible Peripheral ABI. It requires void* pointers for
// instance state, raw memory management, and static global state to bridge with
// the core C interface and maintain peripheral singletons.

#include <algorithm>

#include "apple2/Memory.h"
#include "apple2/peripherals/KeyboardCommands.h"
#include "apple2/peripherals/Keyboard_Structs.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/Peripheral.h"

// --- Constants ---

static constexpr uint8_t KEY_STROBE_BIT = 0x80;
static constexpr uint8_t KEY_CODE_MASK = 0x7F;

// Standard Apple II repeat circuit delays (~0.5s initial, ~0.06s repeat)
static constexpr uint32_t KEY_REPEAT_INITIAL_DELAY = 512000;
static constexpr uint32_t KEY_REPEAT_RATE = 68000;

// --- I/O Address Constants ---

static constexpr uint16_t ADDR_KEYB_DATA_LO = 0xC000;
static constexpr uint16_t ADDR_KEYB_DATA_HI = 0xC00F;
static constexpr uint16_t ADDR_KEYB_STROBE = 0xC010;
static constexpr uint16_t ADDR_KEYB_STROBE_HI = 0xC01F;
static constexpr uint16_t ADDR_OPEN_APPLE = 0xC061;
static constexpr uint16_t ADDR_CLOSED_APPLE = 0xC062;
static constexpr uint16_t ADDR_SHIFT_KEY = 0xC063;

// --- Internal State ---

struct KeyboardPeripheral_t {
  Keyboard_t logic{};
  HostInterface_t* host = nullptr;
  int slot = 0;
  uint8_t current_latch = 0;   // $C000 bits 0-6
  bool strobe = false;         // $C000 bit 7
  bool rocker_switch = false;  // Language rocker switch (US=false, Local=true)
};

// --- I/O Handlers ---

static auto Keyb_IO_ReadData(void* instance, uint16_t pc, uint16_t addr,
                             uint8_t write, uint8_t val, uint32_t cycles_left)
    -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)val;
  (void)cycles_left;
  if (!instance) {
    return MemReadFloatingBus(cycles_left);
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  uint8_t data = kp->current_latch;
  if (kp->strobe) {
    data |= KEY_STROBE_BIT;
  }

  return data;
}

static auto Keyb_IO_StrobeAction(void* instance, uint16_t pc, uint16_t addr,
                                 uint8_t write, uint8_t val,
                                 uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)val;
  (void)cycles_left;
  if (!instance) {
    return MemReadFloatingBus(cycles_left);
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  // Strobe latch is cleared by hardware on any access to the $C010-$C01F range.
  kp->strobe = false;

  uint8_t data = kp->current_latch;
  if (kp->logic.keys_down_count > 0) {
    data |= KEY_STROBE_BIT;
  }

  return data;
}

static auto Keyb_IO_ReadAppleKeys(void* instance, uint16_t pc, uint16_t addr,
                                  uint8_t write, uint8_t val,
                                  uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)write;
  (void)val;
  (void)cycles_left;
  if (!instance) {
    return MemReadFloatingBus(cycles_left);
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  bool pressed = false;
  // Host modifiers are used to emulate the physical Apple II game buttons and
  // shift key.
  if (addr == ADDR_OPEN_APPLE) {
    pressed = kp->logic.gui_key;
  } else if (addr == ADDR_CLOSED_APPLE) {
    pressed = kp->logic.alt_key;
  } else if (addr == ADDR_SHIFT_KEY) {
    pressed = kp->logic.shift_key;
  }

  uint8_t bus = MemReadFloatingBus(cycles_left) & KEY_CODE_MASK;
  if (pressed) {
    bus |= KEY_STROBE_BIT;
  }

  return bus;
}

// --- ABI Implementation ---

static void* Keyb_ABI_Init(int slot, HostInterface_t* host) {
  // Keyboard is a singleton; return existing instance if already initialized.

  auto* kp = new KeyboardPeripheral_t{};
  kp->host = host;
  kp->slot = slot;
  kp->logic.caps_lock = true;

  if (host && host->RegisterDirectIO) {
    for (uint32_t addr = ADDR_KEYB_DATA_LO; addr <= ADDR_KEYB_DATA_HI; ++addr) {
      host->RegisterDirectIO(kp, static_cast<uint16_t>(addr), Keyb_IO_ReadData,
                             nullptr);
    }
    // $C010 (KBDSTRB): read and write both clear the strobe.
    // $C011-$C01F: reads are soft-switch status (owned by Memory.cpp); writes
    // clear the strobe. Register write-only here so reads are unaffected.
    host->RegisterDirectIO(kp, ADDR_KEYB_STROBE, Keyb_IO_StrobeAction,
                           Keyb_IO_StrobeAction);
    for (uint32_t addr = ADDR_KEYB_STROBE + 1; addr <= ADDR_KEYB_STROBE_HI;
         ++addr) {
      host->RegisterDirectIO(kp, static_cast<uint16_t>(addr), nullptr,
                             Keyb_IO_StrobeAction);
    }
    host->RegisterDirectIO(kp, ADDR_OPEN_APPLE, Keyb_IO_ReadAppleKeys, nullptr);
    host->RegisterDirectIO(kp, ADDR_CLOSED_APPLE, Keyb_IO_ReadAppleKeys,
                           nullptr);
    host->RegisterDirectIO(kp, ADDR_SHIFT_KEY, Keyb_IO_ReadAppleKeys, nullptr);
  }

  return kp;
}

static void Keyb_ABI_Reset(void* instance) {
  if (!instance) {
    return;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  kp->current_latch = 0;
  kp->strobe = false;
  kp->logic.keys_down_count = 0;
  kp->logic.repeat_key = 0;
  kp->logic.repeat_delay_cycles = 0;
  kp->logic.repeating = false;
}

static void Keyb_ABI_Shutdown(void* instance) {
  if (!instance) {
    return;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  delete kp;
}

static void Keyb_ABI_Think(void* instance, uint32_t cycles) {
  if (!instance) {
    return;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  // Original Apple II/II+ hardware required a physical REPT key for repeats.
  if (IS_APPLE2()) {
    return;
  }

  if (kp->logic.repeat_key == 0) {
    return;
  }

  // Clamp is necessary because extremely large cycle counts (e.g. from a
  // debugger pause) could otherwise cause the repeat logic to miscalculate or
  // wrap.
  cycles = std::min(cycles, KEY_REPEAT_INITIAL_DELAY);
  kp->logic.repeat_delay_cycles += cycles;
  uint32_t delay =
      kp->logic.repeating ? KEY_REPEAT_RATE : KEY_REPEAT_INITIAL_DELAY;

  if (kp->logic.repeat_delay_cycles >= delay) {
    kp->logic.repeating = true;
    kp->logic.repeat_delay_cycles -= delay;
    kp->strobe = true;
    // Draining prevents multiple repeat strokes from accumulating if Think is
    // called infrequently.
    kp->logic.repeat_delay_cycles %= KEY_REPEAT_RATE;
  }
}

static auto Keyb_ABI_Command(void* instance, uint32_t cmd_id, const void* data,
                             size_t size) -> PeripheralStatus {
  if (!instance || (size > 0 && !data)) {
    return PERIPHERAL_ERROR;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  switch (static_cast<KeyboardCmd_e>(cmd_id)) {
    case KEYB_CMD_EVENT: {
      if (size < sizeof(KeyboardEvent_t)) {
        return PERIPHERAL_ERROR;
      }
      const auto* ev = static_cast<const KeyboardEvent_t*>(data);

      if (ev->is_down) {
        if (ev->ascii > KEY_CODE_MASK) {
          // Positional mapping provided a non-ASCII code that the Apple II
          // cannot process.
          return PERIPHERAL_OK;
        }

        kp->current_latch = ev->ascii;
        kp->strobe = true;
        kp->logic.keys_down_count++;
        kp->logic.repeat_key = ev->ascii;
        kp->logic.repeat_delay_cycles = 0;
        kp->logic.repeating = false;
      } else {
        if (kp->logic.keys_down_count > 0) {
          kp->logic.keys_down_count--;
        }

        // Auto-repeat stops if the repeating key is released or if no physical
        // keys are down.
        if (ev->ascii == kp->logic.repeat_key ||
            kp->logic.keys_down_count == 0) {
          kp->logic.repeat_key = 0;
          kp->logic.repeating = false;
        }
      }
      return PERIPHERAL_OK;
    }
    case KEYB_CMD_SET_CAPS: {
      if (size < sizeof(uint8_t)) {
        return PERIPHERAL_ERROR;
      }
      kp->logic.caps_lock = (*static_cast<const uint8_t*>(data) != 0);
      return PERIPHERAL_OK;
    }
    case KEYB_CMD_SET_ROCKER: {
      if (size < sizeof(uint8_t)) {
        return PERIPHERAL_ERROR;
      }
      kp->rocker_switch = (*static_cast<const uint8_t*>(data) != 0);
      language_rocker_switch = kp->rocker_switch;
      return PERIPHERAL_OK;
    }
    case KEYB_CMD_SET_MODS: {
      if (size < sizeof(KeyboardModifiers_t)) {
        return PERIPHERAL_ERROR;
      }
      const auto* mods = static_cast<const KeyboardModifiers_t*>(data);
      kp->logic.shift_key = (mods->shift != 0);
      kp->logic.ctrl_key = (mods->ctrl != 0);
      kp->logic.alt_key = (mods->alt != 0);
      kp->logic.gui_key = (mods->gui != 0);
      return PERIPHERAL_OK;
    }
    default:
      return PERIPHERAL_INCOMPATIBLE;
  }
}

static auto Keyb_ABI_SaveState(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus {
  if (!size) {
    return PERIPHERAL_ERROR;
  }
  if (!buffer) {
    *size = sizeof(SS_IO_Keyboard);
    return PERIPHERAL_OK;
  }
  if (!instance) {
    return PERIPHERAL_ERROR;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  auto* ss = static_cast<SS_IO_Keyboard*>(buffer);
  ss->last_key = kp->current_latch;
  *size = sizeof(SS_IO_Keyboard);
  return PERIPHERAL_OK;
}

static auto Keyb_ABI_LoadState(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus {
  if (!buffer || size < sizeof(SS_IO_Keyboard) || !instance) {
    return PERIPHERAL_ERROR;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  const auto* ss = static_cast<const SS_IO_Keyboard*>(buffer);
  kp->current_latch = ss->last_key;
  return PERIPHERAL_OK;
}

static auto Keyb_ABI_Query(void* instance, uint32_t cmd_id, void* out,
                           size_t* out_size) -> PeripheralStatus {
  if (!instance || !out_size) {
    return PERIPHERAL_ERROR;
  }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  switch (static_cast<KeyboardQuery_e>(cmd_id)) {
    case KEYB_QUERY_MODS: {
      if (!out) {
        *out_size = sizeof(KeyboardModifiers_t);
        return PERIPHERAL_OK;
      }
      if (*out_size < sizeof(KeyboardModifiers_t)) {
        return PERIPHERAL_ERROR;
      }
      auto* mods = static_cast<KeyboardModifiers_t*>(out);
      mods->shift = kp->logic.shift_key ? 1U : 0U;
      mods->ctrl = kp->logic.ctrl_key ? 1U : 0U;
      mods->alt = kp->logic.alt_key ? 1U : 0U;
      mods->gui = kp->logic.gui_key ? 1U : 0U;
      mods->caps = kp->logic.caps_lock ? 1U : 0U;
      *out_size = sizeof(KeyboardModifiers_t);
      return PERIPHERAL_OK;
    }
    case KEYB_QUERY_ROCKER: {
      if (!out) {
        *out_size = sizeof(uint8_t);
        return PERIPHERAL_OK;
      }
      if (*out_size < sizeof(uint8_t)) {
        return PERIPHERAL_ERROR;
      }
      *static_cast<uint8_t*>(out) = kp->rocker_switch ? 1U : 0U;
      *out_size = sizeof(uint8_t);
      return PERIPHERAL_OK;
    }
    default:
      return PERIPHERAL_INCOMPATIBLE;
  }
}

Peripheral_t keyboard_peripheral = {LINAPPLE_ABI_VERSION,
                                    "linapple.keyboard",
                                    "Keyboard",
                                    "Standard Apple II keyboard emulation",
                                    "LinApple Contributors",
                                    VERSIONSTRING,
                                    0x01,  // Slot 0 (Internal)
                                    0,     // Default Slot 0
                                    Keyb_ABI_Init,
                                    Keyb_ABI_Reset,
                                    Keyb_ABI_Shutdown,
                                    Keyb_ABI_Think,
                                    nullptr,  // on_vblank
                                    Keyb_ABI_SaveState,
                                    Keyb_ABI_LoadState,
                                    Keyb_ABI_Command,
                                    Keyb_ABI_Query};

// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables)
PERIPHERAL_REGISTER(keyboard_peripheral)
