// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables) Justification: This file
// implements the C11-compatible Peripheral ABI. It requires void* pointers for
// instance state, raw memory management, and static global state to bridge with
// the core C interface and maintain peripheral singletons.

#include "apple2/Joystick.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Structs.h"
#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/Log.h"
#include "core/Peripheral.h"

enum { BUTTONTIME = 5000 };

enum {
  DEVICE_NONE = 0,
  DEVICE_JOYSTICK = 1,
  DEVICE_KEYBOARD = 2,
  DEVICE_MOUSE = 3
};

enum {
  MODE_NONE = 0,
  MODE_STANDARD = 1,
  MODE_CENTERING = 2,
  MODE_SMOOTH = 3
};

using joyinforec = struct joyinforec {
  int device;
  int mode;
};

static const std::array<joyinforec, 5> joyinfo = {
    {{DEVICE_NONE, MODE_NONE},
     {DEVICE_JOYSTICK, MODE_STANDARD},
     {DEVICE_KEYBOARD, MODE_STANDARD},
     {DEVICE_KEYBOARD, MODE_CENTERING},
     {DEVICE_MOUSE,    MODE_STANDARD}}};

struct JoystickPeripheral_t {
  std::array<uint32_t, 2> joytype = {{DEVICE_JOYSTICK, DEVICE_NONE}};
  std::array<uint32_t, 3> buttonlatch = {{0, 0, 0}};
  std::array<bool, 3> joybutton = {{false, false, false}};
  std::array<int, 2> xpos = {{127, 127}};
  std::array<int, 2> ypos = {{127, 127}};
  uint64_t reset_cycle = 0;
  int trim_x = 0;
  int trim_y = 0;
  HostInterface_t* host = nullptr;
  int slot = 0;
};

static JoystickPeripheral_t* active_joystick_instance = nullptr;

// Emulation Type for joysticks #0 & #1 (Legacy support)
std::array<uint32_t, 2> joytype = {{DEVICE_JOYSTICK, DEVICE_NONE}};

uint32_t joy1index = 0;
uint32_t joy2index = 1;
uint32_t joy1button1 = 0;
uint32_t joy1button2 = 1;
uint32_t joy2button1 = 0;
uint32_t joy1axis0 = 0;
uint32_t joy1axis1 = 1;
uint32_t joy2axis0 = 0;
uint32_t joy2axis1 = 1;
uint32_t joyexitenable = 0;
uint32_t joyexitbutton0 = 8;
uint32_t joyexitbutton1 = 9;
bool joyquitevent = false;

static const double PDL_CNTR_INTERVAL = 2816.0 / 255.0;  // 11.04 (From KEGS)

// --- Internal Functions ---

static auto Joy_IO_ReadButton(void* instance, uint16_t pc, uint16_t addr,
                              uint8_t write, uint8_t val, uint32_t cycles_left)
    -> uint8_t {
  (void)pc;
  (void)write;
  (void)val;
  if (!instance) {
    return MemReadFloatingBus(cycles_left);
  }
  auto* jp = static_cast<JoystickPeripheral_t*>(instance);

  int idx = (addr & 0xFF) - 0x61;
  bool pressed = false;
  if (idx >= 0 && idx < 3) {
    pressed = (jp->buttonlatch[static_cast<size_t>(idx)] ||
               jp->joybutton[static_cast<size_t>(idx)]);
    jp->buttonlatch[static_cast<size_t>(idx)] = 0;
  }
  return MemReadFloatingBus(pressed, cycles_left);
}

static auto Joy_IO_ReadPosition(void* instance, uint16_t pc, uint16_t addr,
                                uint8_t write, uint8_t val,
                                uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)write;
  (void)val;
  if (!instance) {
    return MemReadFloatingBus(cycles_left);
  }
  auto* jp = static_cast<JoystickPeripheral_t*>(instance);

  int nJoyNum = (addr & 2) ? 1 : 0;  // $C064..$C067
  CpuCalcCycles(cycles_left);

  uint32_t nPdlPos = (addr & 1)
                         ? static_cast<uint32_t>(jp->ypos[static_cast<size_t>(nJoyNum)])
                         : static_cast<uint32_t>(jp->xpos[static_cast<size_t>(nJoyNum)]);

  bool nPdlCntrActive = (g_nCumulativeCycles <= (jp->reset_cycle + static_cast<uint64_t>(static_cast<double>(nPdlPos) * PDL_CNTR_INTERVAL)));

  return MemReadFloatingBus(nPdlCntrActive, cycles_left);
}

static auto Joy_IO_ResetPosition(void* instance, uint16_t pc, uint16_t addr,
                                 uint8_t write, uint8_t val,
                                 uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)addr;
  (void)write;
  (void)val;
  if (!instance) {
    return MemReadFloatingBus(cycles_left);
  }
  auto* jp = static_cast<JoystickPeripheral_t*>(instance);

  CpuCalcCycles(cycles_left);
  jp->reset_cycle = g_nCumulativeCycles;
  return MemReadFloatingBus(cycles_left);
}

// --- ABI Implementation ---

