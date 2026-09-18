// SPDX-License-Identifier: GPL-2.0-only

#include "apple2/peripherals/mockingboard/Mockingboard.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "apple2/chips/6522.h"
#include "apple2/chips/AY8910.h"
#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Audio.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/mockingboard/MockingboardCommands.h"

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers) Justification: Hardware bus bit assignments and register widths

// Version 1 fixed every one of these offsets, and a state written before this
// card was rewritten still has to load. A field that moved would be read out of
// a neighbour's bytes and no test would necessarily notice, so the layout is
// nailed down here rather than trusted to declaration order.
static_assert(sizeof(MockingboardSaveState_t) == 232,
              "MockingboardSaveState_t must be exactly 232 bytes");
static_assert(sizeof(MockingboardChipSaveState_t) == 88,
              "MockingboardChipSaveState_t must be exactly 88 bytes");

static_assert(offsetof(MockingboardSaveState_t, version) == 0);
static_assert(offsetof(MockingboardSaveState_t, struct_size) == 4);
static_assert(offsetof(MockingboardSaveState_t, chips) == 8);
static_assert(offsetof(MockingboardSaveState_t, psg_remainder) == 184);

static_assert(offsetof(MockingboardChipSaveState_t, orb) == 0);
static_assert(offsetof(MockingboardChipSaveState_t, ora) == 1);
static_assert(offsetof(MockingboardChipSaveState_t, ddrb) == 2);
static_assert(offsetof(MockingboardChipSaveState_t, ddra) == 3);
static_assert(offsetof(MockingboardChipSaveState_t, t1_counter) == 4);
static_assert(offsetof(MockingboardChipSaveState_t, t1_latch) == 6);
static_assert(offsetof(MockingboardChipSaveState_t, t2_counter) == 8);
static_assert(offsetof(MockingboardChipSaveState_t, t2_latch) == 10);
static_assert(offsetof(MockingboardChipSaveState_t, serial_shift) == 12);
static_assert(offsetof(MockingboardChipSaveState_t, acr) == 13);
static_assert(offsetof(MockingboardChipSaveState_t, pcr) == 14);
static_assert(offsetof(MockingboardChipSaveState_t, ifr) == 15);
static_assert(offsetof(MockingboardChipSaveState_t, ier) == 16);
static_assert(offsetof(MockingboardChipSaveState_t, ora_no_hs) == 17);
static_assert(offsetof(MockingboardChipSaveState_t, via_flags) == 18);
static_assert(offsetof(MockingboardChipSaveState_t, ay_regs) == 20);
static_assert(offsetof(MockingboardChipSaveState_t, count_a) == 36);
static_assert(offsetof(MockingboardChipSaveState_t, count_b) == 38);
static_assert(offsetof(MockingboardChipSaveState_t, count_c) == 40);
static_assert(offsetof(MockingboardChipSaveState_t, out_a) == 42);
static_assert(offsetof(MockingboardChipSaveState_t, out_b) == 43);
static_assert(offsetof(MockingboardChipSaveState_t, out_c) == 44);
static_assert(offsetof(MockingboardChipSaveState_t, out_n) == 45);
static_assert(offsetof(MockingboardChipSaveState_t, count_n) == 48);
static_assert(offsetof(MockingboardChipSaveState_t, rng) == 52);
static_assert(offsetof(MockingboardChipSaveState_t, count_e) == 56);
static_assert(offsetof(MockingboardChipSaveState_t, envelope_step) == 60);
static_assert(offsetof(MockingboardChipSaveState_t, envelope_vol) == 64);
static_assert(offsetof(MockingboardChipSaveState_t, env_holding) == 65);
static_assert(offsetof(MockingboardChipSaveState_t, ay_current_register) == 66);
static_assert(offsetof(MockingboardChipSaveState_t, env_attack) == 68);

constexpr size_t chips_per_card = 2;
constexpr size_t voices_per_chip = 3;
constexpr size_t voices_per_card = voices_per_chip * chips_per_card;
constexpr int8_t mb_default_slot = 4;

