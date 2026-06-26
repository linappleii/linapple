#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/CPU.h"
#include "doctest.h"

TEST_CASE("CPU Context: Encapsulation and Context-Switching") {
  // 1. Get original context
  CpuInstance_t* original_context = CpuGetActiveContext();
  REQUIRE(original_context != nullptr);

  // Set values on original context
  CpuGetRegisters()->a = 0x11;
  CpuGetRegisters()->x = 0x22;
  CpuGetRegisters()->y = 0x33;
  CpuGetRegisters()->pc = 0x1000;
  CpuGetRegisters()->sp = 0x1FF;
  g_nCumulativeCycles = 100;

  // 2. Setup secondary context
  CpuInstance_t second_context{};
  second_context.cpu_regs.a = 0xAA;
  second_context.cpu_regs.x = 0xBB;
  second_context.cpu_regs.y = 0xCC;
  second_context.cpu_regs.pc = 0x2000;
  second_context.cpu_regs.sp = 0x180;
  second_context.cumulative_cycles = 500;

  // 3. Switch context
  CpuSetActiveContext(&second_context);
  CHECK(CpuGetActiveContext() == &second_context);

  // Verify secondary context values are active
  CHECK(CpuGetRegisters()->a == 0xAA);
  CHECK(CpuGetRegisters()->x == 0xBB);
  CHECK(CpuGetRegisters()->y == 0xCC);
  CHECK(CpuGetRegisters()->pc == 0x2000);
  CHECK(CpuGetRegisters()->sp == 0x180);
  CHECK(CpuGetCumulativeCycles() == 500);

  // Modify registers on active secondary context
  CpuGetRegisters()->x = 0x99;
  g_nCumulativeCycles = 600;

  // 4. Switch back to original context
  CpuSetActiveContext(original_context);
  CHECK(CpuGetActiveContext() == original_context);

  // Verify original context values are restored
  CHECK(CpuGetRegisters()->a == 0x11);
  CHECK(CpuGetRegisters()->x == 0x22);
  CHECK(CpuGetRegisters()->y == 0x33);
  CHECK(CpuGetRegisters()->pc == 0x1000);
  CHECK(CpuGetRegisters()->sp == 0x1FF);
  CHECK(CpuGetCumulativeCycles() == 100);

  // Verify secondary context values were synced back correctly on switch-away
  CHECK(second_context.cpu_regs.x == 0x99);
  CHECK(second_context.cumulative_cycles == 600);
}
