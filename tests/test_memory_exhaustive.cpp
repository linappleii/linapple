// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include <array>
#include <cstdint>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "core/LinAppleCore.h"
#include "doctest.h"

namespace {

struct MachineHarness_t {
  MachineHarness_t() {
    g_apple2_type = A2TYPE_APPLE2EENHANCED;
    mem_initialize();
    cpu_initialize();
  }

  ~MachineHarness_t() {
    cpu_destroy();
    mem_destroy();
  }

  MachineHarness_t(const MachineHarness_t&) = delete;
  auto operator=(const MachineHarness_t&) -> MachineHarness_t& = delete;
  MachineHarness_t(MachineHarness_t&&) = delete;
  auto operator=(MachineHarness_t&&) -> MachineHarness_t& = delete;

  auto bus_read(uint16_t addr) const -> uint8_t {
    if ((addr & IO_REGION_MASK) == IO_REGION_START) {
      return io_map_dispatch(0x1000, addr, 0, 0, 0);
    }
    return *(mem + addr);
  }

  auto bus_write(uint16_t addr, uint8_t val) const -> void {
    memdirty[addr >> 8] = 0xFF;
    uint8_t* page = memwrite[addr >> 8];
    if (page != nullptr) {
      *(page + (addr & 0xFF)) = val;
    } else if ((addr & IO_REGION_MASK) == IO_REGION_START) {
      io_map_dispatch(0x1000, addr, 1, val, 0);
    }
  }

  auto access_softswitch(uint16_t sw) const -> void {
    io_map_dispatch(0x1000, sw, 0, 0, 0);
  }
};

}  // namespace

TEST_CASE("Exhaustive: [MEM-EX-01] Language Card Softswitch Sequence Matrix") {
  MachineHarness_t harness;

  const std::array<uint16_t, 16> lc_switches = {
      0xC080, 0xC081, 0xC082, 0xC083, 0xC084, 0xC085, 0xC086, 0xC087,
      0xC088, 0xC089, 0xC08A, 0xC08B, 0xC08C, 0xC08D, 0xC08E, 0xC08F};

  // Sample the reset ROM byte at $D000
  const uint8_t rom_byte = harness.bus_read(0xD000);
  const uint8_t test_byte_bank2 = (rom_byte == 0x42) ? 0x43 : 0x42;
  const uint8_t test_byte_bank1 = (rom_byte == 0x84) ? 0x85 : 0x84;

  // Initialize distinct RAM bytes in Bank 2 and Bank 1 at $D000
  harness.access_softswitch(0xC083);  // Bank 2 RAM read & write
  harness.bus_write(0xD000, test_byte_bank2);

  harness.access_softswitch(0xC08B);  // Bank 1 RAM read & write
  harness.bus_write(0xD000, test_byte_bank1);

  // Test every pair of softswitch accesses (256 pairs)
  for (uint16_t s1 : lc_switches) {
    for (uint16_t s2 : lc_switches) {
      mem_reset_paging();

      // Step 1: Read switch 1
      harness.access_softswitch(s1);

      // Step 2: Read switch 2
      harness.access_softswitch(s2);

      // Invariant checks on memory page tables:
      // Base RAM (0x00..0xBF) must always have non-null memwrite pointers
      for (uint16_t page = 0; page < PAGE_C0; ++page) {
        REQUIRE(memwrite[page] != nullptr);
      }

      // I/O space (0xC0..0xCF) must never have direct memwrite pointers (I/O
      // routed)
      for (uint16_t page = PAGE_C0; page < PAGE_D0; ++page) {
        CHECK(memwrite[page] == nullptr);
      }

      // Check Language Card state invariants for switch s2:
      // Bit 3 = 0 -> Bank 2 ($D000-$DFFF is 4K Bank 2)
      // Bit 3 = 1 -> Bank 1 ($D000-$DFFF is 4K Bank 1)
      bool bank2_selected = ((s2 & 0x08) == 0);
      uint32_t mem_mode = get_mem_mode();
      if (bank2_selected) {
        CHECK((mem_mode & MF_HRAM_BANK2) != 0);
      } else {
        CHECK((mem_mode & MF_HRAM_BANK2) == 0);
      }

      // Check Read RAM vs ROM in LinApple (bit 1 == bit 0):
      bool read_ram = (((s2 & 2) >> 1) == (s2 & 1));
      if (read_ram) {
        CHECK((mem_mode & MF_HIGHRAM) != 0);
      } else {
        CHECK((mem_mode & MF_HIGHRAM) == 0);
      }

      // Check Write Enable:
      bool write_ram = (s2 & 0x01) != 0;
      if (write_ram) {
        CHECK((mem_mode & MF_HRAM_WRITE) != 0);
        for (uint16_t page = PAGE_D0; page < PAGE_MAX; ++page) {
          CHECK(memwrite[page] != nullptr);
        }
      } else {
        CHECK((mem_mode & MF_HRAM_WRITE) == 0);
        for (uint16_t page = PAGE_D0; page < PAGE_MAX; ++page) {
          CHECK(memwrite[page] == nullptr);
        }
      }

      // Observable bus read assertions for $D000
      const uint8_t observable_val = harness.bus_read(0xD000);
      if (read_ram) {
        if (bank2_selected) {
          CHECK(observable_val == test_byte_bank2);
        } else {
          CHECK(observable_val == test_byte_bank1);
        }
      } else {
        CHECK(observable_val == rom_byte);
      }

      // Observable write protection assertion
      if (!write_ram) {
        harness.bus_write(0xD000, 0xEE);
        const uint8_t val_after_protected_write = harness.bus_read(0xD000);
        if (read_ram) {
          CHECK(val_after_protected_write ==
                (bank2_selected ? test_byte_bank2 : test_byte_bank1));
        } else {
          CHECK(val_after_protected_write == rom_byte);
        }
      }
    }
  }
}

