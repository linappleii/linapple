// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <unistd.h>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Snapshot.h"
#include "apple2/SnapshotTypes.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Internal.h"
#include "frontends/common/SaveStateManager.h"

TEST_CASE("Snapshot: [RoundTrip] Serialize and Deserialize") {
  Linapple_Init();
  Peripheral_Manager_Init();
  Peripheral_Register_Internal();

  uint8_t orig_a = CpuGetRegisters()->a;
  uint8_t orig_x = CpuGetRegisters()->x;
  uint8_t orig_y = CpuGetRegisters()->y;
  uint16_t orig_pc = CpuGetRegisters()->pc;
  uint16_t orig_sp = CpuGetRegisters()->sp;
  uint64_t orig_cycles = CpuGetCumulativeCycles();

  uint32_t orig_mem_mode = MemGetActiveContext()->mem_mode;
  bool orig_last_write_ram = MemGetActiveContext()->last_write_ram;

  uint8_t* mem_2000 = MemGetMainPtr(0x2000);
  uint8_t orig_byte_2000 = *mem_2000;

  CpuGetRegisters()->a = 0x11;
  CpuGetRegisters()->x = 0x22;
  CpuGetRegisters()->y = 0x33;
  CpuGetRegisters()->pc = 0x1000;
  CpuGetRegisters()->sp = 0x1FF;
  g_nCumulativeCycles = 12345;

  MemGetActiveContext()->mem_mode = MF_HRAM_BANK2 | MF_SLOTCXROM | MF_HRAM_WRITE;
  MemGetActiveContext()->last_write_ram = true;
  *mem_2000 = 0x55;

  auto snapshot = std::unique_ptr<APPLEWIN_SNAPSHOT>(new APPLEWIN_SNAPSHOT());
  snapshot_serialize(snapshot.get());

  CpuGetRegisters()->a = 0xFF;
  CpuGetRegisters()->x = 0xFF;
  CpuGetRegisters()->y = 0xFF;
  CpuGetRegisters()->pc = 0x9999;
  CpuGetRegisters()->sp = 0x100;
  g_nCumulativeCycles = 99999;

  MemGetActiveContext()->mem_mode = MF_80STORE | MF_ALTZP;
  MemGetActiveContext()->last_write_ram = false;
  *mem_2000 = 0xAA;

  bool success = snapshot_deserialize(snapshot.get());
  REQUIRE(success);

  CHECK(CpuGetRegisters()->a == 0x11);
  CHECK(CpuGetRegisters()->x == 0x22);
  CHECK(CpuGetRegisters()->y == 0x33);
  CHECK(CpuGetRegisters()->pc == 0x1000);
  CHECK(CpuGetRegisters()->sp == 0x1FF);
  CHECK(CpuGetCumulativeCycles() == 12345);

  CHECK(MemGetActiveContext()->mem_mode == (MF_HRAM_BANK2 | MF_SLOTCXROM | MF_HRAM_WRITE));
  CHECK(MemGetActiveContext()->last_write_ram == true);
  CHECK(*mem_2000 == 0x55);

  CpuGetRegisters()->a = orig_a;
  CpuGetRegisters()->x = orig_x;
  CpuGetRegisters()->y = orig_y;
  CpuGetRegisters()->pc = orig_pc;
  CpuGetRegisters()->sp = orig_sp;
  g_nCumulativeCycles = orig_cycles;

  MemGetActiveContext()->mem_mode = orig_mem_mode;
  MemGetActiveContext()->last_write_ram = orig_last_write_ram;
  *mem_2000 = orig_byte_2000;

  Linapple_Shutdown();
}

TEST_CASE("SaveStateManager: Filename management and Load/Save flow") {
  Linapple_Init();
  Peripheral_Manager_Init();
  Peripheral_Register_Internal();

  save_state_set_filename("test_custom_snapshot.aws");
  CHECK(strcmp(save_state_get_filename(), "test_custom_snapshot.aws") == 0);

  save_state_set_filename(nullptr);
  CHECK(strcmp(save_state_get_filename(), "") == 0);

  const char* test_file = "test_snapshot_flow.aws";
  unlink(test_file);

  save_state_set_filename(test_file);
  save_state_save();

  CHECK(access(test_file, F_OK) == 0);

  unlink(test_file);

  Linapple_Shutdown();
}
