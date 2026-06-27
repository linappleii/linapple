// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/Snapshot.h"

#include <cstddef>
#include <cstdint>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Video.h"
#include "apple2/peripherals/joystick/Joystick.h"
#include "apple2/peripherals/mockingboard/Mockingboard.h"
#include "apple2/peripherals/speaker/Speaker.h"
#include "apple2/peripherals/super_serial_card/SuperSerial.h"
#include "core/LinAppleCore.h"

auto snapshot_serialize(APPLEWIN_SNAPSHOT* snapshot) -> void {
  if (!snapshot) return;

  snapshot->Hdr.dwTag = AW_SS_TAG;
  snapshot->Hdr.dwVersion = MAKE_VERSION(1, 0, 0, 1);
  snapshot->Hdr.dwChecksum = 0; // TO DO

  snapshot->Apple2Unit.UnitHdr.dwLength = sizeof(SS_APPLE2_Unit);
  snapshot->Apple2Unit.UnitHdr.dwVersion = MAKE_VERSION(1, 0, 0, 0);

  Peripheral_GetManifest(&snapshot->Manifest);

  CpuGetSnapshot(&snapshot->Apple2Unit.CPU6502);
  {
    size_t size = sizeof(snapshot->Apple2Unit.Joystick);
    Peripheral_SaveState(0, &snapshot->Apple2Unit.Joystick, &size);
  }
  VideoGetSnapshot(&snapshot->Apple2Unit.Video);
  MemGetSnapshot(&snapshot->Apple2Unit.Memory);

  size_t kbd_size = sizeof(snapshot->Apple2Unit.Keyboard);
  Peripheral_SaveStateByName(0, "Keyboard", &snapshot->Apple2Unit.Keyboard, &kbd_size);

  // Slots 0-7
  for (int i = 0; i < 8; ++i) {
    void* slot_state = nullptr;
    size_t slot_size = 0;
    const char* name = nullptr;
    switch (i) {
      case 0:
        name = "Speaker";
        slot_state = &snapshot->Apple2Unit.Speaker;
        slot_size = sizeof(snapshot->Apple2Unit.Speaker);
        break;
      case 1:
        slot_state = &snapshot->Empty1;
        slot_size = sizeof(snapshot->Empty1);
        break;
      case 2:
        slot_state = &snapshot->Apple2Unit.Comms;
        slot_size = sizeof(snapshot->Apple2Unit.Comms);
        break;
      case 3:
        slot_state = &snapshot->Empty3;
        slot_size = sizeof(snapshot->Empty3);
        break;
      case 4:
        slot_state = &snapshot->Mockingboard1;
        slot_size = sizeof(snapshot->Mockingboard1);
        break;
      case 5:
        slot_state = &snapshot->Mockingboard2;
        slot_size = sizeof(snapshot->Mockingboard2);
        break;
      case 6:
        break; // Slot 6 handled via manifest/ABI
      case 7:
        slot_state = &snapshot->Empty7;
        slot_size = sizeof(snapshot->Empty7);
        break;
    }
    if (slot_state) {
      if (name) {
        Peripheral_SaveStateByName(i, name, slot_state, &slot_size);
      } else {
        Peripheral_SaveState(i, slot_state, &slot_size);
      }
    }
  }
}

auto snapshot_deserialize(APPLEWIN_SNAPSHOT* snapshot) -> bool {
  if (!snapshot) return false;

  // Verify peripheral manifest
  if (!Peripheral_VerifyManifest(&snapshot->Manifest)) {
    return false;
  }

  // Reset all sub-systems
  MemReset();

  if (!IS_APPLE2()) {
    MemResetPaging();
  }

  Peripheral_Manager_Reset();
  VideoResetState();

  CpuSetSnapshot(&snapshot->Apple2Unit.CPU6502);
  {
    size_t size = sizeof(snapshot->Apple2Unit.Joystick);
    Peripheral_LoadState(0, &snapshot->Apple2Unit.Joystick, size);
  }
  Peripheral_LoadStateByName(0, "Keyboard", &snapshot->Apple2Unit.Keyboard,
                             sizeof(snapshot->Apple2Unit.Keyboard));
  VideoSetSnapshot(&snapshot->Apple2Unit.Video);
  MemSetSnapshot(&snapshot->Apple2Unit.Memory);

  // Slots 0-7
  for (int i = 0; i < 8; ++i) {
    void* slot_state = nullptr;
    size_t slot_size = 0;
    const char* name = nullptr;
    switch (i) {
      case 0:
        name = "Speaker";
        slot_state = &snapshot->Apple2Unit.Speaker;
        slot_size = sizeof(snapshot->Apple2Unit.Speaker);
        break;
      case 1:
        slot_state = &snapshot->Empty1;
        slot_size = sizeof(snapshot->Empty1);
        break;
      case 2:
        slot_state = &snapshot->Apple2Unit.Comms;
        slot_size = sizeof(snapshot->Apple2Unit.Comms);
        break;
      case 3:
        slot_state = &snapshot->Empty3;
        slot_size = sizeof(snapshot->Empty3);
        break;
      case 4:
        slot_state = &snapshot->Mockingboard1;
        slot_size = sizeof(snapshot->Mockingboard1);
        break;
      case 5:
        slot_state = &snapshot->Mockingboard2;
        slot_size = sizeof(snapshot->Mockingboard2);
        break;
      case 6:
        break; // Slot 6 handled via manifest/ABI
      case 7:
        slot_state = &snapshot->Empty7;
        slot_size = sizeof(snapshot->Empty7);
        break;
    }
    if (slot_state) {
      if (name) {
        Peripheral_LoadStateByName(i, name, slot_state, slot_size);
      } else {
        Peripheral_LoadState(i, slot_state, slot_size);
      }
    }
  }

  return true;
}
