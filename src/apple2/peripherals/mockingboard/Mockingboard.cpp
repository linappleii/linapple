// SPDX-License-Identifier: GPL-2.0-only

#include "apple2/peripherals/mockingboard/Mockingboard.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#include "EmbeddedRoms.h"
#include "apple2/Apple2Types.h"
#include "apple2/chips/6522.h"
#include "apple2/chips/AY8910.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/mockingboard/MockingboardCommands.h"

#ifndef VERSIONSTRING
#define VERSIONSTRING "2.0.0"
#endif

auto mem_read_floating_bus(uint32_t executed_cycles) -> uint8_t;
extern bool g_full_speed;

namespace {

static_assert(sizeof(MockingboardSaveState_t) == 232,
              "MockingboardSaveState_t must be exactly 232 bytes");

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

constexpr uint32_t default_mockingboard_sample_rate = 44100;
constexpr double default_mockingboard_clock = CLOCK_6502;

// The 6522 T1 latch is 16-bit, so the slowest IRQ is CLOCK_6502/65535 ≈ 15.6
// Hz, bounding one update at ~2832 samples. 4096 leaves headroom without
// rounding up to a full second of audio per voice.
constexpr size_t mb_max_samples_per_update = 4096;

struct Sy6522Ay8910_t {
  Sy6522_t sy6522 = {};
  Ay8910_t ay_chip = {};
  uint16_t ay_current_register = 0;
  uint8_t ay_8910_number = 0;
  int timer_status = 0;
};

struct MockingboardPeripheral_t {
  std::array<Sy6522Ay8910_t, chips_per_card> chips = {};
  std::array<std::array<int16_t, mb_max_samples_per_update>, voices_per_card>
      voice_buffers = {};
  std::array<int16_t, mb_max_samples_per_update * 2> mix_buffer = {};
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
      ay8910_reset_instance(&chips.at(idx).ay_chip);
    }
  }
};

auto get_cycles(HostInterface_t* host) -> uint64_t {
  if (host != nullptr && host->GetCycles != nullptr) {
    return host->GetCycles();
  }
  return 0;
}

auto get_clock_hz(HostInterface_t* host) -> double {
  if (host != nullptr && host->GetClockHz != nullptr) {
    return host->GetClockHz();
  }
  return default_mockingboard_clock;
}

