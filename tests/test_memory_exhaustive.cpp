// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#include <cstdint>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "core/LinAppleCore.h"
#include "doctest.h"

namespace {

void init_test_machine() {
  g_apple2_type = A2TYPE_APPLE2EENHANCED;
  mem_initialize();
  cpu_initialize();
}

}  // namespace

TEST_CASE("Exhaustive: [MEM-EX-01] Language Card Softswitch Sequence Matrix") {
  init_test_machine();

  const uint16_t lc_switches[16] = {
      0xC080, 0xC081, 0xC082, 0xC083, 0xC084, 0xC085, 0xC086, 0xC087,
      0xC088, 0xC089, 0xC08A, 0xC08B, 0xC08C, 0xC08D, 0xC08E, 0xC08F};

  // Test every pair of softswitch accesses (256 pairs)
  for (uint16_t s1 : lc_switches) {
    for (uint16_t s2 : lc_switches) {
      mem_reset_paging();

      // Step 1: Read switch 1
      io_map_dispatch(0x1000, s1, 0, 0, 0);

      // Step 2: Read switch 2
      io_map_dispatch(0x1000, s2, 0, 0, 0);

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
    }
  }
}

TEST_CASE("Exhaustive: [MEM-EX-02] 4-Step Language Card Transition Paths") {
  init_test_machine();

  const uint16_t lc_switches[8] = {0xC080, 0xC081, 0xC082, 0xC083,
                                   0xC088, 0xC089, 0xC08A, 0xC08B};

  // Test 8^4 = 4,096 continuous 4-step softswitch transition paths
  for (uint16_t s1 : lc_switches) {
    for (uint16_t s2 : lc_switches) {
      for (uint16_t s3 : lc_switches) {
        for (uint16_t s4 : lc_switches) {
          io_map_dispatch(0x1000, s1, 0, 0, 0);
          io_map_dispatch(0x1000, s2, 0, 0, 0);
          io_map_dispatch(0x1000, s3, 0, 0, 0);
          io_map_dispatch(0x1000, s4, 0, 0, 0);

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
  init_test_machine();

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
// NOLINTEND(bugprone-easily-swappable-parameters,
// modernize-use-trailing-return-type, cppcoreguidelines-owning-memory,
// cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays,
// modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay)
