// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-owning-memory,
// cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// Justification: This file implements the host-independent peripheral ABI.

#include "apple2/peripherals/mockingboard/Mockingboard.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "apple2/CPU.h"
#include "apple2/Memory.h"
#include "apple2/SnapshotTypes.h"
#include "apple2/chips/6522.h"
#include "apple2/chips/SSI263.h"
#include "apple2/chips/AY8910.h"
#include "apple2/Apple2Types.h"
#include "core/AudioMixer.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Peripheral.h"

namespace via_reg {
constexpr uint8_t orb = 0x0;
constexpr uint8_t ora = 0x1;
constexpr uint8_t ddrb = 0x2;
constexpr uint8_t ddra = 0x3;
constexpr uint8_t t1l_c = 0x4;
constexpr uint8_t t1h_c = 0x5;
constexpr uint8_t t1l_l = 0x6;
constexpr uint8_t t1h_l = 0x7;
constexpr uint8_t t2l_c = 0x8;
constexpr uint8_t t2h_c = 0x9;
constexpr uint8_t sr = 0xA;
constexpr uint8_t acr = 0xB;
constexpr uint8_t pcr = 0xC;
constexpr uint8_t ifr = 0xD;
constexpr uint8_t ier = 0xE;
constexpr uint8_t ora_no_handshake = 0xF;
}  // namespace via_reg

namespace ay {
constexpr uint8_t pb_bc1 = 0x01;
constexpr uint8_t pb_bdir = 0x02;
constexpr uint8_t pb_reset_n = 0x04;
constexpr uint8_t func_write = 0x06;
constexpr uint8_t func_latch = 0x07;
constexpr uint8_t reg_mask = 0x0F;
}  // namespace ay

namespace {
enum class SoundCardType_t { uninit = 0, none, mockingboard, phasor };

constexpr int16_t audio_clamp_min = -32768;
constexpr int16_t audio_clamp_max = 32767;

constexpr int sy6522_device_a = 0;
constexpr int sy6522_device_b = 1;
constexpr int sy6522a_offset = 0x00;
constexpr int sy6522b_offset = 0x80;
constexpr int ixr_timer1 = 0x40;
constexpr int ixr_timer2 = 0x20;
constexpr int via_ifr_bit_mask = 0x7F;
constexpr int via_ifr_irq_flag = 0x80;
constexpr int runmode = 0x40;
constexpr int rm_oneshot = 0x00;
constexpr int timer_low_byte_max = 0xFF;

constexpr int num_voices_per_chip = 3;
constexpr int chips_per_card = 2;
constexpr int voices_per_card = num_voices_per_chip * chips_per_card;

constexpr double phasor_attenuation = 2.0 / 3.0;
constexpr double default_attenuation = 1.0;
constexpr uint64_t inactive_threshold_divisor = 10;
constexpr uint64_t hz_60_divisor = 60;

constexpr int mb_default_slot = 4;
constexpr int mb_type_str_max = 16;
constexpr uint8_t mb_io_addr_hi_mask = 0xFF;
constexpr uint8_t via_reg_mask = 0x0F;
constexpr size_t mb_max_slots = 8;

struct Sy6522Ay8910_t {
  SY6522 sy6522 = {};
  AY8910 ay_chip = {};
  uint16_t ay_current_register = 0;
  uint8_t ay_8910_number = 0;
  int timer_status = 0;
};

struct MockingboardPeripheral_t {
  std::array<Sy6522Ay8910_t, chips_per_card> chips = {};
  std::array<std::array<int16_t, SAMPLE_RATE>, voices_per_card> voice_buffers =
      {};
  std::array<int16_t, SAMPLE_RATE * 2> mix_buffer = {};
  uint32_t timer_period_6522 = 0;
  uint16_t mb_timer_device = 0;
  uint64_t last_cumulative_cycles = 0;
  uint64_t mb_inactive_cycle_count = 0;
  uint64_t last_60hz = 0;
  bool mb_reg_accessed_flag = false;
  bool mb_active = false;
  bool timer_irq_active = false;
  uint32_t timer1_irq_count = 0;
  SoundCardType_t type = SoundCardType_t::mockingboard;
  bool phasor_native = false;
  HostInterface_t* host = nullptr;
  int slot = 0;