static void* Joystick_ABI_Init(int slot, HostInterface_t* host) {
  if (active_joystick_instance) {
    return active_joystick_instance;
  }

  auto* jp = new JoystickPeripheral_t{};
  jp->host = host;
  jp->slot = slot;

  active_joystick_instance = jp;
  return jp;
}

Peripheral_t g_joystick_peripheral = {LINAPPLE_ABI_VERSION,
                                      "Joystick",
                                      0x01,  // Slot 0 (Internal)
                                      Joystick_ABI_Init,
                                      nullptr,  // reset
                                      nullptr,  // shutdown
                                      nullptr,  // think
                                      nullptr,  // on_vblank
                                      nullptr,  // save_state
                                      nullptr,  // load_state
                                      nullptr,  // command
                                      nullptr};

extern "C" void Register_Joystick() {
  Peripheral_Register_Builtin(&g_joystick_peripheral);
}

#ifdef BUILD_SHARED_PERIPHERAL
EXPORT_PERIPHERAL(g_joystick_peripheral)
#endif

// --- Legacy Procedural API ---

void JoyShutDown() {
}

void JoyInitialize() {
  // Initialization now handled by Peripheral Manager
}

void JoyReset() {
}

auto JoyReadButton(uint16_t pc, uint16_t address, uint8_t write, uint8_t val, uint32_t nCyclesLeft) -> uint8_t {
  return Joy_IO_ReadButton(active_joystick_instance, pc, address, write, val, nCyclesLeft);
}

auto JoyReadPosition(uint16_t pc, uint16_t address, uint8_t write, uint8_t val, uint32_t nCyclesLeft) -> uint8_t {
  return Joy_IO_ReadPosition(active_joystick_instance, pc, address, write, val, nCyclesLeft);
}

auto JoyResetPosition(uint16_t pc, uint16_t address, uint8_t write, uint8_t val, uint32_t nCyclesLeft) -> uint8_t {
  return Joy_IO_ResetPosition(active_joystick_instance, pc, address, write, val, nCyclesLeft);
}

void JoySetRawPosition(int joy, int x, int y) {
  if (active_joystick_instance && joy >= 0 && joy < 2) {
    active_joystick_instance->xpos[static_cast<size_t>(joy)] = x;
    active_joystick_instance->ypos[static_cast<size_t>(joy)] = y;
  }
}

void JoySetRawButton(int button_idx, bool down) {
  if (active_joystick_instance && button_idx >= 0 && button_idx < 3) {
    if (down && !active_joystick_instance->joybutton[static_cast<size_t>(button_idx)]) {
      active_joystick_instance->buttonlatch[static_cast<size_t>(button_idx)] = BUTTONTIME;
    }
    active_joystick_instance->joybutton[static_cast<size_t>(button_idx)] = down;
  }
}

void JoyUpdatePosition(uint32_t dwExecutedCycles) {
  (void)dwExecutedCycles;
  if (!active_joystick_instance) return;
  for (uint32_t& i : active_joystick_instance->buttonlatch) {
    if (i) {
      --i;
    }
  }
}

auto JoyGetSnapshot(SS_IO_Joystick *pSS) -> uint32_t {
  if (active_joystick_instance) {
    pSS->g_nJoyCntrResetCycle = active_joystick_instance->reset_cycle;
  }
  return 0;
}

auto JoySetSnapshot(SS_IO_Joystick *pSS) -> uint32_t {
  if (active_joystick_instance) {
    active_joystick_instance->reset_cycle = pSS->g_nJoyCntrResetCycle;
  }
  return 0;
}

void JoySetButton(eBUTTON number, eBUTTONSTATE down) {
  JoySetRawButton(static_cast<int>(number), down == BUTTON_DOWN);
}

void JoySetPosition(int xvalue, int xrange, int yvalue, int yrange) {
  if (xrange == 0 || yrange == 0) return;
  JoySetRawPosition(0, (xvalue * 255) / xrange, (yvalue * 255) / yrange);
}

auto JoySetEmulationType(uint32_t newType, int nJoystickNumber) -> bool {
  if (active_joystick_instance && nJoystickNumber >= 0 && nJoystickNumber < 2) {
    active_joystick_instance->joytype[static_cast<size_t>(nJoystickNumber)] = newType;
    return true;
  }
  return false;
}

auto JoyUsingMouse() -> bool {
  if (!active_joystick_instance) return false;
  return (joyinfo[static_cast<size_t>(active_joystick_instance->joytype[0])].device == DEVICE_MOUSE) ||
         (joyinfo[static_cast<size_t>(active_joystick_instance->joytype[1])].device == DEVICE_MOUSE);
}

void JoySetTrim(short nValue, bool bAxisX) {
  if (active_joystick_instance) {
    if (bAxisX) {
      active_joystick_instance->trim_x = nValue;
    } else {
      active_joystick_instance->trim_y = nValue;
    }
  }
}

auto JoyGetTrim(bool bAxisX) -> short {
  if (active_joystick_instance) {
    return bAxisX ? active_joystick_instance->trim_x : active_joystick_instance->trim_y;
  }
  return 0;
}

// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables)
