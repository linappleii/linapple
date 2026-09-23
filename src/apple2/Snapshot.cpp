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

// The region AppleWin's layout reserved for a slot in the fixed body. Slot 0
// is three motherboard devices; the speaker's is the one addressed by name.
auto fixed_slot_region(ApplewinSnapshot_t* snapshot, int slot)
    -> FixedSlotRegion_t {
  // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
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

// The Disk II answers save_state with its whole controller and drive state,
// which the snapshot layer deliberately does not carry: a disk's state is
// its mounted image. Probing slot 6 like the others would start saving it.
constexpr int skipped_slot = 6;

auto trailer_entry(ApplewinSnapshot_t* snapshot, int slot) -> SsSlotState_t* {
  if (slot < 1 || slot > static_cast<int>(snapshot_trailer_slots) ||
      slot == skipped_slot) {
    return nullptr;
  }
  return &snapshot->slot_trailer.slots[slot - 1];
}

// Two calls, as the ABI has it: the first, with no buffer, asks the card how
// much it needs, so the blob is exactly the card's frame and load_state gets
// back the size it wrote rather than the slot's capacity.
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

auto trailer_is_sane(ApplewinSnapshot_t* snapshot) -> bool {
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

auto snapshot_serialize(ApplewinSnapshot_t* snapshot) -> void {
  if (!snapshot) return;

  *snapshot = ApplewinSnapshot_t{};

  snapshot->hdr.tag = aw_ss_tag;
  snapshot->hdr.version = snapshot_version;
  // Checksum is initialized to 0 here; exact payload checksum is verified by
  // file manager
  snapshot->hdr.checksum = 0;

  snapshot->apple2_unit.unit_hdr.length = sizeof(SsApple2Unit_t);
  snapshot->apple2_unit.unit_hdr.version = make_version(1, 0, 0, 0);

  peripheral_get_manifest(&snapshot->manifest);

  cpu_get_snapshot(&snapshot->apple2_unit.cpu_6502);
  {
    // Slot 0 holds three peripherals, so the joystick is asked for by name.
    // Its region is 8 bytes and the card's frame is 56, so the card refuses
    // the buffer and the region stays zero until the layout grows.
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

auto snapshot_deserialize(ApplewinSnapshot_t* snapshot) -> bool {
  if (!snapshot) return false;

  if (!peripheral_verify_manifest(&snapshot->manifest)) {
    return false;
  }

  // A file without a trailer reaches here with the trailer all zeros, which
  // every slot reads as "nothing carried".
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
    // A slot the trailer carries is loaded from it alone; the fixed body's
    // region for that slot is the one a fixed-body file has and stays the
    // fallback for a slot the trailer left empty.
    const SsSlotState_t* entry = trailer_entry(snapshot, i);
    if (entry != nullptr && entry->length > 0) {
      peripheral_load_state(i, entry->data, entry->length);
      continue;
    }
    FixedSlotRegion_t region = fixed_slot_region(snapshot, i);
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