auto start_timer(MockingboardPeripheral_t* mp, int chip_idx) -> void {
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

auto stop_timer(MockingboardPeripheral_t* mp, int chip_idx) -> void {
  if (chip_idx < 0 || chip_idx >= chips_per_card) {
    return;
  }
  mp->chips.at(static_cast<size_t>(chip_idx)).timer_status = 0;
  mp->timer_irq_active = false;
}

auto update_ifr(MockingboardPeripheral_t* mp, int chip_idx) -> void {
  if (chip_idx < 0 || chip_idx >= chips_per_card) {
    return;
  }
  auto* pmb = &mp->chips.at(static_cast<size_t>(chip_idx));
  pmb->sy6522.IFR &= via_ifr_bit_mask;

  if ((pmb->sy6522.IFR & pmb->sy6522.IER & via_ifr_bit_mask) != 0) {
    pmb->sy6522.IFR |= via_ifr_irq_flag;
  }

  bool irq_asserted = false;
  for (const auto& chip : mp->chips) {
    if ((chip.sy6522.IFR & via_ifr_irq_flag) != 0) {
      irq_asserted = true;
    }
  }

  if (mp->host != nullptr && mp->host->AssertIrq != nullptr) {
    mp->host->AssertIrq(mp->slot, irq_asserted);
  }
}

auto ay8910_write_instance(MockingboardPeripheral_t* mp, uint8_t device,
                           uint8_t value) -> void {
  if (device >= static_cast<uint8_t>(chips_per_card)) {
    return;
  }
  auto* pmb = &mp->chips.at(static_cast<size_t>(device));

  if ((value & ay::pb_reset_n) == 0) {
    ay8910_reset_instance(&pmb->ay_chip);
  } else {
    int bdir = (value & ay::pb_bdir) ? 1 : 0;
    int bc1 = (value & ay::pb_bc1) ? 1 : 0;
    int ay_func = (bdir << 2) | (1 << 1) | bc1;

    if (ay_func == ay::func_write) {
      ay8910_write_instance(&pmb->ay_chip, pmb->ay_current_register,
                            pmb->sy6522.ORA,
                            static_cast<int>(get_clock_hz(mp->host)),
                            static_cast<int>(default_mockingboard_sample_rate));
    } else if (ay_func == ay::func_latch) {
      if (pmb->sy6522.ORA <= ay::reg_mask) {
        pmb->ay_current_register =
            static_cast<uint16_t>(pmb->sy6522.ORA & ay::reg_mask);
      }
    }
  }
}

auto sy6522_write_instance(MockingboardPeripheral_t* mp, int chip_idx,
                           uint8_t reg, uint8_t val) -> void {
  if (chip_idx < 0 || chip_idx >= chips_per_card) {
    return;
  }
  auto* pmb = &mp->chips.at(static_cast<size_t>(chip_idx));

  switch (reg) {
    case via_reg::orb:
      pmb->sy6522.ORB = val;
      ay8910_write_instance(mp, static_cast<uint8_t>(chip_idx), val);
      break;
    case via_reg::ora:
      pmb->sy6522.ORA = val;
      break;
    case via_reg::ddrb:
      pmb->sy6522.DDRB = val;
      break;
    case via_reg::ddra:
      pmb->sy6522.DDRA = val;
      break;
    case via_reg::t1l_c:
      pmb->sy6522.TIMER1_LATCH.l = val;
      break;
    case via_reg::t1h_c:
      pmb->sy6522.TIMER1_LATCH.h = val;
      pmb->sy6522.TIMER1_COUNTER.w = pmb->sy6522.TIMER1_LATCH.w;
      pmb->sy6522.IFR &= ~ixr_timer1;
      update_ifr(mp, chip_idx);
      start_timer(mp, chip_idx);
      break;
    case via_reg::t1l_l:
      pmb->sy6522.TIMER1_LATCH.l = val;
      break;
    case via_reg::t1h_l:
      pmb->sy6522.TIMER1_LATCH.h = val;
      pmb->sy6522.IFR &= ~ixr_timer1;
      update_ifr(mp, chip_idx);
      break;
    case via_reg::t2l_c:
      pmb->sy6522.TIMER2_LATCH.l = val;
      break;
    case via_reg::t2h_c:
      pmb->sy6522.TIMER2_LATCH.h = val;
      pmb->sy6522.TIMER2_COUNTER.w = pmb->sy6522.TIMER2_LATCH.w;
      pmb->sy6522.IFR &= ~ixr_timer2;
      update_ifr(mp, chip_idx);
      break;
    case via_reg::sr:
      pmb->sy6522.SERIAL_SHIFT = val;
      break;
    case via_reg::acr:
      pmb->sy6522.ACR = val;
      break;
    case via_reg::pcr:
      pmb->sy6522.PCR = val;
      break;
    case via_reg::ifr:
      pmb->sy6522.IFR &= ~val;
      update_ifr(mp, chip_idx);
      break;
    case via_reg::ier:
      if ((val & via_ifr_irq_flag) != 0) {
        pmb->sy6522.IER |= (val & via_ifr_bit_mask);
      } else {
        pmb->sy6522.IER &= ~(val & via_ifr_bit_mask);
      }
      update_ifr(mp, chip_idx);
      break;
    case via_reg::ora_no_handshake:
      pmb->sy6522.ORA_NO_HS = val;
      break;
    default:
      break;
  }
}

auto sy6522_read_instance(MockingboardPeripheral_t* mp, int chip_idx,
                          uint8_t reg) -> uint8_t {
  if (chip_idx < 0 || chip_idx >= chips_per_card) {
    return 0;
  }
  auto* pmb = &mp->chips.at(static_cast<size_t>(chip_idx));

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
      update_ifr(mp, chip_idx);
      return pmb->sy6522.TIMER1_COUNTER.l;
    case via_reg::t1h_c:
      return pmb->sy6522.TIMER1_COUNTER.h;
    case via_reg::t1l_l:
      return pmb->sy6522.TIMER1_LATCH.l;
    case via_reg::t1h_l:
      return pmb->sy6522.TIMER1_LATCH.h;
    case via_reg::t2l_c:
      pmb->sy6522.IFR &= ~ixr_timer2;
      update_ifr(mp, chip_idx);
      return pmb->sy6522.TIMER2_COUNTER.l;
    case via_reg::t2h_c:
      return pmb->sy6522.TIMER2_COUNTER.h;
    case via_reg::sr:
      return pmb->sy6522.SERIAL_SHIFT;
    case via_reg::acr:
      return pmb->sy6522.ACR;
    case via_reg::pcr:
      return pmb->sy6522.PCR;
    case via_reg::ifr:
      return pmb->sy6522.IFR;
    case via_reg::ier:
      return pmb->sy6522.IER | via_ifr_irq_flag;
    case via_reg::ora_no_handshake:
      return pmb->sy6522.ORA_NO_HS;
    default:
      return 0;
  }
}

