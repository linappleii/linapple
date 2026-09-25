// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/Snapshot.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"

namespace {

struct FixedSlotRegion_t {
  void* state;
  size_t size;
  const char* name;
};

struct ConstFixedSlotRegion_t {
  const void* state;
  size_t size;
  const char* name;
};

// Motherboard speaker region.
auto fixed_slot_region(Snapshot_t* snapshot, int slot) -> FixedSlotRegion_t {
  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
  // Justification: the case labels are the Apple II's own slot numbers.
  switch (slot) {
    case 0:
      return {&snapshot->apple2_unit.speaker,
              sizeof(snapshot->apple2_unit.speaker), "Speaker"};
    case 1:
      return {&snapshot->empty1, sizeof(snapshot->empty1), nullptr};
    case 2:
      return {&snapshot->apple2_unit.comms, sizeof(snapshot->apple2_unit.comms),
              nullptr};
    case 3:
      return {&snapshot->empty3, sizeof(snapshot->empty3), nullptr};
    case 4:
      return {&snapshot->mockingboard1, sizeof(snapshot->mockingboard1),
              nullptr};
    case 5:
      return {&snapshot->mockingboard2, sizeof(snapshot->mockingboard2),
              nullptr};
    case 7:
      return {&snapshot->empty7, sizeof(snapshot->empty7), nullptr};
    default:
      return {nullptr, 0, nullptr};
  }
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
}

auto fixed_slot_region_const(const Snapshot_t* snapshot, int slot)
    -> ConstFixedSlotRegion_t {
  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
  // Justification: the case labels are the Apple II's own slot numbers.
  switch (slot) {
    case 0:
      return {&snapshot->apple2_unit.speaker,
              sizeof(snapshot->apple2_unit.speaker), "Speaker"};
    case 1:
      return {&snapshot->empty1, sizeof(snapshot->empty1), nullptr};
    case 2:
      return {&snapshot->apple2_unit.comms, sizeof(snapshot->apple2_unit.comms),
              nullptr};
    case 3:
      return {&snapshot->empty3, sizeof(snapshot->empty3), nullptr};
    case 4:
      return {&snapshot->mockingboard1, sizeof(snapshot->mockingboard1),
              nullptr};
    case 5:
      return {&snapshot->mockingboard2, sizeof(snapshot->mockingboard2),
              nullptr};
    case 7:
      return {&snapshot->empty7, sizeof(snapshot->empty7), nullptr};
    default:
      return {nullptr, 0, nullptr};
  }
  // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
}

// Disk II persists state to mounted image; omitted from snapshot trailer.
constexpr int skipped_slot = 6;

auto trailer_entry(Snapshot_t* snapshot, int slot) -> SsSlotState_t* {
  if (slot < 1 || slot > static_cast<int>(snapshot_trailer_slots) ||
      slot == skipped_slot) {
    return nullptr;
  }
  return &snapshot->slot_trailer.slots[slot - 1];
}

auto trailer_entry(const Snapshot_t* snapshot, int slot)
    -> const SsSlotState_t* {
  if (slot < 1 || slot > static_cast<int>(snapshot_trailer_slots) ||
      slot == skipped_slot) {
    return nullptr;
  }
  return &snapshot->slot_trailer.slots[slot - 1];
}

// Query required buffer size before allocating frame.
auto save_slot_to_trailer(int slot, SsSlotState_t* entry) -> void {
  entry->length = 0;
  size_t needed = 0;
  peripheral_save_state(slot, nullptr, &needed);
  if (needed == 0) {
    return;
  }
  if (needed > snapshot_slot_state_capacity) {
    Logger::warning(
        "Slot %d state of %zu bytes exceeds the %u-byte snapshot slot; not "
        "saved\n",
        slot, needed, snapshot_slot_state_capacity);
    return;
  }
  size_t size = needed;
  peripheral_save_state(slot, entry->data, &size);
  entry->length = static_cast<uint32_t>(needed);
}

auto trailer_is_sane(const Snapshot_t* snapshot) -> bool {
  for (int slot = 1; slot <= static_cast<int>(snapshot_trailer_slots); ++slot) {
    const SsSlotState_t* entry = trailer_entry(snapshot, slot);
    if (entry != nullptr && entry->length > snapshot_slot_state_capacity) {
      Logger::error(
          "Snapshot slot %d claims %u bytes, more than a slot holds\n", slot,
          entry->length);
      return false;
    }
  }
  return true;
}

}  // namespace

