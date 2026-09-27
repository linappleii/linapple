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
constexpr std::array<SlotRegionDesc_t, NUM_SLOTS> k_slot_region_descriptors{{
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

[[nodiscard]] auto fixed_slot_desc(int slot) noexcept
    -> const SlotRegionDesc_t* {
  if (slot < 0 || slot >= NUM_SLOTS) {
    return nullptr;
  }
  const auto& desc = k_slot_region_descriptors[static_cast<size_t>(slot)];
  if (desc.size == 0) {
    return nullptr;
  }
  return &desc;
}

// Disk II persists state to mounted image; omitted from snapshot trailer.
constexpr int k_skipped_slot = 6;

[[nodiscard]] auto trailer_entry(Snapshot_t* snapshot, int slot) noexcept
    -> SsSlotState_t* {
  if (snapshot == nullptr || slot < 1 ||
      slot > static_cast<int>(snapshot_trailer_slots) ||
      slot == k_skipped_slot) {
    return nullptr;
  }
  return &snapshot->slot_trailer.slots[slot - 1];
}

[[nodiscard]] auto trailer_entry(const Snapshot_t* snapshot, int slot) noexcept
    -> const SsSlotState_t* {
  if (snapshot == nullptr || slot < 1 ||
      slot > static_cast<int>(snapshot_trailer_slots) ||
      slot == k_skipped_slot) {
    return nullptr;
  }
  return &snapshot->slot_trailer.slots[slot - 1];
}

// Query required buffer size before allocating frame.
auto save_slot_to_trailer(int slot, SsSlotState_t* entry) noexcept -> void {
  if (entry == nullptr) {
    return;
  }
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

[[nodiscard]] auto trailer_is_sane(const Snapshot_t* snapshot) noexcept
    -> bool {
  if (snapshot == nullptr) {
    return false;
  }
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

auto snapshot_serialize(Snapshot_t* snapshot) noexcept -> void {
  if (snapshot == nullptr) {
    return;
  }

  *snapshot = Snapshot_t{};

  snapshot->hdr.tag = snapshot_file_tag;
  snapshot->hdr.version = snapshot_version;
  // Checksum is initialized to 0 here; exact payload checksum is verified by
  // file manager
  snapshot->hdr.checksum = 0;

  snapshot->apple2_unit.unit_hdr.length = sizeof(SsApple2Unit_t);
  snapshot->apple2_unit.unit_hdr.version = make_version(1, 0, 0, 0);

  peripheral_get_manifest(&snapshot->manifest);

  static_cast<void>(cpu_get_snapshot(&snapshot->apple2_unit.cpu_6502));
  size_t joystick_size = sizeof(snapshot->apple2_unit.joystick);
  peripheral_save_state_by_name(0, "Joystick", &snapshot->apple2_unit.joystick,
                                &joystick_size);
  static_cast<void>(video_get_snapshot(&snapshot->apple2_unit.video));
  static_cast<void>(mem_get_snapshot(&snapshot->apple2_unit.memory));

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
  if (snapshot == nullptr) {
    return false;
  }

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

  if (cpu_set_snapshot(&snapshot->apple2_unit.cpu_6502) != 0) {
    return false;
  }
  peripheral_load_state_by_name(0, "Joystick", &snapshot->apple2_unit.joystick,
                                sizeof(snapshot->apple2_unit.joystick));
  peripheral_load_state_by_name(0, "Keyboard", &snapshot->apple2_unit.keyboard,
                                sizeof(snapshot->apple2_unit.keyboard));
  if (video_set_snapshot(&snapshot->apple2_unit.video) != 0) {
    return false;
  }
  if (mem_set_snapshot(&snapshot->apple2_unit.memory) != 0) {
    return false;
  }

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