auto mb_update_instance(MockingboardPeripheral_t* mp) -> void {
  if (g_full_speed) {
    return;
  }

  const uint32_t sample_rate = default_mockingboard_sample_rate;
  const double clock_hz = get_clock_hz(mp->host);

  double timer_period_val =
      (mp->timer_irq_active || (mp->chips.at(0).sy6522.IFR & ixr_timer1))
          ? static_cast<double>(mp->timer_period_6522)
          : (clock_hz / 60.0);

  if (timer_period_val <= 0.0) {
    timer_period_val = clock_hz / 60.0;
  }

  double irq_freq = clock_hz / timer_period_val;
  int num_samples =
      static_cast<int>(static_cast<double>(sample_rate) / irq_freq);

  if (num_samples <= 0) {
    return;
  }

  if (static_cast<size_t>(num_samples) > mb_max_samples_per_update) {
    num_samples = static_cast<int>(mb_max_samples_per_update);
  }

  for (size_t i = 0; i < chips_per_card; i++) {
    int16_t* voices[3];
    voices[0] = mp->voice_buffers.at(i * 3 + 0).data();
    voices[1] = mp->voice_buffers.at(i * 3 + 1).data();
    voices[2] = mp->voice_buffers.at(i * 3 + 2).data();
    ay8910_update_instance(&mp->chips.at(i).ay_chip, voices, num_samples,
                           static_cast<int>(clock_hz),
                           static_cast<int>(sample_rate));
  }

  if (mp->host != nullptr && mp->host->AudioPushChannels != nullptr) {
    const int16_t* channel_ptrs[voices_per_card];
    for (size_t v = 0; v < voices_per_card; ++v) {
      channel_ptrs[v] = mp->voice_buffers.at(v).data();
    }
    mp->host->AudioPushChannels(mp, channel_ptrs, voices_per_card,
                                static_cast<size_t>(num_samples));
  }
}

