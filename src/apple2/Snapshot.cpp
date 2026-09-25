// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/Snapshot.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"

namespace {

struct SlotRegionDesc_t {
  size_t offset;
  size_t size;
  const char* name;
};

// Fixed-body snapshot regions for slots 0 through 7.
constexpr std::array<SlotRegionDesc_t, NUM_SLOTS> slot_region_descriptors{{
    {offsetof(Snapshot_t, apple2_unit.speaker),
     sizeof(Snapshot_t::apple2_unit.speaker), "Speaker"},
    {offsetof(Snapshot_t, empty1), sizeof(SsCardEmpty_t), nullptr},
    {offsetof(Snapshot_t, apple2_unit.comms), sizeof(SsIoComms_t), nullptr},
    {offsetof(Snapshot_t, empty3), sizeof(SsCardEmpty_t), nullptr},
    {offsetof(Snapshot_t, mockingboard1), sizeof(SsCardMockingboard_t),
     nullptr},
    {offsetof(Snapshot_t, mockingboard2), sizeof(SsCardMockingboard_t),
     nullptr},
    {0, 0, nullptr},
    {offsetof(Snapshot_t, empty7), sizeof(SsCardEmpty_t), nullptr},
}};

auto fixed_slot_desc(int slot) -> const SlotRegionDesc_t* {
  if (slot < 0 || slot >= NUM_SLOTS) {
    return nullptr;
  }
  const auto& desc = slot_region_descriptors[static_cast<size_t>(slot)];
  if (desc.size == 0) {
    return nullptr;
  }
  return &desc;
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
  size_t joystick_size = sizeof(snapshot->apple2_unit.joystick);
  peripheral_save_state_by_name(0, "Joystick", &snapshot->apple2_unit.joystick,
                                &joystick_size);
  video_get_snapshot(&snapshot->apple2_unit.video);
  mem_get_snapshot(&snapshot->apple2_unit.memory);

  size_t kbd_size = sizeof(snapshot->apple2_unit.keyboard);
  peripheral_save_state_by_name(0, "Keyboard", &snapshot->apple2_unit.keyboard,
                                &kbd_size);

  for (int i = 0; i < NUM_SLOTS; ++i) {
    const SlotRegionDesc_t* desc = fixed_slot_desc(i);
    if (desc == nullptr) {
      continue;
    }
    auto* state = reinterpret_cast<uint8_t*>(snapshot) + desc->offset;
    size_t size = desc->size;
    if (desc->name != nullptr) {
      peripheral_save_state_by_name(i, desc->name, state, &size);
    } else {
      peripheral_save_state(i, state, &size);
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

  if (!is_apple2()) {
    mem_reset_paging();
  }

  peripheral_manager_reset();
  video_reset_state();

  cpu_set_snapshot(&snapshot->apple2_unit.cpu_6502);
  peripheral_load_state_by_name(0, "Joystick", &snapshot->apple2_unit.joystick,
                                sizeof(snapshot->apple2_unit.joystick));
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
    const SlotRegionDesc_t* desc = fixed_slot_desc(i);
    if (desc == nullptr) {
      continue;
    }
    const auto* state =
        reinterpret_cast<const uint8_t*>(snapshot) + desc->offset;
    if (desc->name != nullptr) {
      peripheral_load_state_by_name(i, desc->name, state, desc->size);
    } else {
      peripheral_load_state(i, state, desc->size);
    }
  }

  return true;
}
