// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/Snapshot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/Video.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Internal.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"

namespace {

struct SlotRegionDesc {
  size_t offset;
  size_t size;
  const char* name;
};

// Fixed-body snapshot regions for slots 0 through 7, sized as the AppleWin
// layout has them; a card whose frame is larger refuses the region and rides
// the slot trailer.
constexpr std::array<SlotRegionDesc, num_slots> slot_region_descriptors{
    {
        {
            offsetof(Snapshot_t, apple2_unit.speaker),
            sizeof(Snapshot_t::apple2_unit.speaker),
            "Speaker",
        },
        {offsetof(Snapshot_t, empty1), sizeof(SsCardEmpty_t), nullptr},
        {offsetof(Snapshot_t, apple2_unit.comms), sizeof(SsIoComms_t), nullptr},
        {offsetof(Snapshot_t, empty3), sizeof(SsCardEmpty_t), nullptr},
        {
            offsetof(Snapshot_t, mockingboard1),
            sizeof(SsCardMockingboard_t),
            nullptr,
        },
        {
            offsetof(Snapshot_t, mockingboard2),
            sizeof(SsCardMockingboard_t),
            nullptr,
        },
        {0, 0, nullptr},
        {offsetof(Snapshot_t, empty7), sizeof(SsCardEmpty_t), nullptr},
    },
};

auto fixed_slot_desc(int slot) noexcept -> const SlotRegionDesc* {
  if (slot < 0 || slot >= num_slots) {
    return nullptr;
  }
  const auto& desc = slot_region_descriptors[static_cast<size_t>(slot)];
  if (desc.size == 0) {
    return nullptr;
  }
  return &desc;
}

// Disk II persists state to mounted image; omitted from snapshot trailer.
constexpr int k_skipped_slot = 6;

auto trailer_entry(Snapshot_t* snapshot, int slot) noexcept -> SsSlotState_t* {
  if (snapshot == nullptr || slot < 1 ||
      slot > static_cast<int>(snapshot_trailer_slots) ||
      slot == k_skipped_slot) {
    return nullptr;
  }
  return &snapshot->slot_trailer.slots[slot - 1];
}

auto trailer_entry(const Snapshot_t* snapshot, int slot) noexcept
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

auto trailer_is_sane(const Snapshot_t* snapshot) noexcept -> bool {
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

// The one change of card a file may ask: the slot the Mouse in slot 4 key took
// over may hold the displaced card instead, or the key's card again after such
// a load. card is null when the slot is to be left empty.
struct LegacySwap_t {
  bool wanted = false;
  int slot = 0;
  Peripheral_t* card = nullptr;
  const char* held = "";
  const char* wanted_name = "";
};

auto name_or_none(const char* name) noexcept -> const char* {
  return name[0] != '\0' ? name : "no card";
}

// Slot 0 holds several internal devices and a manifest names one of them;
// peripheral_verify_manifest's rule for which are accepted decides here too.
auto slot0_matches(const SsPeripheralManifest_t* file,
                   const SsPeripheralManifest_t* live) -> bool {
  SsPeripheralManifest_t probe = *live;
  memcpy(probe.peripherals[0].name, file->peripherals[0].name,
         max_peripheral_name);
  return peripheral_verify_manifest(&probe);
}

// The overridden slot alone may name the other of the two cards the key knows.
// Nothing is changed here; the swap that would make the file match is only
// described.
auto manifest_admits(const SsPeripheralManifest_t* file, LegacySwap_t* swap)
    -> bool {
  SsPeripheralManifest_t live{};
  peripheral_get_manifest(&live);
  int override_slot = 0;
  const char* key_card = "";
  const char* displaced = "";
  const bool overrode =
      peripheral_legacy_override(&override_slot, &key_card, &displaced);

  for (size_t i = 0; i < num_slots; ++i) {
    const char* wanted = file->peripherals[i].name;
    const char* held = live.peripherals[i].name;
    const bool same =
        (i == 0) ? slot0_matches(file, &live) : strcmp(wanted, held) == 0;
    if (same) {
      continue;
    }
    if (overrode && static_cast<int>(i) == override_slot) {
      // The swap outlives live, so it keeps the key's own two names, which are
      // the descriptors', and never a pointer into the manifest copied here.
      const char* other = nullptr;
      const char* holding = nullptr;
      if (strcmp(held, key_card) == 0 && strcmp(wanted, displaced) == 0) {
        other = displaced;
        holding = key_card;
      } else if (strcmp(held, displaced) == 0 &&
                 strcmp(wanted, key_card) == 0) {
        other = key_card;
        holding = displaced;
      }
      if (other != nullptr) {
        Peripheral_t* card =
            other[0] != '\0' ? peripheral_find_internal(other) : nullptr;
        if (other[0] != '\0' && card == nullptr) {
          Logger::info(
              "Slot %zu: the save state names %s, which is not built\n", i,
              other);
          return false;
        }
        swap->wanted = true;
        swap->slot = static_cast<int>(i);
        swap->card = card;
        swap->held = holding;
        swap->wanted_name = other;
        continue;
      }
    }
    Logger::info(
        "Slot %zu: the save state names %s where the machine holds %s; the "
        "file is refused\n",
        i, name_or_none(wanted), name_or_none(held));
    return false;
  }
  return true;
}

auto apply_legacy_swap(const LegacySwap_t& swap) -> bool {
  if (!swap.wanted) {
    return true;
  }
  peripheral_unregister(swap.slot);
  if (swap.card != nullptr && peripheral_register(swap.card, swap.slot) != 0) {
    Logger::error("Slot %d: %s could not be installed for the save state\n",
                  swap.slot, swap.wanted_name);
    return false;
  }
  Logger::info(
      "Slot %d: the save state holds %s where Mouse in slot 4 left %s; "
      "loading with %s for this session\n",
      swap.slot, name_or_none(swap.wanted_name), name_or_none(swap.held),
      name_or_none(swap.wanted_name));
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
  static_cast<void>(video_get_snapshot(&snapshot->apple2_unit.video));
  static_cast<void>(mem_get_snapshot(&snapshot->apple2_unit.memory));

  size_t kbd_size = sizeof(snapshot->apple2_unit.keyboard.bytes);
  peripheral_save_state_by_name(
      0, "Keyboard", snapshot->apple2_unit.keyboard.bytes, &kbd_size);

  for (int i = 0; i < num_slots; ++i) {
    const SlotRegionDesc* desc = fixed_slot_desc(i);
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
  for (int i = 0; i < num_slots; ++i) {
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

  // Every check that can refuse the file runs before the machine is touched.
  LegacySwap_t swap;
  if (!manifest_admits(&snapshot->manifest, &swap)) {
    return false;
  }

  if (!trailer_is_sane(snapshot)) {
    return false;
  }

  if (!apply_legacy_swap(swap)) {
    return false;
  }

  if (!peripheral_verify_manifest(&snapshot->manifest)) {
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
  peripheral_load_state_by_name(0, "Keyboard",
                                snapshot->apple2_unit.keyboard.bytes,
                                sizeof(snapshot->apple2_unit.keyboard.bytes));
  if (video_set_snapshot(&snapshot->apple2_unit.video) != 0) {
    return false;
  }
  if (mem_set_snapshot(&snapshot->apple2_unit.memory) != 0) {
    return false;
  }

  for (int i = 0; i < num_slots; ++i) {
    // Fall back to fixed body if slot trailer is empty.
    const SsSlotState_t* entry = trailer_entry(snapshot, i);
    if (entry != nullptr && entry->length > 0) {
      peripheral_load_state(i, entry->data, entry->length);
      continue;
    }
    const SlotRegionDesc* desc = fixed_slot_desc(i);
    if (desc == nullptr) {
      continue;
    }
    const auto* state =
        reinterpret_cast<const uint8_t*>(snapshot) + desc->offset;
    if (desc->name != nullptr) {
      peripheral_load_state_by_name(i, desc->name, state, desc->size);
      continue;
    }
    // A silent refusal would look like a load. The manifest was verified
    // against the active cards above, so its name is the card that refused.
    if (peripheral_load_state(i, state, desc->size) == peripheral_error) {
      Logger::info(
          "Slot %d: %s refused the %zu-byte fixed-body region and stays at "
          "reset\n",
          i, snapshot->manifest.peripherals[i].name, desc->size);
    }
  }

  return true;
}
