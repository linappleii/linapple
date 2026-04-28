/*
 * Keyboard.cpp - LinApple Keyboard Peripheral Implementation
 */

#include <algorithm>
#include "apple2/Keyboard_Structs.h"
#include "apple2/KeyboardCommands.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "apple2/Memory.h"
#include "core/Peripheral.h"

// --- Constants ---

static constexpr uint8_t KEY_STROBE_BIT = 0x80;
static constexpr uint8_t KEY_CODE_MASK  = 0x7F;

// Standard Apple II repeat circuit delays (~0.5s initial, ~0.06s repeat)
static constexpr uint32_t KEY_REPEAT_INITIAL_DELAY = 512000;
static constexpr uint32_t KEY_REPEAT_RATE = 68000;

// --- I/O Address Constants ---

static constexpr uint16_t ADDR_KEYB_DATA_LO  = 0xC000;
static constexpr uint16_t ADDR_KEYB_DATA_HI  = 0xC00F;
static constexpr uint16_t ADDR_KEYB_STROBE   = 0xC010;
static constexpr uint16_t ADDR_KEYB_STROBE_HI = 0xC01F;
static constexpr uint16_t ADDR_OPEN_APPLE    = 0xC061;
static constexpr uint16_t ADDR_CLOSED_APPLE  = 0xC062;
static constexpr uint16_t ADDR_SHIFT_KEY     = 0xC063;

// --- Internal State ---