// The card decodes /IO SELECT and leaves A4 through A6 unconnected, so the
// whole $Cn00-$CnFF page aliases onto the two VIAs in sixteen-byte steps:
// $Cn00-$Cn7F is VIA A and $Cn80-$CnFF is VIA B. There is no unmapped offset
// and therefore no floating-bus read anywhere on this card.
constexpr uint16_t via_select_bit = 0x80;

// The card clocks both PSGs from the slot's phase-0 line, and the AY divides
// by eight before its tone, noise and envelope counters. One AY sample is
// therefore eight 6502 cycles: 127,560.5 Hz on NTSC.
constexpr uint32_t cycles_per_ay_tick = 8;

// A frame at emulation_speed_max is 85,150 ticks, so this is 84 chunks of a
// 24 KiB scratch that lives in the instance and never on the stack.
constexpr size_t scratch_ticks = 1024;

// The AY is unipolar and the real card's output stage is AC-coupled. Without
// that coupling three voices per side sit on the positive rail and never
// reach the negative one. tau = 1000 ticks puts the corner at 20.3 Hz at
// 127.6 kHz.
constexpr double dc_blocker_tau_ticks = 1000.0;
constexpr double dc_blocker_coefficient = 1.0 - (1.0 / dc_blocker_tau_ticks);

// A one-pole decay never actually reaches zero, so without a floor the card
// emits a trickle forever and can never say it is silent. An output LSB after
// the mixer's three-way fan-in gain is 9.2e-5, so snapping a ninth of that
// away cannot truncate anything audible.
constexpr double dc_silence_epsilon = 1.0e-5;

// ORB bits 2..0 drive the AY control bus; BC2 is tied high and /RESET is
// active low.
namespace ay_bus {
constexpr uint8_t bc1 = 0x01;
constexpr uint8_t bdir = 0x02;
constexpr uint8_t reset_n = 0x04;
}  // namespace ay_bus

namespace via_flag {
constexpr uint8_t t1_fired = 0x01;
constexpr uint8_t t2_fired = 0x02;
constexpr uint8_t pb7 = 0x04;
constexpr uint8_t t1_phase_shift = 3;
constexpr uint8_t t2_phase_shift = 5;
constexpr uint8_t phase_mask = 0x03;
}  // namespace via_flag

constexpr const char* channel_names[voices_per_card] = {
    "AY0 Voice A", "AY0 Voice B", "AY0 Voice C",
    "AY1 Voice A", "AY1 Voice B", "AY1 Voice C"};

struct DcBlock_t {
  double previous_input = 0.0;
  double previous_output = 0.0;
};

struct Mockingboard_t {
  std::array<Via6522_t, chips_per_card> via = {};
  std::array<Ay8910_t, chips_per_card> ay = {};
  std::array<uint8_t, chips_per_card> ay_latched_register = {};
  std::array<DcBlock_t, voices_per_card> dc = {};
  std::array<std::array<float, scratch_ticks>, voices_per_card> scratch = {};
  // CPU cycles of this slice already consumed, and CPU cycles not yet worth a
  // whole AY tick. Together they are why no cycle is ever counted twice.
  uint32_t synced = 0;
  uint32_t psg_remainder = 0;
  bool irq_line = false;
  int slot = 0;
  HostInterface_t* host = nullptr;
};

auto update_irq(Mockingboard_t* mb) -> void {
  const bool line = via_irq(&mb->via[0]) || via_irq(&mb->via[1]);
  if (line == mb->irq_line) {
    return;
  }
  mb->irq_line = line;
  if (mb->host != nullptr && mb->host->AssertIrq != nullptr) {
    mb->host->AssertIrq(mb->slot, line);
  }
}