TEST_CASE("Exhaustive: [MEM-EX-02] 4-Step Language Card Transition Paths") {
  MachineHarness_t harness;

  const std::array<uint16_t, 8> lc_switches = {0xC080, 0xC081, 0xC082, 0xC083,
                                               0xC088, 0xC089, 0xC08A, 0xC08B};

  // Test 8^4 = 4,096 continuous 4-step softswitch transition paths
  for (uint16_t s1 : lc_switches) {
    for (uint16_t s2 : lc_switches) {
      for (uint16_t s3 : lc_switches) {
        for (uint16_t s4 : lc_switches) {
          harness.access_softswitch(s1);
          harness.access_softswitch(s2);
          harness.access_softswitch(s3);
          harness.access_softswitch(s4);

          // Verify memory integrity for base RAM
          for (uint16_t page = 0; page < PAGE_C0; ++page) {
            REQUIRE(memwrite[page] != nullptr);
          }

          // I/O space must remain nullptr for direct writes
          for (uint16_t page = PAGE_C0; page < PAGE_D0; ++page) {
            CHECK(memwrite[page] == nullptr);
          }
        }
      }
    }
  }
}

TEST_CASE("Exhaustive: [MEM-EX-03] Auxiliary Memory 64-State Routing Matrix") {
  MachineHarness_t harness;

  // 6 binary switches:
  // 80STORE (C000/C001), RAMRD (C002/C003), RAMWRT (C004/C005),
  // ALTZP (C008/C009), PAGE2 (C054/C055), HIRES (C056/C057)
  for (int s_80store = 0; s_80store < 2; ++s_80store) {
    for (int s_ramrd = 0; s_ramrd < 2; ++s_ramrd) {
      for (int s_ramwrt = 0; s_ramwrt < 2; ++s_ramwrt) {
        for (int s_altzp = 0; s_altzp < 2; ++s_altzp) {
          for (int s_page2 = 0; s_page2 < 2; ++s_page2) {
            for (int s_hires = 0; s_hires < 2; ++s_hires) {
              // Apply switch states via mem_set_paging
              mem_set_paging(0, s_80store ? 0xC001 : 0xC000, 1, 0, 0);
              mem_set_paging(0, s_ramrd ? 0xC003 : 0xC002, 1, 0, 0);
              mem_set_paging(0, s_ramwrt ? 0xC005 : 0xC004, 1, 0, 0);
              mem_set_paging(0, s_altzp ? 0xC009 : 0xC008, 1, 0, 0);
              mem_set_paging(0, s_page2 ? 0xC055 : 0xC054, 0, 0, 0);
              mem_set_paging(0, s_hires ? 0xC057 : 0xC056, 0, 0, 0);

              // Invariant: Verify base RAM pages have valid non-null page table
              // pointers
              for (uint16_t p = 0; p < PAGE_C0; ++p) {
                REQUIRE(memwrite[p] != nullptr);
              }

              // Verify mode flags reflect switches
              uint32_t mode = get_mem_mode();
              CHECK(((mode & MF_80STORE) != 0) == (s_80store != 0));
              CHECK(((mode & MF_AUXREAD) != 0) == (s_ramrd != 0));
              CHECK(((mode & MF_AUXWRITE) != 0) == (s_ramwrt != 0));
              CHECK(((mode & MF_ALTZP) != 0) == (s_altzp != 0));
              CHECK(((mode & MF_PAGE2) != 0) == (s_page2 != 0));
              CHECK(((mode & MF_HIRES) != 0) == (s_hires != 0));
            }
          }
        }
      }
    }
  }
}