  MockingboardPeripheral_t() {
    for (int i = 0; i < chips_per_card; ++i) {
      const auto idx = static_cast<size_t>(i);
      chips.at(idx).ay_8910_number = static_cast<uint8_t>(i);
      AY8910_reset_instance(&chips.at(idx).ay_chip);
    }
  }
};

struct MockingboardSaveState_t {
  struct ChipState_t {
    SY6522 sy6522;
    AY8910 ay_chip;
    uint16_t ay_current_register;
    uint8_t ay_8910_number;
    int32_t timer_status;
  };

  std::array<ChipState_t, chips_per_card> chips;
  uint32_t timer_period_6522;
  uint16_t mb_timer_device;
  uint64_t last_cumulative_cycles;
  uint64_t mb_inactive_cycle_count;
  uint64_t last_60hz;
  uint8_t mb_reg_accessed_flag;
  uint8_t mb_active;
  uint8_t timer_irq_active;
  uint32_t timer1_irq_count;
  int32_t type;
  uint8_t phasor_native;
};
}  // namespace

static std::array<MockingboardPeripheral_t*, mb_max_slots> active_mb_instances =
    {nullptr};

static auto start_timer(MockingboardPeripheral_t* mp, int chip_idx) -> void {
  if (chip_idx != sy6522_device_a) {
    return;
  }
  auto* pmb = &mp->chips.at(static_cast<size_t>(chip_idx));
  if ((pmb->sy6522.IER & ixr_timer1) == 0x00) {
    return;
  }

  uint16_t period = pmb->sy6522.TIMER1_LATCH.w;
  if (period <= timer_low_byte_max) {
    return;
  }

  pmb->timer_status = 1;
  mp->timer_period_6522 = period;
  mp->timer_irq_active = true;
  mp->mb_timer_device = static_cast<uint16_t>(chip_idx);
}

static auto stop_timer(MockingboardPeripheral_t* mp, int chip_idx) -> void {
  if (chip_idx < 0 || chip_idx >= chips_per_card) {
    return;
  }
  mp->chips.at(static_cast<size_t>(chip_idx)).timer_status = 0;
  mp->timer_irq_active = false;
}

static auto update_ifr(MockingboardPeripheral_t* mp, int chip_idx) -> void {
  if (chip_idx < 0 || chip_idx >= chips_per_card) {
    return;
  }
  auto* pmb = &mp->chips.at(static_cast<size_t>(chip_idx));
  pmb->sy6522.IFR &= via_ifr_bit_mask;

  if (pmb->sy6522.IFR & pmb->sy6522.IER & via_ifr_bit_mask) {
    pmb->sy6522.IFR |= via_ifr_irq_flag;
  }

  bool irq_asserted = false;
  for (const auto& chip : mp->chips) {
    if ((chip.sy6522.IFR & via_ifr_irq_flag) != 0) {
      irq_asserted = true;
    }
  }

  if (mp->host && mp->host->AssertIrq) {
    mp->host->AssertIrq(mp->slot, irq_asserted);
  } else {
    if (irq_asserted) {
      CpuIrqAssert(is_6522);
    } else {
      CpuIrqDeassert(is_6522);
    }
  }
}