auto mb_update_cycles_instance(MockingboardPeripheral_t* mp,
                               uint32_t executed_cycles) -> void {
  if (mp->type == SoundCardType_t::none) {
    return;
  }

  uint64_t host_cycles = (mp->host != nullptr && mp->host->GetCycles != nullptr)
                             ? mp->host->GetCycles()
                             : 0;
  uint64_t current_cycle = host_cycles + executed_cycles;
  uint64_t cycles = 0;
  if (current_cycle >= mp->last_cumulative_cycles) {
    cycles = current_cycle - mp->last_cumulative_cycles;
    mp->last_cumulative_cycles = current_cycle;
  } else {
    mp->last_cumulative_cycles = current_cycle;
  }

  while (cycles > 0) {
    constexpr uint64_t max_clocks_u16 = 0xFFFF;
    constexpr uint16_t max_clocks_u16_val = 0xFFFF;
    uint16_t clocks = (cycles > max_clocks_u16) ? max_clocks_u16_val
                                                : static_cast<uint16_t>(cycles);
    cycles -= clocks;

    for (size_t i = 0; i < chips_per_card; i++) {
      auto* pmb = &mp->chips.at(i);
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

      if (!g_full_speed) {
        mb_update_instance(mp);
      }
    }
  }

  if (mp->mb_reg_accessed_flag) {
    mp->mb_inactive_cycle_count = 0;
    mp->mb_reg_accessed_flag = false;
    if (!g_full_speed) {
      mp->mb_active = true;
    }
    return;
  }

  if (mp->mb_inactive_cycle_count == 0) {
    mp->mb_inactive_cycle_count = get_cycles(mp->host);
    return;
  }

  if (get_cycles(mp->host) - mp->mb_inactive_cycle_count >
      static_cast<uint64_t>(get_clock_hz(mp->host)) /
          inactive_threshold_divisor) {
    mp->mb_active = false;
  }
}

auto mb_io_read(void* instance, uint16_t pc, uint16_t addr, uint8_t write,
                uint8_t val, uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)write;
  (void)val;
  if (instance == nullptr) {
    return mem_read_floating_bus(cycles_left);
  }
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  mb_update_cycles_instance(mp, cycles_left);
  uint8_t offset = addr & mb_io_addr_hi_mask;
  if (offset <= (sy6522a_offset + via_reg_mask)) {
    return sy6522_read_instance(mp, sy6522_device_a, offset & via_reg_mask);
  }
  if ((offset >= sy6522b_offset) &&
      (offset <= (sy6522b_offset + via_reg_mask))) {
    return sy6522_read_instance(mp, sy6522_device_b, offset & via_reg_mask);
  }
  return mem_read_floating_bus(cycles_left);
}

auto mb_io_write(void* instance, uint16_t pc, uint16_t addr, uint8_t write,
                 uint8_t val, uint32_t cycles_left) -> uint8_t {
  (void)pc;
  (void)write;
  if (instance == nullptr) {
    return 0;
  }
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
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

auto phasor_io(void* instance, uint16_t pc, uint16_t addr, uint8_t write,
               uint8_t val, uint32_t cycles_left) -> uint8_t {
  (void)pc;
  if (instance == nullptr) {
    return mem_read_floating_bus(cycles_left);
  }
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
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
    cs = ((addr & 0x80) != 0) ? 2 : 1;
  }

  uint8_t res = 0;
  if ((cs & 1) != 0) {
    if (write != 0) {
      sy6522_write_instance(mp, sy6522_device_a, addr & via_reg_mask, val);
    } else {
      res = sy6522_read_instance(mp, sy6522_device_a, addr & via_reg_mask);
    }
  }

  if ((cs & 2) != 0) {
    if (write != 0) {
      sy6522_write_instance(mp, sy6522_device_b, addr & via_reg_mask, val);
    } else {
      res = sy6522_read_instance(mp, sy6522_device_b, addr & via_reg_mask);
    }
  }

  return (write != 0) ? 0 : res;
}

auto mb_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr || host->RegisterIO == nullptr) {
    return nullptr;
  }
  auto new_mp = std::unique_ptr<MockingboardPeripheral_t>(
      new (std::nothrow) MockingboardPeripheral_t{});
  if (!new_mp) {
    return nullptr;
  }
  new_mp->host = host;
  new_mp->slot = slot;

  char type_str[mb_type_str_max] = {0};
  if (host->GetConfig != nullptr &&
      host->GetConfig("Mockingboard", "Type", type_str, sizeof(type_str)) &&
      strcmp(type_str, "Phasor") == 0) {
    new_mp->type = SoundCardType_t::phasor;
  }

  auto* handler =
      (new_mp->type == SoundCardType_t::phasor) ? phasor_io : nullptr;
