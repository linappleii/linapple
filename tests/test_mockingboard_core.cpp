// SPDX-License-Identifier: GPL-2.0-only
#include <array>
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

// RAII fixture isolating file-static mock state between tests.
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

constexpr uint32_t NTSC_FRAME_CYCLES = 17030;

// Slot 4's VIA A, and the registers this case drives.
constexpr uint16_t VIA_A_ORB = 0xC400;
constexpr uint16_t VIA_A_T1C_L = 0xC404;
constexpr uint16_t VIA_A_T1C_H = 0xC405;
constexpr uint16_t VIA_A_ACR = 0xC40B;
constexpr uint16_t VIA_A_IER = 0xC40E;

constexpr uint16_t HANDLER_ADDR = 0x0300;
constexpr uint16_t PROGRAM_ADDR = 0x0320;

// LDA $C404 acknowledges Timer 1 by reading its counter, then RTI. A handler
// that did not acknowledge would re-enter on every instruction and the count
// would say nothing about the timer.
constexpr std::array<uint8_t, 4> irq_handler = {{0xAD, 0x04, 0xC4, 0x40}};

// JMP to itself: the interrupted instruction is always the same one, so the
// entry count depends only on the timer.
constexpr std::array<uint8_t, 3> spin_program = {{0x4C, 0x20, 0x03}};

// A free-running Timer 1 with latch N interrupts every N + 2 cycles, so 4098
// cycles is four underflows inside one NTSC frame and the fifth 3460 cycles
// past its end.
constexpr uint16_t TIMER1_LATCH = 0x1000;
constexpr int EXPECTED_IRQ_ENTRIES = 4;

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

TEST_CASE("Mockingboard Core Seam: A Timer Interrupt Reaches The 6502") {
  TestConfig_t config(mockingboard_in_slot_4());
  ScopedCore_t core(config);

  ScopedCore_t::poke(HANDLER_ADDR, irq_handler);
  ScopedCore_t::poke(PROGRAM_ADDR, spin_program);
  const std::array<uint8_t, 2> vector = {
      {static_cast<uint8_t>(HANDLER_ADDR & 0xFF),
       static_cast<uint8_t>(HANDLER_ADDR >> 8)}};
  ScopedCore_t::poke(IRQ_VECTOR_ADDR, vector);

  io_map_dispatch(0, VIA_A_ORB, 1, 0x04, 0);
  io_map_dispatch(0, VIA_A_ACR, 1, 0x40, 0);
  io_map_dispatch(0, VIA_A_IER, 1, 0xC0, 0);
  io_map_dispatch(0, VIA_A_T1C_L, 1, static_cast<uint8_t>(TIMER1_LATCH), 0);
  io_map_dispatch(0, VIA_A_T1C_H, 1, static_cast<uint8_t>(TIMER1_LATCH >> 8),
                  0);

  CpuRegisters_t* regs = cpu_get_registers();
  regs->pc = PROGRAM_ADDR;
  regs->sp = 0x01FF;
  regs->ps = 0x20;

  // The card advances only on think, so the frame is run the way the core
  // runs one: an instruction, then the cycles it took handed to the slot.
  int entries = 0;
  uint32_t elapsed = 0;
  while (elapsed < NTSC_FRAME_CYCLES) {
    const uint32_t executed = cpu_execute(0);
    elapsed += executed;
    peripheral_manager_think(executed);
    entries += static_cast<int>(regs->pc == HANDLER_ADDR);
  }

  CHECK(entries == EXPECTED_IRQ_ENTRIES);
}
