// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/CPU.h"
#include "core/Peripheral_Types.h"
#include "doctest.h"

TEST_CASE("CPU Context: Encapsulation and Context-Switching") {
  // 1. Get original context
  CpuInstance_t* original_context = cpu_get_active_context();
  REQUIRE(original_context != nullptr);

  // Set values on original context
  cpu_get_registers()->a = 0x11;
  cpu_get_registers()->x = 0x22;
  cpu_get_registers()->y = 0x33;
  cpu_get_registers()->pc = 0x1000;
  cpu_get_registers()->sp = 0x1FF;
  g_cumulative_cycles = 100;
  cpu_irq_assert(is_speech);
  cpu_nmi_assert(is_speech);

  // 2. Setup secondary context
  CpuInstance_t second_context{};
  second_context.cpu_regs.a = 0xAA;
  second_context.cpu_regs.x = 0xBB;
  second_context.cpu_regs.y = 0xCC;
  second_context.cpu_regs.pc = 0x2000;
  second_context.cpu_regs.sp = 0x180;
  second_context.cumulative_cycles = 500;
  second_context.cycles_submitted = 1000;
  second_context.cycles_executed = 200;
  second_context.bm_irq = 0;
  second_context.bm_nmi = 0;
  second_context.nmi_flank = false;

  // 3. Switch context
  cpu_set_active_context(&second_context);
  CHECK(cpu_get_active_context() == &second_context);

  // Verify secondary context values are active
  CHECK(cpu_get_registers()->a == 0xAA);
  CHECK(cpu_get_registers()->x == 0xBB);
  CHECK(cpu_get_registers()->y == 0xCC);
  CHECK(cpu_get_registers()->pc == 0x2000);
  CHECK(cpu_get_registers()->sp == 0x180);
  CHECK(cpu_get_cumulative_cycles() == 500);

  // Modify registers and interrupt state on active secondary context
  cpu_get_registers()->x = 0x99;
  g_cumulative_cycles = 600;
  cpu_irq_assert(is_6522);

  // 4. Switch back to original context
  cpu_set_active_context(original_context);
  CHECK(cpu_get_active_context() == original_context);

  // Verify original context values are restored
  CHECK(cpu_get_registers()->a == 0x11);
  CHECK(cpu_get_registers()->x == 0x22);
  CHECK(cpu_get_registers()->y == 0x33);
  CHECK(cpu_get_registers()->pc == 0x1000);
  CHECK(cpu_get_registers()->sp == 0x1FF);
  CHECK(cpu_get_cumulative_cycles() == 100);
  CHECK((original_context->bm_irq & (1U << is_speech)) != 0);
  CHECK((original_context->bm_irq & (1U << is_6522)) == 0);
  CHECK((original_context->bm_nmi & (1U << is_speech)) != 0);
  CHECK(original_context->nmi_flank == true);

  // Verify secondary context values were synced back correctly on switch-away
  CHECK(second_context.cpu_regs.x == 0x99);
  CHECK(second_context.cumulative_cycles == 600);
  CHECK((second_context.bm_irq & (1U << is_6522)) != 0);
  CHECK((second_context.bm_irq & (1U << is_speech)) == 0);
  CHECK(second_context.bm_nmi == 0);
}