auto run_dc_block(DcBlock_t* f, float* buffer, size_t count) -> bool {
  bool any_signal = false;
  for (size_t i = 0; i < count; ++i) {
    const double input = buffer[i];
    double output = input - f->previous_input +
                    (dc_blocker_coefficient * f->previous_output);
    if (output > -dc_silence_epsilon && output < dc_silence_epsilon) {
      output = 0.0;
    }
    f->previous_input = input;
    f->previous_output = output;
    buffer[i] = static_cast<float>(output);
    if (output != 0.0) {
      any_signal = true;
    }
  }
  return any_signal;
}

auto render_chunk(Mockingboard_t* mb, size_t count) -> void {
  for (size_t chip = 0; chip < chips_per_card; ++chip) {
    std::array<float*, AY8910_NUM_VOICES> voices = {
        {mb->scratch[(chip * voices_per_chip) + 0].data(),
         mb->scratch[(chip * voices_per_chip) + 1].data(),
         mb->scratch[(chip * voices_per_chip) + 2].data()}};
    ay8910_step(&mb->ay[chip], count, voices.data(), scratch_ticks);
  }

  std::array<const float*, voices_per_card> channels = {};
  bool any_signal = false;
  for (size_t v = 0; v < voices_per_card; ++v) {
    any_signal =
        run_dc_block(&mb->dc[v], mb->scratch[v].data(), count) || any_signal;
    channels[v] = mb->scratch[v].data();
  }

  if (!any_signal) {
    return;
  }
  if (mb->host != nullptr && mb->host->AudioPushChannels != nullptr) {
    mb->host->AudioPushChannels(mb, channels.data(), voices_per_card, count);
  }
}

auto advance(Mockingboard_t* mb, uint32_t cycles) -> void {
  if (cycles == 0) {
    return;
  }

  bool changed = false;
  for (auto& v : mb->via) {
    changed = via_step(&v, cycles) || changed;
  }
  if (changed) {
    update_irq(mb);
  }

  const uint64_t total = static_cast<uint64_t>(mb->psg_remainder) + cycles;
  uint64_t ticks = total / cycles_per_ay_tick;
  mb->psg_remainder = static_cast<uint32_t>(total % cycles_per_ay_tick);

  // Chunking keeps any cycle count correct without ever clamping or dropping
  // one, which would desynchronize the PSGs from the VIAs.
  while (ticks > 0) {
    const size_t count =
        (ticks < scratch_ticks) ? static_cast<size_t>(ticks) : scratch_ticks;
    render_chunk(mb, count);
    ticks -= count;
  }
}

// Brings the card to the current instruction so a register read returns the
// value the hardware would hold on this very cycle. Mad Effect 2's boot code
// reads T1C-L twice eight cycles apart and refuses to run unless the
// difference is exactly 0xF8.
// The mark only ever moves forward. The 6502 is monotonic within a slice, but
// a caller that is not would otherwise rewind it and have the cycles between
// the two marks charged to the card a second time.
auto sync_to(Mockingboard_t* mb, uint32_t executed_cycles) -> void {
  if (executed_cycles <= mb->synced) {
    return;
  }
  advance(mb, executed_cycles - mb->synced);
  mb->synced = executed_cycles;
}

auto ay_bus_cycle(Mockingboard_t* mb, size_t chip, uint8_t orb) -> void {
  Ay8910_t& psg = mb->ay[chip];
  if ((orb & ay_bus::reset_n) == 0) {
    ay8910_reset(&psg);
    mb->ay_latched_register[chip] = 0;
    return;
  }

  const bool bdir = (orb & ay_bus::bdir) != 0;
  const bool bc1 = (orb & ay_bus::bc1) != 0;
  const uint8_t port_a = mb->via[chip].ora;

  if (bdir && bc1) {
    // A9 and A8 are grounded on this card, so an address above 15 selects no
    // chip and leaves the previous register latched.
    if (port_a < AY8910_NUM_REGISTERS) {
      mb->ay_latched_register[chip] = port_a;
    }
    return;
  }
  if (bdir) {
    ay8910_write(&psg, mb->ay_latched_register[chip], port_a);
    return;
  }
  if (bc1) {
    // The chip drives the port A pins the 6502 is not driving, so the next
    // ORA read returns the register.
    const uint8_t driven = mb->via[chip].ddra;
    const uint8_t value = psg.regs[mb->ay_latched_register[chip]];
    mb->via[chip].ora =
        static_cast<uint8_t>((port_a & driven) | (value & ~driven));
  }
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) Justification: The I/O handler signature is fixed by the peripheral ABI
auto cx_read(void* instance, uint16_t pc, uint16_t addr, uint8_t write,
             uint8_t val, uint32_t executed_cycles) -> uint8_t {
  (void)pc;
  (void)write;
  (void)val;
  auto* mb = static_cast<Mockingboard_t*>(instance);
  if (mb == nullptr) {
    return 0;
  }
  sync_to(mb, executed_cycles);
  const size_t chip = ((addr & via_select_bit) != 0) ? 1 : 0;
  const uint8_t value = via_read(&mb->via[chip], static_cast<uint8_t>(addr));
  // A counter or flag access changes the interrupt state, and a slice is a
  // whole video field: deferring the deassert to think() would re-enter the
  // 6502's handler on every RTI.
  update_irq(mb);
  return value;
}

