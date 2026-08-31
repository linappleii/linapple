#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdlib>
#include "apple2/Memory.h"
#include "doctest.h"

TEST_CASE("Memory Context: Encapsulation and Context-Switching") {
  // 1. Get original active context
  MemoryInstance_t* original_context = mem_get_active_context();
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
  mem_set_active_context(&second_context);
  CHECK(mem_get_active_context() == &second_context);

  // Verify secondary context values are active
  CHECK(mem_get_active_context()->mem_mode == 0x5678);
  CHECK(mem_get_active_context()->last_write_ram == false);
  CHECK(mem_get_active_context()->active_bank == 10);

  // Modify active secondary context
  mem_get_active_context()->active_bank = 99;

  // 4. Switch back to original context
  mem_set_active_context(original_context);
  CHECK(mem_get_active_context() == original_context);

  // Verify original context values are restored
  CHECK(mem_get_active_context()->mem_mode == 0x1234);
  CHECK(mem_get_active_context()->last_write_ram == true);
  CHECK(mem_get_active_context()->active_bank == 5);

  // Verify secondary context values were synced back correctly on switch-away
  CHECK(second_context.active_bank == 99);
}

TEST_CASE("Memory Context: Destructor does not free active context buffers (MEM-1)") {
  MemoryInstance_t active_ctx{};
  active_ctx.memmain = static_cast<uint8_t*>(malloc(MEMORY_64K));
  active_ctx.memaux_allocated = static_cast<uint8_t*>(malloc(MEMORY_64K));
  REQUIRE(active_ctx.memmain != nullptr);
  REQUIRE(active_ctx.memaux_allocated != nullptr);
  active_ctx.memmain[0] = 0x42;

  MemoryInstance_t* prev_active = mem_get_active_context();
  mem_set_active_context(&active_ctx);

  {
    // Create secondary context that allocates its own buffers
    MemoryInstance_t second_ctx{};
    second_ctx.memmain = static_cast<uint8_t*>(malloc(MEMORY_64K));
    REQUIRE(second_ctx.memmain != nullptr);
    second_ctx.memmain[0] = 0x99;
    // second_ctx destroyed at scope exit while active_ctx is still active
  }

  // Active context must not have had its buffers freed or invalidated
  CHECK(active_ctx.memmain != nullptr);
  CHECK(active_ctx.memmain[0] == 0x42);

  mem_set_active_context(prev_active);
}