#if ENABLE_ROM_MOCKINGBOARD
  if (host->RegisterCxROM != nullptr) {
    host->RegisterCxROM(slot, const_cast<uint8_t*>(g_rom_mockingboard_d));
  }
#endif
  host->RegisterIO(slot, handler, handler, mb_io_read, mb_io_write);

  return new_mp.release();
}

auto mb_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
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
    std::memset(&chip.sy6522, 0, sizeof(Sy6522_t));
    ay8910_reset_instance(&chip.ay_chip);
    chip.timer_status = 0;
    chip.ay_current_register = 0;
  }
}

auto mb_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  delete static_cast<MockingboardPeripheral_t*>(instance);
}

auto mb_abi_think(void* instance, uint32_t cycles) -> void {
  (void)cycles;
  if (instance == nullptr) {
    return;
  }
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  mb_update_cycles_instance(mp, 0);

  // If timers are inactive, force a 60Hz audio update to prevent buffer
  // starvation.
  const bool timers_active =
      mp->timer_irq_active || (mp->chips.at(0).sy6522.IFR & ixr_timer1);
  if (timers_active) {
    return;
  }

  const uint64_t cycles_since_last_update =
      get_cycles(mp->host) - mp->last_60hz;
  const uint64_t cycles_per_frame =
      static_cast<uint64_t>(get_clock_hz(mp->host)) / hz_60_divisor;

  if (cycles_since_last_update > cycles_per_frame) {
    mp->last_60hz = get_cycles(mp->host);
    mb_update_instance(mp);
  }
}

auto mb_abi_save_state(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus_t {
  if (size == nullptr) {
    return peripheral_error;
  }

  const size_t required = sizeof(MockingboardSaveState_t);
  if (buffer == nullptr) {
    *size = required;
    return peripheral_ok;
  }

  if (instance == nullptr || *size < required) {
    *size = required;
    return peripheral_error;
  }

  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);
  auto* ss = static_cast<MockingboardSaveState_t*>(buffer);
  std::memset(ss, 0, sizeof(MockingboardSaveState_t));

  ss->version = MOCKINGBOARD_STATE_VERSION;
  ss->struct_size = static_cast<uint32_t>(sizeof(MockingboardSaveState_t));

  for (int i = 0; i < chips_per_card; ++i) {
    const auto& src = mp->chips.at(static_cast<size_t>(i));
    auto& dst = ss->chips[i];
    dst.orb = src.sy6522.ORB;
    dst.ora = src.sy6522.ORA;
    dst.ddrb = src.sy6522.DDRB;
    dst.ddra = src.sy6522.DDRA;
    dst.t1_counter = src.sy6522.TIMER1_COUNTER.w;
    dst.t1_latch = src.sy6522.TIMER1_LATCH.w;
    dst.t2_counter = src.sy6522.TIMER2_COUNTER.w;
    dst.t2_latch = src.sy6522.TIMER2_LATCH.w;
    dst.serial_shift = src.sy6522.SERIAL_SHIFT;
    dst.acr = src.sy6522.ACR;
    dst.pcr = src.sy6522.PCR;
    dst.ifr = src.sy6522.IFR;
    dst.ier = src.sy6522.IER;
    dst.ora_no_hs = src.sy6522.ORA_NO_HS;

    for (size_t r = 0; r < MOCKINGBOARD_AY_REGS; ++r) {
      dst.ay_regs[r] = src.ay_chip.regs.at(r);
    }
    dst.count_a = src.ay_chip.count_a;
    dst.count_b = src.ay_chip.count_b;
    dst.count_c = src.ay_chip.count_c;
    dst.out_a = src.ay_chip.out_a;
    dst.out_b = src.ay_chip.out_b;
    dst.out_c = src.ay_chip.out_c;
    dst.out_n = src.ay_chip.out_n;
    dst.count_n = src.ay_chip.count_n;
    dst.rng = src.ay_chip.rng;
    dst.count_e = src.ay_chip.count_e;
    dst.envelope_step = src.ay_chip.envelope_step;
    dst.envelope_vol = src.ay_chip.envelope_vol;
    dst.env_holding = src.ay_chip.env_holding ? 1U : 0U;
    dst.ay_current_register = src.ay_current_register;
    dst.ay_number = src.ay_8910_number;
    dst.timer_status = src.timer_status;
    dst.count_accum = src.ay_chip.count_accum;
  }

  ss->timer_period_6522 = mp->timer_period_6522;
  ss->mb_timer_device = mp->mb_timer_device;
  ss->last_cumulative_cycles = mp->last_cumulative_cycles;
  ss->mb_inactive_cycle_count = mp->mb_inactive_cycle_count;
  ss->last_60hz = mp->last_60hz;
  ss->mb_reg_accessed_flag = mp->mb_reg_accessed_flag ? 1U : 0U;
  ss->mb_active = mp->mb_active ? 1U : 0U;
  ss->timer_irq_active = mp->timer_irq_active ? 1U : 0U;
  ss->timer1_irq_count = mp->timer1_irq_count;
  ss->card_type = (mp->type == SoundCardType_t::phasor)
                      ? static_cast<uint8_t>(mockingboard_type_phasor)
                      : static_cast<uint8_t>(mockingboard_type_mockingboard);
  ss->phasor_native = mp->phasor_native ? 1U : 0U;

  *size = required;
  return peripheral_ok;
}

