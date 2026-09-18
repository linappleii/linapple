// SPDX-License-Identifier: GPL-2.0-only
#include <cstddef>
#include <cstdint>
#include <vector>

#include "apple2/CPU.h"
#include "apple2/peripherals/Peripheral.h"
#include "doctest.h"
#include "test_fixtures_core.h"

auto io_map_dispatch(uint16_t pc, uint16_t addr, uint8_t write, uint8_t val,
                     uint32_t cycles) -> uint8_t;

namespace {

using TestConfig_t = TestFixtures::ScopedTestConfig_t;
using TestFixtures::ScopedCore_t;

constexpr int MOCK_SLOT = 2;
// $C080 + slot * 16, and the slot's own $Cn00 page.
constexpr uint16_t ADDR_DEVICE_SELECT = 0xC0A0;
constexpr uint16_t ADDR_IO_SELECT = 0xC200;

// One value per bridge, all different, so that a bridge which folds nothing
// cannot borrow the cycles an earlier bridge in the same case folded.
constexpr uint32_t EXECUTED_READ_C0 = 12;
constexpr uint32_t EXECUTED_WRITE_C0 = 24;
constexpr uint32_t EXECUTED_READ_CX = 36;
constexpr uint32_t EXECUTED_WRITE_CX = 48;

struct MockState_t {
  HostInterface_t* host = nullptr;
  std::vector<uint64_t> observed;
};

MockState_t g_mock;

auto record_cycles(void* instance, uint16_t, uint16_t, uint8_t, uint8_t,
                   uint32_t) -> uint8_t {
  auto* state = static_cast<MockState_t*>(instance);
  if (state != nullptr && state->host != nullptr &&
      state->host->GetCycles != nullptr) {
    state->observed.push_back(state->host->GetCycles());
  }
  return 0;
}

auto mock_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr || host->RegisterIO == nullptr) {
    return nullptr;
  }
  g_mock.host = host;
  host->RegisterIO(slot, record_cycles, record_cycles, record_cycles,
                   record_cycles);
  return &g_mock;
}

auto mock_shutdown(void* instance) -> void { (void)instance; }

Peripheral_t g_mock_descriptor = {LINAPPLE_ABI_VERSION,
                                  "test.mock.slot",
                                  "MockSlotCard",
                                  "Records the cycle count at handler entry",
                                  "LinApple Contributors",
                                  "1.0.0",
                                  PERIPHERAL_MASK_EXPANSION,
                                  -1,
                                  mock_init,
                                  nullptr,
                                  mock_shutdown,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  nullptr};

/**
 * @brief RAII owner of the mock's recording.
 *
 * The descriptor is a C-ABI struct of free functions, so what the handlers
 * record has to live in a file-static; this bounds its lifetime to one case.
 */
class ScopedMock_t {
 public:
  ScopedMock_t() { g_mock = MockState_t(); }
  ~ScopedMock_t() { g_mock = MockState_t(); }

  ScopedMock_t(const ScopedMock_t&) = delete;
  auto operator=(const ScopedMock_t&) -> ScopedMock_t& = delete;
  ScopedMock_t(ScopedMock_t&&) = delete;
  auto operator=(ScopedMock_t&&) -> ScopedMock_t& = delete;

  static auto descriptor() -> Peripheral_t* { return &g_mock_descriptor; }
  static auto observed() -> const std::vector<uint64_t>& {
    return g_mock.observed;
  }
};

auto mockingboard_in_slot_4() -> TestConfig_t::Description_t {
  TestConfig_t::Description_t description;
  description.slots[3] = "Mockingboard";
  return description;
}

}  // namespace

TEST_CASE("Mockingboard Core Seam: A Slot Handler Sees The Executed Cycles") {
  // A card asked for a register mid-instruction has to be able to reconstruct
  // where in the slice it is, and GetCycles() is the only thing that tells it.
  // If the bridge hands the handler a count that still stops at the slice
  // boundary, every card either reads stale time or adds the executed cycles
  // itself -- and double-counts the moment a $C0xx handler ran first in the
  // same slice.
  TestConfig_t config(mockingboard_in_slot_4());
  ScopedCore_t core(config);
  ScopedMock_t mock;
  REQUIRE(peripheral_register(ScopedMock_t::descriptor(), MOCK_SLOT) == 0);

  const uint64_t slice_start = cpu_get_cumulative_cycles();

  io_map_dispatch(0, ADDR_DEVICE_SELECT, 0, 0, EXECUTED_READ_C0);
  io_map_dispatch(0, ADDR_DEVICE_SELECT, 1, 0x55, EXECUTED_WRITE_C0);
  io_map_dispatch(0, ADDR_IO_SELECT, 0, 0, EXECUTED_READ_CX);
  io_map_dispatch(0, ADDR_IO_SELECT, 1, 0x55, EXECUTED_WRITE_CX);

  REQUIRE(ScopedMock_t::observed().size() == 4);
  CHECK(ScopedMock_t::observed()[0] == slice_start + EXECUTED_READ_C0);
  CHECK(ScopedMock_t::observed()[1] == slice_start + EXECUTED_WRITE_C0);
  CHECK(ScopedMock_t::observed()[2] == slice_start + EXECUTED_READ_CX);
  CHECK(ScopedMock_t::observed()[3] == slice_start + EXECUTED_WRITE_CX);
}