struct KeyboardPeripheral_t {
  Keyboard_t logic;
  HostInterface_t* host = nullptr;
  int slot = 0;
  uint8_t current_latch = 0; // $C000 bits 0-6
  bool strobe = false;        // $C000 bit 7
  bool rocker_switch = false; // Language rocker switch (US=false, Local=true)
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static KeyboardPeripheral_t* g_active_keyboard_instance = nullptr;

// --- I/O Handlers ---

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static auto Keyb_IO_ReadData(void* instance, uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  (void)pc; (void)addr; (void)bWrite; (void)d; (void)nCyclesLeft;
  if (!instance) return MemReadFloatingBus(nCyclesLeft);
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  uint8_t val = kp->current_latch;
  if (kp->strobe) val |= KEY_STROBE_BIT;

  return val;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static auto Keyb_IO_StrobeAction(void* instance, uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  (void)pc; (void)addr; (void)bWrite; (void)d; (void)nCyclesLeft;
  if (!instance) return MemReadFloatingBus(nCyclesLeft);
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  // Any access (read or write) to $C010-$C01F clears the strobe.
  kp->strobe = false;

  // Bit 7 of Read: Any-Key-Down flag.
  uint8_t val = kp->current_latch;
  if (kp->logic.keys_down_count > 0) val |= KEY_STROBE_BIT;

  return val;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static auto Keyb_IO_ReadAppleKeys(void* instance, uint16_t pc, uint16_t addr, uint8_t bWrite, uint8_t d, uint32_t nCyclesLeft) -> uint8_t {
  (void)pc; (void)bWrite; (void)d; (void)nCyclesLeft;
  if (!instance) return MemReadFloatingBus(nCyclesLeft);
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  bool pressed = false;
  // Map host modifiers to Apple II game buttons
  if (addr == ADDR_OPEN_APPLE)   { pressed = kp->logic.gui_key;   }  // Open Apple
  else if (addr == ADDR_CLOSED_APPLE) { pressed = kp->logic.alt_key;   }  // Closed Apple
  else if (addr == ADDR_SHIFT_KEY)    { pressed = kp->logic.shift_key; }  // Shift Mod

  uint8_t bus = MemReadFloatingBus(nCyclesLeft) & KEY_CODE_MASK;
  if (pressed) { bus |= KEY_STROBE_BIT; }

  return bus;
}

// --- ABI Implementation ---

// NOLINTNEXTLINE(modernize-use-trailing-return-type) - C ABI requires void* return
static void* Keyb_ABI_Init(int slot, HostInterface_t* host) {
  // Keyboard is a singleton; return existing instance if already initialized.
  if (g_active_keyboard_instance) {
    return g_active_keyboard_instance;
  }

  // Justification: raw new/delete required by the Peripheral C ABI (void* instance lifetime).
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  auto* kp = new KeyboardPeripheral_t{};
  kp->host = host;
  kp->slot = slot;
  kp->logic.caps_lock = true;

  if (host && host->RegisterDirectIO) {
    for (uint16_t addr = ADDR_KEYB_DATA_LO; addr <= ADDR_KEYB_DATA_HI; ++addr) {
      host->RegisterDirectIO(kp, addr, Keyb_IO_ReadData, nullptr);
    }
    // $C010 (KBDSTRB): read and write both clear the strobe.
    // $C011-$C01F: reads are soft-switch status (owned by Memory.cpp); writes clear
    // the strobe. Register write-only here so reads are unaffected.
    host->RegisterDirectIO(kp, ADDR_KEYB_STROBE, Keyb_IO_StrobeAction, Keyb_IO_StrobeAction);
    for (uint16_t addr = ADDR_KEYB_STROBE + 1; addr <= ADDR_KEYB_STROBE_HI; ++addr) {
      host->RegisterDirectIO(kp, addr, nullptr, Keyb_IO_StrobeAction);
    }
    host->RegisterDirectIO(kp, ADDR_OPEN_APPLE,    Keyb_IO_ReadAppleKeys, nullptr);
    host->RegisterDirectIO(kp, ADDR_CLOSED_APPLE,  Keyb_IO_ReadAppleKeys, nullptr);
    host->RegisterDirectIO(kp, ADDR_SHIFT_KEY,     Keyb_IO_ReadAppleKeys, nullptr);
  }

  g_active_keyboard_instance = kp;
  return kp;
}

static void Keyb_ABI_Reset(void* instance) {
  if (!instance) return;
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  kp->current_latch = 0;
  kp->strobe = false;
  kp->logic.keys_down_count = 0;
  kp->logic.repeat_key = 0;
  kp->logic.repeat_delay_cycles = 0;
  kp->logic.repeating = false;
}

static void Keyb_ABI_Shutdown(void* instance) {
  if (!instance) return;
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  if (g_active_keyboard_instance == kp) { g_active_keyboard_instance = nullptr; }
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  delete kp;
}

static void Keyb_ABI_Think(void* instance, uint32_t cycles) {
  if (!instance) return;
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  // Auto-repeat requirement: only if not II/II+ (which had a physical REPT key)
  if (IS_APPLE2()) {
    return;
  }

  if (kp->logic.repeat_key == 0) return;

  // Clamp to prevent uint32_t wrap on abnormally large cycle batches (e.g. pause/resume).
  cycles = std::min(cycles, KEY_REPEAT_INITIAL_DELAY);
  kp->logic.repeat_delay_cycles += cycles;
  uint32_t delay = kp->logic.repeating ? KEY_REPEAT_RATE : KEY_REPEAT_INITIAL_DELAY;

  if (kp->logic.repeat_delay_cycles >= delay) {
    kp->logic.repeating = true;
    kp->logic.repeat_delay_cycles -= delay;
    kp->strobe = true;
    while (kp->logic.repeat_delay_cycles >= KEY_REPEAT_RATE) {
      kp->logic.repeat_delay_cycles -= KEY_REPEAT_RATE;
    }
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static auto Keyb_ABI_Command(void* instance, uint32_t cmd_id, const void* data, size_t size) -> PeripheralStatus {
  if (!instance || (size > 0 && !data)) { return PERIPHERAL_ERROR; }
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);

  switch (static_cast<KeyboardCmd_e>(cmd_id)) {
    case KEYB_CMD_EVENT: {
      if (size < sizeof(KeyboardEvent_t)) return PERIPHERAL_ERROR;
      const auto* ev = static_cast<const KeyboardEvent_t*>(data);

      if (ev->is_down) {
        kp->current_latch = ev->ascii & KEY_CODE_MASK;
        kp->strobe = true;
        kp->logic.keys_down_count++;
        kp->logic.repeat_key = ev->ascii;
        kp->logic.repeat_delay_cycles = 0;
        kp->logic.repeating = false;
      } else {
        if (kp->logic.keys_down_count > 0) kp->logic.keys_down_count--;
        kp->logic.repeat_key = 0;
      }
      return PERIPHERAL_OK;
    }
    case KEYB_CMD_SET_CAPS: {
      if (size < sizeof(uint8_t)) return PERIPHERAL_ERROR;
      kp->logic.caps_lock = (*static_cast<const uint8_t*>(data) != 0);
      return PERIPHERAL_OK;
    }
    case KEYB_CMD_SET_ROCKER: {
      if (size < sizeof(uint8_t)) return PERIPHERAL_ERROR;
      kp->rocker_switch = (*static_cast<const uint8_t*>(data) != 0);
      g_LanguageRockerSwitch = kp->rocker_switch;
      return PERIPHERAL_OK;
    }
    case KEYB_CMD_SET_MODS: {
      if (size < sizeof(KeyboardModifiers_t)) return PERIPHERAL_ERROR;
      const auto* mods = static_cast<const KeyboardModifiers_t*>(data);
      kp->logic.shift_key = (mods->shift != 0);
      kp->logic.ctrl_key = (mods->ctrl != 0);
      kp->logic.alt_key = (mods->alt != 0);
      kp->logic.gui_key = (mods->gui != 0);
      return PERIPHERAL_OK;
    }
    default: return PERIPHERAL_INCOMPATIBLE;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static auto Keyb_ABI_SaveState(void* instance, void* buffer, size_t* size) -> PeripheralStatus {
  if (!size) return PERIPHERAL_ERROR;
  if (!buffer) { *size = sizeof(SS_IO_Keyboard); return PERIPHERAL_OK; }
  if (!instance) return PERIPHERAL_ERROR;
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  auto* pSS = static_cast<SS_IO_Keyboard*>(buffer);
  pSS->nLastKey = kp->current_latch;
  *size = sizeof(SS_IO_Keyboard);
  return PERIPHERAL_OK;
}

static auto Keyb_ABI_LoadState(void* instance, const void* buffer, size_t size) -> PeripheralStatus {
  if (!buffer || size < sizeof(SS_IO_Keyboard) || !instance) return PERIPHERAL_ERROR;
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  const auto* pSS = static_cast<const SS_IO_Keyboard*>(buffer);
  kp->current_latch = pSS->nLastKey;
  return PERIPHERAL_OK;
}

static auto Keyb_ABI_Query(void* instance, uint32_t cmd_id, void* out, size_t* out_size) -> PeripheralStatus {
  if (!instance || !out_size) return PERIPHERAL_ERROR;
  auto* kp = static_cast<KeyboardPeripheral_t*>(instance);
  switch (static_cast<KeyboardQuery_e>(cmd_id)) {
    case KEYB_QUERY_MODS: {
      if (!out) { *out_size = sizeof(KeyboardModifiers_t); return PERIPHERAL_OK; }
      if (*out_size < sizeof(KeyboardModifiers_t)) return PERIPHERAL_ERROR;
      auto* mods = static_cast<KeyboardModifiers_t*>(out);
      mods->shift = kp->logic.shift_key  ? 1U : 0U;
      mods->ctrl  = kp->logic.ctrl_key   ? 1U : 0U;
      mods->alt   = kp->logic.alt_key    ? 1U : 0U;
      mods->gui   = kp->logic.gui_key    ? 1U : 0U;
      mods->caps  = kp->logic.caps_lock  ? 1U : 0U;
      *out_size = sizeof(KeyboardModifiers_t);
      return PERIPHERAL_OK;
    }
    case KEYB_QUERY_ROCKER: {
      if (!out) { *out_size = sizeof(uint8_t); return PERIPHERAL_OK; }
      if (*out_size < sizeof(uint8_t)) return PERIPHERAL_ERROR;
      *static_cast<uint8_t*>(out) = kp->rocker_switch ? 1U : 0U;
      *out_size = sizeof(uint8_t);
      return PERIPHERAL_OK;
    }
    default: return PERIPHERAL_INCOMPATIBLE;
  }
}

Peripheral_t g_keyboard_peripheral = {
  LINAPPLE_ABI_VERSION,
  "Keyboard",
  0x01,
  Keyb_ABI_Init,
  Keyb_ABI_Reset,
  Keyb_ABI_Shutdown,
  Keyb_ABI_Think,
  nullptr, // on_vblank
  Keyb_ABI_SaveState,
  Keyb_ABI_LoadState,
  Keyb_ABI_Command,
  Keyb_ABI_Query
};

#ifdef BUILD_SHARED_PERIPHERAL
EXPORT_PERIPHERAL(g_keyboard_peripheral)
#endif


