// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/Snapshot.h"

#include <cstddef>
#include <cstring>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/Video.h"
#include "core/Common.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"

auto snapshot_serialize(ApplewinSnapshot_t* snapshot) -> void {
  if (!snapshot) return;

  *snapshot = ApplewinSnapshot_t{};

  snapshot->hdr.tag = aw_ss_tag;
  snapshot->hdr.version = make_version(1, 0, 0, 1);
  // Checksum is initialized to 0 here; exact payload checksum is verified by
  // file manager
  snapshot->hdr.checksum = 0;

  snapshot->apple2_unit.unit_hdr.length = sizeof(SsApple2Unit_t);
  snapshot->apple2_unit.unit_hdr.version = make_version(1, 0, 0, 0);

  Peripheral_GetManifest(&snapshot->manifest);

  CpuGetSnapshot(&snapshot->apple2_unit.cpu_6502);
  {
    size_t size = sizeof(snapshot->apple2_unit.joystick);
    Peripheral_SaveState(0, &snapshot->apple2_unit.joystick, &size);
  }
  VideoGetSnapshot(&snapshot->apple2_unit.video);
  MemGetSnapshot(&snapshot->apple2_unit.memory);

  size_t kbd_size = sizeof(snapshot->apple2_unit.keyboard);
  Peripheral_SaveStateByName(0, "Keyboard", &snapshot->apple2_unit.keyboard,
                             &kbd_size);

  for (int i = 0; i < NUM_SLOTS; ++i) {
    void* slot_state = nullptr;
    size_t slot_size = 0;
    const char* name = nullptr;
    // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers): Hardware expansion
    // slot index constants 0 through 7
    switch (i) {
      case 0:
        name = "Speaker";
        slot_state = &snapshot->apple2_unit.speaker;
        slot_size = sizeof(snapshot->apple2_unit.speaker);
        break;
      case 1:
        slot_state = &snapshot->empty1;
        slot_size = sizeof(snapshot->empty1);
        break;
      case 2:
        slot_state = &snapshot->apple2_unit.comms;
        slot_size = sizeof(snapshot->apple2_unit.comms);
        break;
      case 3:
        slot_state = &snapshot->empty3;
        slot_size = sizeof(snapshot->empty3);
        break;
      case 4:
        slot_state = &snapshot->mockingboard1;
        slot_size = sizeof(snapshot->mockingboard1);
        break;
      case 5:
        slot_state = &snapshot->mockingboard2;
        slot_size = sizeof(snapshot->mockingboard2);
        break;
      case 6:
        break;  // Slot 6 handled via manifest/ABI
      case 7:
        slot_state = &snapshot->empty7;
        slot_size = sizeof(snapshot->empty7);
        break;
      default:
        break;
    }
    // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
    if (slot_state) {
      if (name) {
        Peripheral_SaveStateByName(i, name, slot_state, &slot_size);
      } else {
        Peripheral_SaveState(i, slot_state, &slot_size);
      }
    }
  }
}

auto snapshot_deserialize(ApplewinSnapshot_t* snapshot) -> bool {
  if (!snapshot) return false;

  if (!Peripheral_VerifyManifest(&snapshot->manifest)) {
    return false;
  }

  MemReset();

  if (!IS_APPLE2()) {
    MemResetPaging();
  }

  Peripheral_Manager_Reset();
  VideoResetState();

  CpuSetSnapshot(&snapshot->apple2_unit.cpu_6502);
  {
    size_t size = sizeof(snapshot->apple2_unit.joystick);
    Peripheral_LoadState(0, &snapshot->apple2_unit.joystick, size);
  }
  Peripheral_LoadStateByName(0, "Keyboard", &snapshot->apple2_unit.keyboard,
                             sizeof(snapshot->apple2_unit.keyboard));
  VideoSetSnapshot(&snapshot->apple2_unit.video);
  MemSetSnapshot(&snapshot->apple2_unit.memory);

  for (int i = 0; i < NUM_SLOTS; ++i) {
    void* slot_state = nullptr;
    size_t slot_size = 0;
    const char* name = nullptr;
    // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers): Hardware expansion
    // slot index constants 0 through 7
    switch (i) {
      case 0:
        name = "Speaker";
        slot_state = &snapshot->apple2_unit.speaker;
        slot_size = sizeof(snapshot->apple2_unit.speaker);
        break;
      case 1:
        slot_state = &snapshot->empty1;
        slot_size = sizeof(snapshot->empty1);
        break;
      case 2:
        slot_state = &snapshot->apple2_unit.comms;
        slot_size = sizeof(snapshot->apple2_unit.comms);
        break;
      case 3:
        slot_state = &snapshot->empty3;
        slot_size = sizeof(snapshot->empty3);
        break;
      case 4:
        slot_state = &snapshot->mockingboard1;
        slot_size = sizeof(snapshot->mockingboard1);
        break;
      case 5:
        slot_state = &snapshot->mockingboard2;
        slot_size = sizeof(snapshot->mockingboard2);
        break;
      case 6:
        break;  // Slot 6 handled via manifest/ABI
      case 7:
        slot_state = &snapshot->empty7;
        slot_size = sizeof(snapshot->empty7);
        break;
      default:
        break;
    }
    // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
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
