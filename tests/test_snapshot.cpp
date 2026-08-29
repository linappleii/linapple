// SPDX-License-Identifier: GPL-2.0-only
#include <cstdint>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <unistd.h>

#include <cstring>
#include <memory>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/Snapshot.h"
#include "apple2/SnapshotTypes.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral_Internal.h"
#include "doctest.h"
#include "frontends/common/SaveStateManager.h"

TEST_CASE("Snapshot: [RoundTrip] Serialize and Deserialize") {
  linapple_init();
  peripheral_manager_init();
  peripheral_register_internal();

  uint8_t orig_a = cpu_get_registers()->a;
  uint8_t orig_x = cpu_get_registers()->x;
  uint8_t orig_y = cpu_get_registers()->y;
  uint16_t orig_pc = cpu_get_registers()->pc;
  uint16_t orig_sp = cpu_get_registers()->sp;
  uint64_t orig_cycles = cpu_get_cumulative_cycles();

  uint32_t orig_mem_mode = mem_get_active_context()->mem_mode;
  bool orig_last_write_ram = mem_get_active_context()->last_write_ram;

  uint8_t* mem_2000 = mem_get_main_ptr(0x2000);
  uint8_t orig_byte_2000 = *mem_2000;

  cpu_get_registers()->a = 0x11;
  cpu_get_registers()->x = 0x22;
  cpu_get_registers()->y = 0x33;
  cpu_get_registers()->pc = 0x1000;
  cpu_get_registers()->sp = 0x1FF;
  g_cumulative_cycles = 12345;

  mem_get_active_context()->mem_mode =
      MF_HRAM_BANK2 | MF_SLOTCXROM | MF_HRAM_WRITE;
  mem_get_active_context()->last_write_ram = true;
  *mem_2000 = 0x55;

  auto snapshot = std::unique_ptr<ApplewinSnapshot_t>(new ApplewinSnapshot_t());
  snapshot_serialize(snapshot.get());

  cpu_get_registers()->a = 0xFF;
  cpu_get_registers()->x = 0xFF;
  cpu_get_registers()->y = 0xFF;
  cpu_get_registers()->pc = 0x9999;
  cpu_get_registers()->sp = 0x100;
  g_cumulative_cycles = 99999;

  mem_get_active_context()->mem_mode = MF_80STORE | MF_ALTZP;
  mem_get_active_context()->last_write_ram = false;
  *mem_2000 = 0xAA;

  bool success = snapshot_deserialize(snapshot.get());
  REQUIRE(success);

  CHECK(cpu_get_registers()->a == 0x11);
  CHECK(cpu_get_registers()->x == 0x22);
  CHECK(cpu_get_registers()->y == 0x33);
  CHECK(cpu_get_registers()->pc == 0x1000);
  CHECK(cpu_get_registers()->sp == 0x1FF);
  CHECK(cpu_get_cumulative_cycles() == 12345);

  CHECK(mem_get_active_context()->mem_mode ==
        (MF_HRAM_BANK2 | MF_SLOTCXROM | MF_HRAM_WRITE));
  CHECK(mem_get_active_context()->last_write_ram == true);
  CHECK(*mem_2000 == 0x55);

  cpu_get_registers()->a = orig_a;
  cpu_get_registers()->x = orig_x;
  cpu_get_registers()->y = orig_y;
  cpu_get_registers()->pc = orig_pc;
  cpu_get_registers()->sp = orig_sp;
  g_cumulative_cycles = orig_cycles;

  mem_get_active_context()->mem_mode = orig_mem_mode;
  mem_get_active_context()->last_write_ram = orig_last_write_ram;
  *mem_2000 = orig_byte_2000;

  linapple_shutdown();
}

TEST_CASE("SaveStateManager: Filename management and Load/Save flow") {
  linapple_init();
  peripheral_manager_init();
  peripheral_register_internal();

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

  linapple_shutdown();
}