auto cx_write(void* instance, uint16_t pc, uint16_t addr, uint8_t write,
              uint8_t val, uint32_t executed_cycles) -> uint8_t {
  (void)pc;
  (void)write;
  auto* mb = static_cast<Mockingboard_t*>(instance);
  if (mb == nullptr) {
    return 0;
  }
  sync_to(mb, executed_cycles);
  const size_t chip = ((addr & via_select_bit) != 0) ? 1 : 0;
  const uint8_t reg = static_cast<uint8_t>(addr) & via_reg::mask;
  via_write(&mb->via[chip], reg, val);
  // ORA only sets the data the next ORB strobe will use; running the protocol
  // on an ORA write too would double-write whenever software parks ORB at the
  // write function.
  if (reg == via_reg::orb) {
    ay_bus_cycle(mb, chip, val);
  }
  update_irq(mb);
  return 0;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

auto mb_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr || host->RegisterIO == nullptr) {
    return nullptr;
  }
  auto card =
      std::unique_ptr<Mockingboard_t>(new (std::nothrow) Mockingboard_t{});
  if (!card) {
    return nullptr;
  }
  card->host = host;
  card->slot = slot;
  host->RegisterIO(slot, nullptr, nullptr, cx_read, cx_write);
  return card.release();
}

auto mb_abi_reset(void* instance) -> void {
  auto* mb = static_cast<Mockingboard_t*>(instance);
  if (mb == nullptr) {
    return;
  }
  for (auto& v : mb->via) {
    via_reset(&v);
  }
  for (auto& psg : mb->ay) {
    ay8910_reset(&psg);
  }
  for (auto& latched : mb->ay_latched_register) {
    latched = 0;
  }
  for (auto& f : mb->dc) {
    f = DcBlock_t{};
  }
  mb->synced = 0;
  mb->psg_remainder = 0;
  mb->irq_line = false;
  // /RES drives the slot's interrupt line inactive; that is a bus event, not a
  // state-change notification, so it is reported whether or not it changed.
  if (mb->host != nullptr && mb->host->AssertIrq != nullptr) {
    mb->host->AssertIrq(mb->slot, false);
  }
}

auto mb_abi_shutdown(void* instance) -> void {
  delete static_cast<Mockingboard_t*>(instance);
}

auto mb_abi_think(void* instance, uint32_t cycles) -> void {
  auto* mb = static_cast<Mockingboard_t*>(instance);
  if (mb == nullptr) {
    return;
  }
  if (cycles > mb->synced) {
    advance(mb, cycles - mb->synced);
  }
  mb->synced = 0;
}

auto pack_via_flags(const Via6522_t& v) -> uint8_t {
  uint8_t flags = 0;
  if (v.t1_fired) {
    flags |= via_flag::t1_fired;
  }
  if (v.t2_fired) {
    flags |= via_flag::t2_fired;
  }
  if (v.pb7) {
    flags |= via_flag::pb7;
  }
  flags |= static_cast<uint8_t>(static_cast<uint8_t>(v.t1_phase)
                                << via_flag::t1_phase_shift);
  flags |= static_cast<uint8_t>(static_cast<uint8_t>(v.t2_phase)
                                << via_flag::t2_phase_shift);
  return flags;
}