TEST_CASE(
    "Exhaustive: [MEM-EX-04] Language Card Observable Banking and Write "
    "Protection") {
  MachineHarness_t harness;

  const uint8_t rom_byte = harness.bus_read(0xD000);
  const uint8_t test_byte_bank2 = (rom_byte == 0x42) ? 0x43 : 0x42;
  const uint8_t test_byte_bank1 = (rom_byte == 0x84) ? 0x85 : 0x84;
  const uint8_t overwrite_attempt = 0xAA;

  // 1. Configure Bank 2: write enable, read RAM ($C083)
  harness.access_softswitch(0xC083);
  harness.bus_write(0xD000, test_byte_bank2);
  CHECK(harness.bus_read(0xD000) == test_byte_bank2);

  // 2. Configure Bank 1: write enable, read RAM ($C08B)
  harness.access_softswitch(0xC08B);
  harness.bus_write(0xD000, test_byte_bank1);
  CHECK(harness.bus_read(0xD000) == test_byte_bank1);

  // 3. Switch back to Bank 2: read RAM, write protect ($C080)
  harness.access_softswitch(0xC080);
  CHECK(harness.bus_read(0xD000) == test_byte_bank2);

  // 4. Attempt write to Bank 2 while write-protected
  harness.bus_write(0xD000, overwrite_attempt);
  CHECK(harness.bus_read(0xD000) == test_byte_bank2);

  // 5. Switch to Bank 1: read RAM, write protect ($C088)
  harness.access_softswitch(0xC088);
  CHECK(harness.bus_read(0xD000) == test_byte_bank1);

  // 6. Attempt write to Bank 1 while write-protected
  harness.bus_write(0xD000, overwrite_attempt);
  CHECK(harness.bus_read(0xD000) == test_byte_bank1);

  // 7. Switch to Bank 2 ROM read: ($C082)
  harness.access_softswitch(0xC082);
  CHECK(harness.bus_read(0xD000) == rom_byte);

  // 8. Switch to Bank 1 ROM read: ($C08A)
  harness.access_softswitch(0xC08A);
  CHECK(harness.bus_read(0xD000) == rom_byte);

  // 9. Verify Bank 2 RAM still intact after ROM access
  harness.access_softswitch(0xC080);
  CHECK(harness.bus_read(0xD000) == test_byte_bank2);

  // 10. Verify Bank 1 RAM still intact after ROM access
  harness.access_softswitch(0xC088);
  CHECK(harness.bus_read(0xD000) == test_byte_bank1);

  // 11. Test write-enable with ROM read ($C081 for Bank 2, $C089 for Bank 1)
  const uint8_t new_bank2_byte = 0x33;
  harness.access_softswitch(0xC081);
  CHECK(harness.bus_read(0xD000) == rom_byte);
  harness.bus_write(0xD000, new_bank2_byte);
  CHECK(harness.bus_read(0xD000) == rom_byte);  // ROM remains visible

  // Read Bank 2 RAM to confirm write succeeded
  harness.access_softswitch(0xC080);
  CHECK(harness.bus_read(0xD000) == new_bank2_byte);

  const uint8_t new_bank1_byte = 0x77;
  harness.access_softswitch(0xC089);
  CHECK(harness.bus_read(0xD000) == rom_byte);
  harness.bus_write(0xD000, new_bank1_byte);
  CHECK(harness.bus_read(0xD000) == rom_byte);  // ROM remains visible

  // Read Bank 1 RAM to confirm write succeeded
  harness.access_softswitch(0xC088);
  CHECK(harness.bus_read(0xD000) == new_bank1_byte);
}

// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
