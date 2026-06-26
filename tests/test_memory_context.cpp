#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "apple2/Memory.h"
#include "doctest.h"

TEST_CASE("Memory Context: Encapsulation and Context-Switching") {
  // 1. Get original active context
  MemoryInstance_t* original_context = MemGetActiveContext();
  REQUIRE(original_context != nullptr);

  // Set some distinct values in the original context
  original_context->mem_mode = 0x1234;
  original_context->last_write_ram = true;
  original_context->active_bank = 5;

  // 2. Setup secondary context
  MemoryInstance_t second_context{};
  second_context.mem_mode = 0x5678;
  second_context.last_write_ram = false;
  second_context.active_bank = 10;

  // 3. Switch context
  MemSetActiveContext(&second_context);
  CHECK(MemGetActiveContext() == &second_context);

  // Verify secondary context values are active
  CHECK(MemGetActiveContext()->mem_mode == 0x5678);
  CHECK(MemGetActiveContext()->last_write_ram == false);
  CHECK(MemGetActiveContext()->active_bank == 10);

  // Modify active secondary context
  MemGetActiveContext()->active_bank = 99;

  // 4. Switch back to original context
  MemSetActiveContext(original_context);
  CHECK(MemGetActiveContext() == original_context);

  // Verify original context values are restored
  CHECK(MemGetActiveContext()->mem_mode == 0x1234);
  CHECK(MemGetActiveContext()->last_write_ram == true);
  CHECK(MemGetActiveContext()->active_bank == 5);

  // Verify secondary context values were synced back correctly on switch-away
  CHECK(second_context.active_bank == 99);
}