static auto ay8910_write_instance(MockingboardPeripheral_t* mp, uint8_t device,
                                  uint8_t value) -> void {
  if (device >= static_cast<uint8_t>(chips_per_card)) {
    return;
  }
  auto* pmb = &mp->chips.at(static_cast<size_t>(device));

  if ((value & ay::pb_reset_n) == 0) {
    AY8910_reset_instance(&pmb->ay_chip);
  } else {
    int bdir = (value & ay::pb_bdir) ? 1 : 0;
    int bc1 = (value & ay::pb_bc1) ? 1 : 0;
    int ay_func = (bdir << 2) | (1 << 1) | bc1;

    if (ay_func == ay::func_write) {
      AY8910_write_instance(&pmb->ay_chip, pmb->ay_current_register,
                            pmb->sy6522.ORA,
                            static_cast<int>(g_fCurrentCLK6502), SAMPLE_RATE);
    } else if (ay_func == ay::func_latch) {
      if (pmb->sy6522.ORA <= ay::reg_mask) {
        pmb->ay_current_register =
            static_cast<uint16_t>(pmb->sy6522.ORA & ay::reg_mask);
      }
    }
  }
}

static auto sy6522_write_instance(MockingboardPeripheral_t* mp, uint8_t device,
                                  uint8_t reg, uint8_t value) -> void {
  mp->mb_reg_accessed_flag = true;
  if (!g_bFullSpeed) {
    mp->mb_active = true;
  }
  if (device >= static_cast<uint8_t>(chips_per_card)) {
    return;
  }
  auto* pmb = &mp->chips.at(static_cast<size_t>(device));

  // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
  // Justification: Reg latch and counter require direct access to union
  // components.
  switch (reg) {
    case via_reg::orb:
      value &= pmb->sy6522.DDRB;
      pmb->sy6522.ORB = value;
      if (mp->type == SoundCardType_t::phasor) {
        int ay_cs = mp->phasor_native ? (~(value >> 3) & 3) : 1;
        if ((ay_cs & 1) != 0) {
          ay8910_write_instance(mp, device, value);
        }
      } else {
        ay8910_write_instance(mp, device, value);
      }
      break;
    case via_reg::ora:
      pmb->sy6522.ORA = value & pmb->sy6522.DDRA;
      break;
    case via_reg::ddrb:
      pmb->sy6522.DDRB = value;
      break;
    case via_reg::ddra:
      pmb->sy6522.DDRA = value;
      break;
    case via_reg::t1l_c:
    case via_reg::t1l_l:
      pmb->sy6522.TIMER1_LATCH.l = value;
      break;
    case via_reg::t1h_c:
      pmb->sy6522.IFR &= ~ixr_timer1;
      update_ifr(mp, device);
      pmb->sy6522.TIMER1_LATCH.h = value;
      pmb->sy6522.TIMER1_COUNTER.w = pmb->sy6522.TIMER1_LATCH.w;
      start_timer(mp, device);
      break;
    case via_reg::t1h_l:
      pmb->sy6522.TIMER1_LATCH.h = value;
      pmb->sy6522.IFR &= ~ixr_timer1;
      update_ifr(mp, device);
      break;
    case via_reg::t2l_c:
      pmb->sy6522.TIMER2_LATCH.l = value;
      break;
    case via_reg::t2h_c:
      pmb->sy6522.IFR &= ~ixr_timer2;
      update_ifr(mp, device);
      pmb->sy6522.TIMER2_LATCH.h = value;
      pmb->sy6522.TIMER2_COUNTER.w = pmb->sy6522.TIMER2_LATCH.w;
      break;
    case via_reg::acr:
      pmb->sy6522.ACR = value;
      break;
    case via_reg::pcr:
      pmb->sy6522.PCR = value;
      break;
    case via_reg::ifr:
      value |= via_ifr_irq_flag;
      value ^= via_ifr_bit_mask;
      pmb->sy6522.IFR &= value;
      update_ifr(mp, device);
      break;
    case via_reg::ier:
      if (!(value & via_ifr_irq_flag)) {
        value ^= via_ifr_bit_mask;
        pmb->sy6522.IER &= value;
        update_ifr(mp, device);
        if (!(pmb->sy6522.IER & ixr_timer1) && pmb->timer_status != 0) {
          stop_timer(mp, device);
        }
      } else {
        value &= via_ifr_bit_mask;
        pmb->sy6522.IER |= value;
        update_ifr(mp, device);
        start_timer(mp, device);
      }
      break;
    case via_reg::sr:
      pmb->sy6522.SERIAL_SHIFT = value;
      break;
    case via_reg::ora_no_handshake:
      pmb->sy6522.ORA = value & pmb->sy6522.DDRA;
      break;
    default:
      break;
  }
  // NOLINTEND(cppcoreguidelines-pro-type-union-access)
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
// Justification: Functions are part of the Peripheral ABI or internal
// helpers that mimic it, where parameter order is fixed or follows convention.

static auto sy6522_read_instance(MockingboardPeripheral_t* mp, uint8_t device,
                                 uint8_t reg) -> uint8_t {
  mp->mb_reg_accessed_flag = true;
  if (!g_bFullSpeed) {
    mp->mb_active = true;
  }
  if (device >= static_cast<uint8_t>(chips_per_card)) {
    return 0;
  }
  auto* pmb = &mp->chips.at(static_cast<size_t>(device));

  // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
  // Justification: Read access to registers requires access to union structure.
  switch (reg) {
    case via_reg::orb:
      return pmb->sy6522.ORB;
    case via_reg::ora:
      return pmb->sy6522.ORA;
    case via_reg::ddrb:
      return pmb->sy6522.DDRB;
    case via_reg::ddra:
      return pmb->sy6522.DDRA;
    case via_reg::t1l_c:
      pmb->sy6522.IFR &= ~ixr_timer1;
      update_ifr(mp, device);
      return pmb->sy6522.TIMER1_COUNTER.l;
    case via_reg::t1h_c:
      return pmb->sy6522.TIMER1_COUNTER.h;
    case via_reg::t1l_l:
      return pmb->sy6522.TIMER1_LATCH.l;
    case via_reg::t1h_l:
      return pmb->sy6522.TIMER1_LATCH.h;
    case via_reg::t2l_c:
      pmb->sy6522.IFR &= ~ixr_timer2;
      update_ifr(mp, device);
      return pmb->sy6522.TIMER2_COUNTER.l;
    case via_reg::t2h_c:
      return pmb->sy6522.TIMER2_COUNTER.h;
    case via_reg::acr:
      return pmb->sy6522.ACR;
    case via_reg::pcr:
      return pmb->sy6522.PCR;
    case via_reg::ifr:
      return pmb->sy6522.IFR;
    case via_reg::ier:
      return static_cast<uint8_t>(pmb->sy6522.IER | via_ifr_irq_flag);
    case via_reg::sr:
      return pmb->sy6522.SERIAL_SHIFT;
    case via_reg::ora_no_handshake:
      return pmb->sy6522.ORA;
    default:
      break;
  }
  // NOLINTEND(cppcoreguidelines-pro-type-union-access)
  return 0;
}

static auto mb_update_instance(MockingboardPeripheral_t* mp) -> void {
  if (mp->type == SoundCardType_t::none || !mp->mb_active) {
    return;
  }

  double timer_period_val =
      (mp->timer_irq_active || (mp->chips.at(0).sy6522.IFR & ixr_timer1))
          ? static_cast<double>(mp->timer_period_6522)
          : (CLOCK_6502 / 60.0);

  if (timer_period_val <= 0.0) {
    timer_period_val = CLOCK_6502 / 60.0;
  }

  double irq_freq = g_fCurrentCLK6502 / timer_period_val;
  int num_samples =
      static_cast<int>(static_cast<double>(SAMPLE_RATE) / irq_freq);

  if (num_samples <= 0) {
    return;
  }

  if (static_cast<uint32_t>(num_samples) > SAMPLE_RATE) {
    num_samples = static_cast<int>(SAMPLE_RATE);
  }

  for (size_t i = 0; i < chips_per_card; i++) {
    int16_t* voices[3];
    voices[0] = mp->voice_buffers.at(i * 3 + 0).data();
    voices[1] = mp->voice_buffers.at(i * 3 + 1).data();
    voices[2] = mp->voice_buffers.at(i * 3 + 2).data();
    AY8910_update_instance(&mp->chips.at(i).ay_chip, voices, num_samples,
                           static_cast<int>(g_fCurrentCLK6502), SAMPLE_RATE);
  }

  const double attenuation = (mp->type == SoundCardType_t::phasor)
                                 ? phasor_attenuation
                                 : default_attenuation;

  for (int i = 0; i < num_samples; i++) {
    int data_l = 0;
    int data_r = 0;

    for (int j = 0; j < 3; j++) {
      data_l += static_cast<int>(
          static_cast<double>(
              mp->voice_buffers.at(0 * 3 + j).at(static_cast<size_t>(i))) *
          attenuation);
      data_r += static_cast<int>(
          static_cast<double>(
              mp->voice_buffers.at(1 * 3 + j).at(static_cast<size_t>(i))) *
          attenuation);
    }

    if (data_l < audio_clamp_min) {
      data_l = audio_clamp_min;
    } else if (data_l > audio_clamp_max) {
      data_l = audio_clamp_max;
    }
    if (data_r < audio_clamp_min) {
      data_r = audio_clamp_min;
    } else if (data_r > audio_clamp_max) {
      data_r = audio_clamp_max;
    }

    mp->mix_buffer.at(static_cast<size_t>(i) * 2) =
        static_cast<int16_t>(data_l);
    mp->mix_buffer.at(static_cast<size_t>(i) * 2 + 1) =
        static_cast<int16_t>(data_r);
  }

  if (mp->host && mp->host->AudioPushSamples) {
    mp->host->AudioPushSamples(mp, mp->mix_buffer.data(),
                               static_cast<uint32_t>(num_samples * 2));
  }
}

static auto get_cycles(HostInterface_t* host) -> uint64_t {
  if (host != nullptr && host->GetCycles != nullptr) {
    return host->GetCycles();
  }
  return CpuGetCumulativeCycles();
}

static auto mb_update_cycles_instance(MockingboardPeripheral_t* mp,
                                      uint32_t executed_cycles) -> void {
  (void)executed_cycles;
  if (mp->type == SoundCardType_t::none) {
    return;
  }

  if (get_cycles(mp->host) < mp->last_cumulative_cycles) {
    mp->last_cumulative_cycles = get_cycles(mp->host);
    return;
  }

  uint64_t cycles = get_cycles(mp->host) - mp->last_cumulative_cycles;
  mp->last_cumulative_cycles = get_cycles(mp->host);

  while (cycles > 0) {
    constexpr uint64_t max_clocks_u16 = 0xFFFF;
    constexpr uint16_t max_clocks_u16_val = 0xFFFF;
    uint16_t clocks = (cycles > max_clocks_u16) ? max_clocks_u16_val
                                                : static_cast<uint16_t>(cycles);
    cycles -= clocks;

    for (size_t i = 0; i < chips_per_card; i++) {
      auto* pmb = &mp->chips.at(i);
      // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
      // Justification: Read and modify union members directly.
      uint16_t old_timer1 = pmb->sy6522.TIMER1_COUNTER.w;
      pmb->sy6522.TIMER1_COUNTER.w =
          static_cast<uint16_t>(pmb->sy6522.TIMER1_COUNTER.w - clocks);
      pmb->sy6522.TIMER2_COUNTER.w =
          static_cast<uint16_t>(pmb->sy6522.TIMER2_COUNTER.w - clocks);

      constexpr uint16_t timer_msb_bit = 0x8000;
      bool timer1_underflow = (!(old_timer1 & timer_msb_bit) &&
                               (pmb->sy6522.TIMER1_COUNTER.w & timer_msb_bit));

      if (!timer1_underflow ||
          (mp->mb_timer_device != static_cast<uint16_t>(i)) ||
          !mp->timer_irq_active) {
        // NOLINTEND(cppcoreguidelines-pro-type-union-access)
        continue;
      }

      mp->timer1_irq_count++;
      pmb->sy6522.IFR |= ixr_timer1;
      update_ifr(mp, static_cast<int>(i));

      if ((pmb->sy6522.ACR & runmode) == rm_oneshot) {
        stop_timer(mp, static_cast<int>(i));
      } else {
        pmb->sy6522.TIMER1_COUNTER.w = pmb->sy6522.TIMER1_LATCH.w;
        start_timer(mp, static_cast<int>(i));
      }
      // NOLINTEND(cppcoreguidelines-pro-type-union-access)
      mb_update_instance(mp);
    }
  }

  if (mp->mb_reg_accessed_flag) {
    mp->mb_inactive_cycle_count = 0;
    mp->mb_reg_accessed_flag = false;
    if (!g_bFullSpeed) {
      mp->mb_active = true;
    }
    return;
  }

  if (mp->mb_inactive_cycle_count == 0) {
    mp->mb_inactive_cycle_count = get_cycles(mp->host);
    return;
  }

  if (get_cycles(mp->host) - mp->mb_inactive_cycle_count >
      static_cast<uint64_t>(g_fCurrentCLK6502) / 10) {
    mp->mb_active = false;
  }
}

static auto mb_io_read(void* instance, uint16_t pc, uint16_t addr,
                       uint8_t write, uint8_t val, uint32_t cycles_left)
    -> uint8_t {
  (void)pc;
  (void)write;
  (void)val;
  if (instance == nullptr) {
    return MemReadFloatingBus(cycles_left);
  }
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  CpuCalcCycles(cycles_left);
  mb_update_cycles_instance(mp, cycles_left);
  uint8_t offset = addr & mb_io_addr_hi_mask;
  if (offset <= (sy6522a_offset + via_reg_mask)) {
    return sy6522_read_instance(mp, sy6522_device_a, offset & via_reg_mask);
  }
  if ((offset >= sy6522b_offset) &&
      (offset <= (sy6522b_offset + via_reg_mask))) {
    return sy6522_read_instance(mp, sy6522_device_b, offset & via_reg_mask);
  }
  return MemReadFloatingBus(cycles_left);
}

static auto mb_io_write(void* instance, uint16_t pc, uint16_t addr,
                        uint8_t write, uint8_t val, uint32_t cycles_left)
    -> uint8_t {
  (void)pc;
  (void)write;
  if (instance == nullptr) {
    return 0;
  }
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  CpuCalcCycles(cycles_left);
  mb_update_cycles_instance(mp, cycles_left);

  uint8_t offset = addr & mb_io_addr_hi_mask;
  if (offset <= (sy6522a_offset + via_reg_mask)) {
    sy6522_write_instance(mp, sy6522_device_a, offset & via_reg_mask, val);
  } else if ((offset >= sy6522b_offset) &&
             (offset <= (sy6522b_offset + via_reg_mask))) {
    sy6522_write_instance(mp, sy6522_device_b, offset & via_reg_mask, val);
  }
  return 0;
}

static auto phasor_io(void* instance, uint16_t pc, uint16_t addr, uint8_t write,
                      uint8_t val, uint32_t cycles_left) -> uint8_t {
  (void)pc;
  if (instance == nullptr) {
    return MemReadFloatingBus(cycles_left);
  }
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  CpuCalcCycles(cycles_left);
  mb_update_cycles_instance(mp, cycles_left);

  if (!mp->phasor_native) {
    mp->phasor_native = (addr & 1) != 0;
  }

  uint8_t cs = 0;
  if (mp->phasor_native) {
    constexpr int shift_bit_3 = 2;
    constexpr int shift_bit_2 = 2;
    constexpr uint8_t mask_bit_3 = 0x08;
    constexpr uint8_t mask_bit_2 = 0x04;
    cs = ((addr & mask_bit_3) >> shift_bit_3) |
         ((addr & mask_bit_2) >> shift_bit_2);
  } else {
    constexpr int shift_bit_3 = 3;
    constexpr uint8_t mask_bit_3 = 0x08;
    cs = ((addr & mask_bit_3) >> shift_bit_3) + 1;
  }

  if (cs == 1 || cs == 2) {
    const uint8_t dev = (cs == 1) ? sy6522_device_a : sy6522_device_b;
    const uint8_t reg = addr & via_reg_mask;

    if (write == 0) {
      return sy6522_read_instance(mp, dev, reg);
    }

    sy6522_write_instance(mp, dev, reg, val);
    return 0;
  }

  return (write != 0) ? 0 : MemReadFloatingBus(cycles_left);
}

static auto mb_abi_init(int slot, HostInterface_t* host) -> void* {
  const auto s_idx = static_cast<size_t>(slot);
  auto* mp = active_mb_instances.at(s_idx);

  if (!mp) {
    auto new_mp = std::unique_ptr<MockingboardPeripheral_t>(
        new MockingboardPeripheral_t{});
    mp = new_mp.release();
    active_mb_instances.at(s_idx) = mp;

    char type_str[mb_type_str_max] = {0};
    if (host->GetConfig("Mockingboard", "Type", type_str, sizeof(type_str)) &&
        strcmp(type_str, "Phasor") == 0) {
      mp->type = SoundCardType_t::phasor;
    }
  }

  mp->host = host;
  mp->slot = slot;

  auto* handler = (mp->type == SoundCardType_t::phasor) ? phasor_io : nullptr;
  host->RegisterIO(slot, handler, handler, mb_io_read, mb_io_write);

  return mp;
}

static auto mb_abi_reset(void* instance) -> void {
  if (!instance) {
    return;
  }
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  mp->timer_period_6522 = 0;
  mp->mb_timer_device = 0;
  mp->last_cumulative_cycles = get_cycles(mp->host);
  mp->mb_reg_accessed_flag = false;
  mp->mb_active = false;
  mp->mb_inactive_cycle_count = 0;
  mp->last_60hz = get_cycles(mp->host);
  mp->phasor_native = false;

  for (auto& chip : mp->chips) {
    memset(&chip.sy6522, 0, sizeof(SY6522));
    AY8910_reset_instance(&chip.ay_chip);
    chip.timer_status = 0;
    chip.ay_current_register = 0;
  }
}

static auto mb_abi_shutdown(void* instance) -> void {
  if (!instance) {
    return;
  }
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  active_mb_instances.at(static_cast<size_t>(mp->slot)) = nullptr;
  std::unique_ptr<MockingboardPeripheral_t> cleanup(mp);
}

static auto mb_abi_think(void* instance, uint32_t cycles) -> void {
  if (!instance) {
    return;
  }
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  mb_update_cycles_instance(mp, cycles);

  // If timers are inactive, force a 60Hz audio update to prevent buffer
  // starvation.
  const bool timers_active =
      mp->timer_irq_active || (mp->chips.at(0).sy6522.IFR & ixr_timer1);
  if (timers_active) {
    return;
  }

  const uint64_t cycles_since_last_update = get_cycles(mp->host) - mp->last_60hz;
  const uint64_t cycles_per_frame =
      static_cast<uint64_t>(g_fCurrentCLK6502) / hz_60_divisor;

  if (cycles_since_last_update > cycles_per_frame) {
    mp->last_60hz = get_cycles(mp->host);
    mb_update_instance(mp);
  }
}

static auto mb_abi_save_state(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus_t {
  if (!instance || !size) {
    return peripheral_error;
  }

  constexpr size_t required = sizeof(MockingboardSaveState_t);
  if (!buffer) {
    *size = required;
    return peripheral_ok;
  }

  if (*size < required) {
    *size = required;
    return peripheral_error;
  }

  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  auto* ss = static_cast<MockingboardSaveState_t*>(buffer);

  memset(ss, 0, required);
  for (int i = 0; i < chips_per_card; ++i) {
    const auto& src = mp->chips.at(static_cast<size_t>(i));
    auto& dst = ss->chips.at(static_cast<size_t>(i));
    dst.sy6522 = src.sy6522;
    dst.ay_chip = src.ay_chip;
    dst.ay_current_register = src.ay_current_register;
    dst.ay_8910_number = src.ay_8910_number;
    dst.timer_status = src.timer_status;
  }

  ss->timer_period_6522 = mp->timer_period_6522;
  ss->mb_timer_device = mp->mb_timer_device;
  ss->last_cumulative_cycles = mp->last_cumulative_cycles;
  ss->mb_inactive_cycle_count = mp->mb_inactive_cycle_count;
  ss->last_60hz = mp->last_60hz;
  ss->mb_reg_accessed_flag = mp->mb_reg_accessed_flag ? 1 : 0;
  ss->mb_active = mp->mb_active ? 1 : 0;
  ss->timer_irq_active = mp->timer_irq_active ? 1 : 0;
  ss->timer1_irq_count = mp->timer1_irq_count;
  ss->type = static_cast<int32_t>(mp->type);
  ss->phasor_native = mp->phasor_native ? 1 : 0;

  return peripheral_ok;
}

static auto mb_abi_load_state(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus_t {
  if (!instance || !buffer) {
    return peripheral_error;
  }

  constexpr size_t required = sizeof(MockingboardSaveState_t);
  if (size < required) {
    return peripheral_error;
  }

  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  const auto* ss = static_cast<const MockingboardSaveState_t*>(buffer);

  for (int i = 0; i < chips_per_card; ++i) {
    auto& dst = mp->chips.at(static_cast<size_t>(i));
    const auto& src = ss->chips.at(static_cast<size_t>(i));
    dst.sy6522 = src.sy6522;
    dst.ay_chip = src.ay_chip;
    dst.ay_current_register = src.ay_current_register;
    dst.ay_8910_number = src.ay_8910_number;
    dst.timer_status = src.timer_status;
  }

  mp->timer_period_6522 = ss->timer_period_6522;
  mp->mb_timer_device = ss->mb_timer_device;
  mp->last_cumulative_cycles = ss->last_cumulative_cycles;
  mp->mb_inactive_cycle_count = ss->mb_inactive_cycle_count;
  mp->last_60hz = ss->last_60hz;
  mp->mb_reg_accessed_flag = (ss->mb_reg_accessed_flag != 0);
  mp->mb_active = (ss->mb_active != 0);
  mp->timer_irq_active = (ss->timer_irq_active != 0);
  mp->timer1_irq_count = ss->timer1_irq_count;
  mp->type = static_cast<SoundCardType_t>(ss->type);
  mp->phasor_native = (ss->phasor_native != 0);

  return peripheral_ok;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

static Peripheral_t g_mockingboard_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.mockingboard",
    .name = "Mockingboard",
    .description = "Dual AY-3-8910 sound card emulation",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = mb_default_slot,
    .init = mb_abi_init,
    .reset = mb_abi_reset,
    .shutdown = mb_abi_shutdown,
    .think = mb_abi_think,
    .on_vblank = nullptr,
    .save_state = mb_abi_save_state,
    .load_state = mb_abi_load_state,
    .command = nullptr,
    .query = nullptr};

auto Mockingboard_GetDescriptor() -> Peripheral_t* {
  return &g_mockingboard_peripheral;
}

PERIPHERAL_REGISTER(g_mockingboard_peripheral)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,
// cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays,
// cppcoreguidelines-pro-bounds-array-to-pointer-decay,
// cppcoreguidelines-owning-memory,
// cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