auto mb_abi_load_state(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus_t {
  if (instance == nullptr || buffer == nullptr ||
      size != sizeof(MockingboardSaveState_t)) {
    return peripheral_error;
  }

  const auto* ss = static_cast<const MockingboardSaveState_t*>(buffer);
  if (ss->version != MOCKINGBOARD_STATE_VERSION ||
      ss->struct_size != sizeof(MockingboardSaveState_t)) {
    return peripheral_error;
  }

  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);

  for (int i = 0; i < chips_per_card; ++i) {
    auto& dst = mp->chips.at(static_cast<size_t>(i));
    const auto& src = ss->chips[i];
    dst.sy6522.ORB = src.orb;
    dst.sy6522.ORA = src.ora;
    dst.sy6522.DDRB = src.ddrb;
    dst.sy6522.DDRA = src.ddra;
    dst.sy6522.TIMER1_COUNTER.w = src.t1_counter;
    dst.sy6522.TIMER1_LATCH.w = src.t1_latch;
    dst.sy6522.TIMER2_COUNTER.w = src.t2_counter;
    dst.sy6522.TIMER2_LATCH.w = src.t2_latch;
    dst.sy6522.SERIAL_SHIFT = src.serial_shift;
    dst.sy6522.ACR = src.acr;
    dst.sy6522.PCR = src.pcr;
    dst.sy6522.IFR = src.ifr;
    dst.sy6522.IER = src.ier;
    dst.sy6522.ORA_NO_HS = src.ora_no_hs;

    for (size_t r = 0; r < MOCKINGBOARD_AY_REGS; ++r) {
      dst.ay_chip.regs.at(r) = src.ay_regs[r];
    }
    dst.ay_chip.count_a = src.count_a;
    dst.ay_chip.count_b = src.count_b;
    dst.ay_chip.count_c = src.count_c;
    dst.ay_chip.out_a = src.out_a;
    dst.ay_chip.out_b = src.out_b;
    dst.ay_chip.out_c = src.out_c;
    dst.ay_chip.out_n = src.out_n;
    dst.ay_chip.count_n = src.count_n;
    dst.ay_chip.rng = src.rng;
    dst.ay_chip.count_e = src.count_e;
    dst.ay_chip.envelope_step = src.envelope_step;
    dst.ay_chip.envelope_vol = src.envelope_vol;
    dst.ay_chip.env_holding = (src.env_holding != 0);
    dst.ay_current_register = src.ay_current_register;
    dst.ay_8910_number = src.ay_number;
    dst.timer_status = src.timer_status;
    dst.ay_chip.count_accum = src.count_accum;
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
  mp->type = (ss->card_type == mockingboard_type_phasor)
                 ? SoundCardType_t::phasor
                 : SoundCardType_t::mockingboard;
  mp->phasor_native = (ss->phasor_native != 0);

  return peripheral_ok;
}

auto mb_abi_command(void* instance, uint32_t cmd_id, const void* data,
                    size_t size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);

  switch (static_cast<MockingboardCmd_t>(cmd_id)) {
    case mockingboard_cmd_set_type: {
      if (data == nullptr || size < sizeof(uint8_t)) {
        return peripheral_error;
      }
      auto card_type = *static_cast<const uint8_t*>(data);
      if (card_type == mockingboard_type_phasor) {
        mp->type = SoundCardType_t::phasor;
      } else {
        mp->type = SoundCardType_t::mockingboard;
        mp->phasor_native = false;
      }
      if (mp->host != nullptr && mp->host->RegisterIO != nullptr) {
        auto* handler =
            (mp->type == SoundCardType_t::phasor) ? phasor_io : nullptr;
        mp->host->RegisterIO(mp->slot, handler, handler, mb_io_read,
                             mb_io_write);
      }
      return peripheral_ok;
    }
    case mockingboard_cmd_reset_audio: {
      for (auto& chip : mp->chips) {
        ay8910_reset_instance(&chip.ay_chip);
      }
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

auto mb_abi_query(void* instance, uint32_t cmd_id, void* out, size_t* out_size)
    -> PeripheralStatus_t {
  if (instance == nullptr || out_size == nullptr) {
    return peripheral_error;
  }
  auto* mp = static_cast<MockingboardPeripheral_t*>(instance);

  switch (cmd_id) {
    case mockingboard_query_status: {
      const size_t required = sizeof(MockingboardStatus_t);
      if (out == nullptr) {
        *out_size = required;
        return peripheral_ok;
      }
      if (*out_size < required) {
        *out_size = required;
        return peripheral_error;
      }
      auto* status = static_cast<MockingboardStatus_t*>(out);
      std::memset(status, 0, sizeof(MockingboardStatus_t));
      status->card_type =
          (mp->type == SoundCardType_t::phasor)
              ? static_cast<uint8_t>(mockingboard_type_phasor)
              : static_cast<uint8_t>(mockingboard_type_mockingboard);
      status->timer_irq_active = mp->timer_irq_active ? 1U : 0U;
      status->phasor_native = mp->phasor_native ? 1U : 0U;
      *out_size = required;
      return peripheral_ok;
    }
    case PERIPHERAL_QUERY_AUDIO_INFO: {
      const size_t required = sizeof(PeripheralAudioInfo_t);
      if (out == nullptr) {
        *out_size = required;
        return peripheral_ok;
      }
      if (*out_size < required) {
        *out_size = required;
        return peripheral_error;
      }
      auto* info = static_cast<PeripheralAudioInfo_t*>(out);
      info->sample_rate = default_mockingboard_sample_rate;
      info->num_channels = voices_per_card;
      const char* names[6] = {"AY0 Voice A", "AY0 Voice B", "AY0 Voice C",
                              "AY1 Voice A", "AY1 Voice B", "AY1 Voice C"};
      for (size_t i = 0; i < voices_per_card; ++i) {
        std::strncpy(info->channels[i].name, names[i],
                     sizeof(info->channels[i].name) - 1);
        info->channels[i].name[sizeof(info->channels[i].name) - 1] = '\0';
        if (i < 3) {
          info->channels[i].default_pan_left = 1.0f;
          info->channels[i].default_pan_right = 0.0f;
        } else {
          info->channels[i].default_pan_left = 0.0f;
          info->channels[i].default_pan_right = 1.0f;
        }
      }
      *out_size = required;
      return peripheral_ok;
    }
    default:
      return peripheral_incompatible;
  }
}

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
    .command = mb_abi_command,
    .query = mb_abi_query};

}  // namespace

extern "C" auto mockingboard_get_descriptor() -> Peripheral_t* {
  return &g_mockingboard_peripheral;
}

PERIPHERAL_REGISTER(g_mockingboard_peripheral)