auto snapshot_serialize(Snapshot_t* snapshot) -> void {
  if (!snapshot) return;

  *snapshot = Snapshot_t{};

  snapshot->hdr.tag = snapshot_file_tag;
  snapshot->hdr.version = snapshot_version;
  // Checksum is initialized to 0 here; exact payload checksum is verified by
  // file manager
  snapshot->hdr.checksum = 0;

  snapshot->apple2_unit.unit_hdr.length = sizeof(SsApple2Unit_t);
  snapshot->apple2_unit.unit_hdr.version = make_version(1, 0, 0, 0);

  peripheral_get_manifest(&snapshot->manifest);

  cpu_get_snapshot(&snapshot->apple2_unit.cpu_6502);
  {
    // Slot 0 contains multiple devices; query joystick by name.
    size_t size = sizeof(snapshot->apple2_unit.joystick);
    peripheral_save_state_by_name(0, "Joystick",
                                  &snapshot->apple2_unit.joystick, &size);
  }
  video_get_snapshot(&snapshot->apple2_unit.video);
  mem_get_snapshot(&snapshot->apple2_unit.memory);

  size_t kbd_size = sizeof(snapshot->apple2_unit.keyboard);
  peripheral_save_state_by_name(0, "Keyboard", &snapshot->apple2_unit.keyboard,
                                &kbd_size);

  for (int i = 0; i < NUM_SLOTS; ++i) {
    FixedSlotRegion_t region = fixed_slot_region(snapshot, i);
    if (region.state == nullptr) {
      continue;
    }
    if (region.name != nullptr) {
      peripheral_save_state_by_name(i, region.name, region.state, &region.size);
    } else {
      peripheral_save_state(i, region.state, &region.size);
    }
  }

  snapshot->slot_trailer.unit_hdr.length = sizeof(SsSlotTrailer_t);
  snapshot->slot_trailer.unit_hdr.version = make_version(1, 0, 0, 0);
  for (int i = 0; i < NUM_SLOTS; ++i) {
    SsSlotState_t* entry = trailer_entry(snapshot, i);
    if (entry != nullptr) {
      save_slot_to_trailer(i, entry);
    }
  }
}

auto snapshot_deserialize(const Snapshot_t* snapshot) -> bool {
  if (!snapshot) return false;

  if (!peripheral_verify_manifest(&snapshot->manifest)) {
    return false;
  }

  if (!trailer_is_sane(snapshot)) {
    return false;
  }

  mem_reset();

  if (!IS_APPLE2()) {
    mem_reset_paging();
  }

  peripheral_manager_reset();
  video_reset_state();

  cpu_set_snapshot(&snapshot->apple2_unit.cpu_6502);
  {
    size_t size = sizeof(snapshot->apple2_unit.joystick);
    peripheral_load_state_by_name(0, "Joystick",
                                  &snapshot->apple2_unit.joystick, size);
  }
  peripheral_load_state_by_name(0, "Keyboard", &snapshot->apple2_unit.keyboard,
                                sizeof(snapshot->apple2_unit.keyboard));
  video_set_snapshot(&snapshot->apple2_unit.video);
  mem_set_snapshot(&snapshot->apple2_unit.memory);

  for (int i = 0; i < NUM_SLOTS; ++i) {
    // Fall back to fixed body if slot trailer is empty.
    const SsSlotState_t* entry = trailer_entry(snapshot, i);
    if (entry != nullptr && entry->length > 0) {
      peripheral_load_state(i, entry->data, entry->length);
      continue;
    }
    ConstFixedSlotRegion_t region = fixed_slot_region_const(snapshot, i);
    if (region.state == nullptr) {
      continue;
    }
    if (region.name != nullptr) {
      peripheral_load_state_by_name(i, region.name, region.state, region.size);
    } else {
      peripheral_load_state(i, region.state, region.size);
    }
  }

  return true;
}