auto unpack_phase(uint8_t flags, uint8_t shift) -> ViaTimerPhase_t {
  switch ((flags >> shift) & via_flag::phase_mask) {
    case 1:
      return ViaTimerPhase_t::load_delay;
    case 2:
      return ViaTimerPhase_t::reload_pending;
    default:
      return ViaTimerPhase_t::running;
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

  auto* mb = static_cast<Mockingboard_t*>(instance);
  auto* ss = static_cast<MockingboardSaveState_t*>(buffer);
  std::memset(ss, 0, required);

  ss->version = MOCKINGBOARD_STATE_VERSION;
  ss->struct_size = static_cast<uint32_t>(required);

  for (size_t i = 0; i < chips_per_card; ++i) {
    const Via6522_t& v = mb->via[i];
    const Ay8910_t& psg = mb->ay[i];
    MockingboardChipSaveState_t& dst = ss->chips[i];

    dst.orb = v.orb;
    dst.ora = v.ora;
    dst.ddrb = v.ddrb;
    dst.ddra = v.ddra;
    dst.t1_counter = v.t1_counter;
    dst.t1_latch = v.t1_latch;
    dst.t2_counter = v.t2_counter;
    dst.t2_latch = v.t2_latch;
    dst.serial_shift = v.shift_register;
    dst.acr = v.acr;
    dst.pcr = v.pcr;
    dst.ifr = v.ifr;
    dst.ier = v.ier;
    dst.ora_no_hs = v.ora_no_handshake;
    dst.via_flags = pack_via_flags(v);

    for (size_t r = 0; r < MOCKINGBOARD_AY_REGS; ++r) {
      dst.ay_regs[r] = psg.regs[r];
    }
    dst.count_a = psg.count_a;
    dst.count_b = psg.count_b;
    dst.count_c = psg.count_c;
    dst.out_a = psg.out_a;
    dst.out_b = psg.out_b;
    dst.out_c = psg.out_c;
    dst.out_n = psg.out_n;
    dst.count_n = psg.count_n;
    dst.rng = psg.rng;
    dst.count_e = psg.count_e;
    dst.envelope_step = psg.envelope_step;
    dst.envelope_vol = psg.envelope_vol;
    dst.env_holding = psg.env_holding ? 1U : 0U;
    dst.env_attack = psg.env_attack ? 1U : 0U;
    dst.ay_current_register = mb->ay_latched_register[i];
  }

  ss->psg_remainder = mb->psg_remainder;

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

  auto* mb = static_cast<Mockingboard_t*>(instance);

  for (size_t i = 0; i < chips_per_card; ++i) {
    const MockingboardChipSaveState_t& src = ss->chips[i];
    Via6522_t& v = mb->via[i];
    Ay8910_t& psg = mb->ay[i];

    v.orb = src.orb;
    v.ora = src.ora;
    v.ddrb = src.ddrb;
    v.ddra = src.ddra;
    v.t1_counter = src.t1_counter;
    v.t1_latch = src.t1_latch;
    v.t2_counter = src.t2_counter;
    v.t2_latch = src.t2_latch;
    v.shift_register = src.serial_shift;
    v.acr = src.acr;
    v.pcr = src.pcr;
    v.ifr = src.ifr & via_ifr::mask;
    v.ier = src.ier & via_ifr::mask;
    v.ora_no_handshake = src.ora_no_hs;
    v.t1_fired = (src.via_flags & via_flag::t1_fired) != 0;
    v.t2_fired = (src.via_flags & via_flag::t2_fired) != 0;
    v.pb7 = (src.via_flags & via_flag::pb7) != 0;
    v.t1_phase = unpack_phase(src.via_flags, via_flag::t1_phase_shift);
    v.t2_phase = unpack_phase(src.via_flags, via_flag::t2_phase_shift);

    // Going in through the chip's own write path is what masks each register
    // to its data-sheet width, so a hostile blob cannot widen one.
    for (size_t r = 0; r < MOCKINGBOARD_AY_REGS; ++r) {
      ay8910_write(&psg, static_cast<uint8_t>(r), src.ay_regs[r]);
    }
    psg.count_a = src.count_a;
    psg.count_b = src.count_b;
    psg.count_c = src.count_c;
    psg.out_a = src.out_a;
    psg.out_b = src.out_b;
    psg.out_c = src.out_c;
    psg.out_n = src.out_n;
    psg.count_n = src.count_n;
    psg.rng = (src.rng != 0) ? src.rng : 1U;
    psg.count_e = src.count_e;
    psg.envelope_step = src.envelope_step & 0x0F;
    psg.env_holding = src.env_holding != 0;
    psg.env_attack = src.env_attack != 0;
    psg.envelope_vol = static_cast<uint8_t>(
        psg.env_attack ? psg.envelope_step : (15U - psg.envelope_step));

    mb->ay_latched_register[i] = static_cast<uint8_t>(
        src.ay_current_register & (AY8910_NUM_REGISTERS - 1));
  }

  mb->psg_remainder = ss->psg_remainder % cycles_per_ay_tick;
  mb->synced = 0;
  // Transient: from a full-scale step the coupling settles below an output
  // LSB in about 50 ms, so it is cheaper to restart it than to carry it.
  for (auto& f : mb->dc) {
    f = DcBlock_t{};
  }
  update_irq(mb);

  return peripheral_ok;
}

auto query_audio_info(void* out, size_t* out_size) -> PeripheralStatus_t {
  const size_t required = sizeof(PeripheralAudioInfo_t);
  if (out == nullptr) {
    *out_size = required;
    return peripheral_ok;
  }
  if (*out_size < required) {
    *out_size = required;
    return peripheral_error;
  }

  std::memset(out, 0, required);
  auto& info = *static_cast<PeripheralAudioInfo_t*>(out);
  info.time_base = peripheral_audio_cpu_clocked;
  info.cycle_divisor = cycles_per_ay_tick;
  info.num_channels = voices_per_card;
  // A voice in 0..1 through the coupling is bounded by -1..1 and both bounds
  // are reachable: a step from silence to full volume, and the charge the
  // same step leaves behind when it goes away.
  info.peak_magnitude = 1.0F;

  for (size_t i = 0; i < voices_per_card; ++i) {
    std::strncpy(info.channels[i].name, channel_names[i],
                 sizeof(info.channels[i].name) - 1);
    const bool left = i < voices_per_chip;
    info.channels[i].default_pan_left = left ? 1.0F : 0.0F;
    info.channels[i].default_pan_right = left ? 0.0F : 1.0F;
  }

  *out_size = required;
  return peripheral_ok;
}

auto mb_abi_query(void* instance, uint32_t cmd_id, void* out, size_t* out_size)
    -> PeripheralStatus_t {
  if (instance == nullptr || out_size == nullptr) {
    return peripheral_error;
  }
  if (cmd_id == PERIPHERAL_QUERY_AUDIO_INFO) {
    return query_audio_info(out, out_size);
  }
  return peripheral_incompatible;
}

static const Peripheral_t g_mockingboard_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.mockingboard",
    .name = "Mockingboard",
    .description = "Sweet Micro Systems Mockingboard (2x 6522, 2x AY-3-8910)",
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
    .query = mb_abi_query};

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

}  // namespace

// peripheral_register and ActivePeripheral_t::api still take a mutable
// Peripheral_t*, so the immutable descriptor is cast the same way
// PERIPHERAL_REGISTER casts it.
extern "C" auto mockingboard_get_descriptor() -> Peripheral_t* {
  return const_cast<Peripheral_t*>(&g_mockingboard_peripheral);
}

PERIPHERAL_REGISTER(g_mockingboard_peripheral)
