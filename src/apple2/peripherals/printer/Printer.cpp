// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/printer/Printer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/printer/PrinterCommands.h"

namespace {

// Apple's PROM 341-0005 "Printer Card I Firmware P1-2" (Woz 11/1/77, revised
// 3/17/78 by Huston and Sander), the firmware of the Apple Parallel Printer
// Interface Card A2B0002, as listed in Appendix A of the 1982 Apple II
// Parallel Interface Card manual (A2L0045), whose SW6-off firmware is this
// same image. res/roms/Parallel.rom holds the same 256 bytes; the build pins
// that file's SHA-1, and the suite checks the page the card registers against
// its own transcription of it. The two entry points are $Cn00 (initialise the
// screen holes, then print) and $Cn02 (print); the only card access in the
// image is the STA $C080,Y at $Cn84, and the bytes at $Cn80 (90 FE) and $Cn82
// (B0 FE) are the "wait for ready" images that the card presents in place of
// $CnC0 and $CnC2 while the printer has not acknowledged.
constexpr size_t page_size = 0x100;
const std::array<uint8_t, page_size> printer_rom = {{
    0x18, 0xb0, 0x38, 0x48, 0x8a, 0x48, 0x98, 0x48, 0x08, 0x78, 0x20, 0x58,
    0xff, 0xba, 0x68, 0x68, 0x68, 0x68, 0xa8, 0xca, 0x9a, 0x68, 0x28, 0xaa,
    0x90, 0x38, 0xbd, 0xb8, 0x05, 0x10, 0x19, 0x98, 0x29, 0x7f, 0x49, 0x30,
    0xc9, 0x0a, 0x90, 0x3b, 0xc9, 0x78, 0xb0, 0x29, 0x49, 0x3d, 0xf0, 0x21,
    0x98, 0x29, 0x9f, 0x9d, 0x38, 0x06, 0x90, 0x7e, 0xbd, 0xb8, 0x06, 0x30,
    0x14, 0xa5, 0x24, 0xdd, 0x38, 0x07, 0xb0, 0x0d, 0xc9, 0x11, 0xb0, 0x09,
    0x09, 0xf0, 0x3d, 0x38, 0x07, 0x65, 0x24, 0x85, 0x24, 0x4a, 0x38, 0xb0,
    0x6d, 0x18, 0x6a, 0x3d, 0xb8, 0x06, 0x90, 0x02, 0x49, 0x81, 0x9d, 0xb8,
    0x06, 0xd0, 0x53, 0xa0, 0x0a, 0x7d, 0x38, 0x05, 0x88, 0xd0, 0xfa, 0x9d,
    0xb8, 0x04, 0x9d, 0x38, 0x05, 0x38, 0xb0, 0x43, 0xc5, 0x24, 0x90, 0x3a,
    0x68, 0xa8, 0x68, 0xaa, 0x68, 0x4c, 0xf0, 0xfd, 0x90, 0xfe, 0xb0, 0xfe,
    0x99, 0x80, 0xc0, 0x90, 0x37, 0x49, 0x07, 0xa8, 0x49, 0x0a, 0x0a, 0xd0,
    0x06, 0xb8, 0x85, 0x24, 0x9d, 0x38, 0x07, 0xbd, 0xb8, 0x06, 0x4a, 0x70,
    0x02, 0xb0, 0x23, 0x0a, 0x0a, 0xa9, 0x27, 0xb0, 0xcf, 0xbd, 0x38, 0x07,
    0xfd, 0xb8, 0x04, 0xc9, 0xf8, 0x90, 0x03, 0x69, 0x27, 0xac, 0xa9, 0x00,
    0x85, 0x24, 0x18, 0x7e, 0xb8, 0x05, 0x68, 0xa8, 0x68, 0xaa, 0x68, 0x60,
    0x90, 0x27, 0xb0, 0x00, 0x10, 0x11, 0xa9, 0x89, 0x9d, 0x38, 0x06, 0x9d,
    0xb8, 0x06, 0xa9, 0x28, 0x9d, 0xb8, 0x04, 0xa9, 0x02, 0x85, 0x36, 0x98,
    0x5d, 0x38, 0x06, 0x0a, 0xf0, 0x90, 0x5e, 0xb8, 0x05, 0x98, 0x48, 0x8a,
    0x0a, 0x0a, 0x0a, 0x0a, 0xa8, 0xbd, 0x38, 0x07, 0xc5, 0x24, 0x68, 0xb0,
    0x05, 0x48, 0x29, 0x80, 0x09, 0x20, 0x2c, 0x58, 0xff, 0xf0, 0x03, 0xfe,
    0x38, 0x07, 0x70, 0x84,
}};

constexpr int min_slot = 1;
constexpr int max_slot = 7;
constexpr size_t wait_image_source = 0x80;
constexpr size_t wait_image_target = 0xC0;
constexpr size_t wait_image_length = 0x40;

struct PrinterCard_t {
  std::array<uint8_t, page_size> waiting_page{};
  HostInterface_t* host = nullptr;
  void* sink = nullptr;
  int slot = 0;
  uint8_t data_latch = 0;
  bool waiting = false;
};

// While the printer has not acknowledged, the card's 74LS00 forces PROM A6
// high whenever A7 is high, so a fetch from $CnC0-$CnFF returns the byte at
// the same offset in $Cn80-$CnBF and $Cn00-$CnBF are untouched
// (1978 manual A2L0004X, Section VI, Figures 8-10; the listing's "PROM
// ADDRESSING" table). The firmware's BCC PRNT1 at $CnC0 and BCS *+2 at $CnC2
// then read 90 FE and B0 FE, branches to themselves, and spin until the
// acknowledge arrives. Source, target and length are all inside one 256-byte
// page by their definitions above, so the copy cannot run off either array.
auto build_waiting_page(PrinterCard_t* card) -> void {
  static_assert(wait_image_source + wait_image_length == wait_image_target,
                "the altered half begins where the images end");
  static_assert(wait_image_target + wait_image_length == page_size,
                "the altered half is the top of the page");
  std::copy(printer_rom.begin(), printer_rom.end(), card->waiting_page.begin());
  std::copy_n(printer_rom.begin() + wait_image_source, wait_image_length,
              card->waiting_page.begin() + wait_image_target);
}

// The card's busy state is the level the sink reports, sampled after each
// strobe and again while waiting; the page the 6502 sees follows it, and is
// re-registered only when it changes. On the A2B0002 busy is the flip-flop
// FF2, set by DEVICE SELECT and cleared by the printer's acknowledge edge or
// by RESET (1978 manual, Section VI), which differs from this model in three
// ways: a ready sink releases the wait where the hardware needs an edge; the
// byte the hardware strobes into a switched-off printer after Ctrl-Reset
// (RESET clears FF2, but the 74LS174 register's clear pin is tied high, so
// the next STA goes out before the firmware parks again) does not occur here,
// since there is no flip-flop for reset to clear; and were the dummy read of
// STA abs,Y ever emulated, its DEVICE SELECT one cycle before the store would
// be superseded by the store's, as the card's strobe generator supersedes it
// ("an indexed store operation from the 6502 will cause a false DEV the cycle
// prior to the legitimate store operation", same section).
auto follow_sink_readiness(PrinterCard_t* card) -> void {
  const bool waiting = !card->host->SinkReady(card->sink);
  if (waiting == card->waiting) {
    return;
  }
  card->waiting = waiting;
  card->host->RegisterCxROM(
      card->slot, waiting ? card->waiting_page.data() : printer_rom.data());
  if (waiting && card->host->Log != nullptr) {
    card->host->Log(card, log_warn,
                    "printer in slot %d is not ready; the machine is waiting "
                    "as it would with the printer off\n",
                    card->slot);
  }
}

// The A2B0002 decodes DEVICE SELECT alone: R/W is not wired to it, so any
// access to its sixteen addresses clocks the data bus into the 74LS174 and
// 74LS298 register, strobes the printer and sets FF2, in that one cycle (1978
// manual, Section VI and Figure 9). A write latches the byte the 6502 drove;
// a read finds the bus undriven, latches whatever the video scanner left on
// it, and returns that same byte, because the card has no bus driver. The
// byte goes out before anyone asks whether the printer was ready, so a
// printer that is off swallows one byte and the machine parks on the next.
auto printer_io_access(void* instance, uint16_t program_counter,
                       uint16_t memory_address, uint8_t is_write,
                       uint8_t data_value, uint32_t executed_cycles)
    -> uint8_t {
  (void)program_counter;
  (void)memory_address;
  if (instance == nullptr) {
    return 0;
  }
  auto* card = static_cast<PrinterCard_t*>(instance);
  const uint8_t byte =
      is_write != 0 ? data_value : card->host->ReadFloatingBus(executed_cycles);
  card->data_latch = byte;
  card->host->SinkWrite(card->sink, byte);
  follow_sink_readiness(card);
  return byte;
}

// Without a ROM PR#n never reaches the firmware, without I/O the firmware's
// store reaches nothing, without the bus a read has no byte to latch, and
// without the sink the bytes have nowhere to go: better no card than a
// phantom one, and the log says which member was missing.
auto missing_host_member(const HostInterface_t* host) -> const char* {
  if (host->RegisterIO == nullptr) {
    return "RegisterIO";
  }
  if (host->RegisterCxROM == nullptr) {
    return "RegisterCxROM";
  }
  if (host->ReadFloatingBus == nullptr) {
    return "ReadFloatingBus";
  }
  if (host->SinkOpen == nullptr) {
    return "SinkOpen";
  }
  if (host->SinkWrite == nullptr) {
    return "SinkWrite";
  }
  if (host->SinkReady == nullptr) {
    return "SinkReady";
  }
  if (host->SinkClose == nullptr) {
    return "SinkClose";
  }
  return nullptr;
}

auto printer_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr) {
    return nullptr;
  }
  const char* missing = missing_host_member(host);
  if (missing != nullptr) {
    if (host->Log != nullptr) {
      host->Log(nullptr, log_error,
                "Printer card in slot %d: the host offers no %s\n", slot,
                missing);
    }
    return nullptr;
  }
  if (slot < min_slot || slot > max_slot) {
    if (host->Log != nullptr) {
      host->Log(nullptr, log_error,
                "Printer card in slot %d: an expansion card sits in slots 1 "
                "to 7\n",
                slot);
    }
    return nullptr;
  }

  auto card =
      std::unique_ptr<PrinterCard_t>(new (std::nothrow) PrinterCard_t());
  if (!card) {
    return nullptr;
  }
  card->host = host;
  card->slot = slot;
  card->sink = host->SinkOpen(card.get(), slot, peripheral_sink_printer);
  if (card->sink == nullptr) {
    if (host->Log != nullptr) {
      host->Log(nullptr, log_error,
                "Printer card in slot %d: the host has no printer sink for "
                "the slot\n",
                slot);
    }
    return nullptr;
  }
  build_waiting_page(card.get());

  host->RegisterCxROM(slot, printer_rom.data());
  host->RegisterIO(slot, printer_io_access, printer_io_access, nullptr,
                   nullptr);

  return card.release();
}

// RESET on the A2B0002 clears the busy flip-flop and nothing else: the data
// register's clear pin is tied to +5 V through R3 (1978 manual, Figure 10),
// so the last byte stays on the printer's data lines. This model keeps the
// last byte too, and has no flip-flop to clear: a wait in progress stays in
// place until the sink is ready.
auto printer_abi_reset(void* instance) -> void { (void)instance; }

auto printer_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  std::unique_ptr<PrinterCard_t> card(static_cast<PrinterCard_t*>(instance));
  card->host->SinkClose(card->sink);
}

// A parked machine fetches nothing but the wait image, so the card itself has
// to notice the printer coming back; one readiness poll per call while
// waiting, and nothing at all otherwise.
auto printer_abi_think(void* instance, uint32_t elapsed_cycles) -> void {
  (void)elapsed_cycles;
  if (instance == nullptr) {
    return;
  }
  auto* card = static_cast<PrinterCard_t*>(instance);
  if (card->waiting) {
    follow_sink_readiness(card);
  }
}

// The card has no commands and no queries: its bytes go to the sink and its
// state is the byte on its data lines.
auto printer_abi_command(void* instance, uint32_t command_id,
                         const void* payload, size_t payload_size)
    -> PeripheralStatus_t {
  (void)command_id;
  (void)payload;
  (void)payload_size;
  if (instance == nullptr) {
    return peripheral_error;
  }
  return peripheral_incompatible;
}

auto printer_abi_query(void* instance, uint32_t query_id, void* output,
                       size_t* output_size) -> PeripheralStatus_t {
  (void)instance;
  (void)query_id;
  (void)output;
  if (output_size == nullptr) {
    return peripheral_error;
  }
  return peripheral_incompatible;
}

static_assert(sizeof(PrinterSaveState_t) == 24,
              "the printer card's state frame is part of the plugin ABI");
static_assert(offsetof(PrinterSaveState_t, version) == 0,
              "the frame header is version then size");
static_assert(offsetof(PrinterSaveState_t, struct_size) == 4,
              "the frame header is version then size");
static_assert(offsetof(PrinterSaveState_t, total_chars_printed) == 8,
              "the dead fields keep their place so every frame written loads");
static_assert(offsetof(PrinterSaveState_t, busy_cycles) == 16,
              "the dead fields keep their place so every frame written loads");
static_assert(offsetof(PrinterSaveState_t, data_latch) == 20,
              "the data latch sits where every frame written has it");
static_assert(offsetof(PrinterSaveState_t, status_latch) == 21,
              "the dead fields keep their place so every frame written loads");
static_assert(offsetof(PrinterSaveState_t, is_online) == 22,
              "the dead fields keep their place so every frame written loads");
static_assert(offsetof(PrinterSaveState_t, is_busy) == 23,
              "the dead fields keep their place so every frame written loads");

// Value-initialised, so the dead fields go out as zeros; the latch is the only
// hardware state the card has.
auto printer_abi_save_state(void* instance, void* state_buffer,
                            size_t* buffer_size) -> PeripheralStatus_t {
  if (buffer_size == nullptr) {
    return peripheral_error;
  }
  constexpr size_t required_size = sizeof(PrinterSaveState_t);
  if (state_buffer == nullptr) {
    *buffer_size = required_size;
    return peripheral_ok;
  }
  if (instance == nullptr || *buffer_size < required_size) {
    return peripheral_error;
  }

  const auto* card = static_cast<const PrinterCard_t*>(instance);
  PrinterSaveState_t state{};
  state.version = PRINTER_STATE_VERSION;
  state.struct_size = static_cast<uint32_t>(required_size);
  state.data_latch = card->data_latch;
  std::memcpy(state_buffer, &state, required_size);

  *buffer_size = required_size;
  return peripheral_ok;
}

// A slot's snapshot buffer may be larger than the frame (the layer sizes it
// for the biggest card), so the frame's own struct_size says how much to
// read; everything past it is left alone. The frame carries no wait: the
// sink's readiness is sampled again at the next strobe, so a machine saved
// while parked drops one byte and parks again, as the hardware does after a
// reset.
auto printer_abi_load_state(void* instance, const void* state_buffer,
                            size_t buffer_size) -> PeripheralStatus_t {
  constexpr size_t header_size =
      offsetof(PrinterSaveState_t, total_chars_printed);
  if (instance == nullptr || state_buffer == nullptr ||
      buffer_size < header_size) {
    return peripheral_error;
  }

  PrinterSaveState_t state{};
  std::memcpy(&state, state_buffer, header_size);
  if (state.struct_size != sizeof(state) || buffer_size < state.struct_size) {
    return peripheral_error;
  }
  if (state.version != PRINTER_STATE_VERSION) {
    return peripheral_error;
  }

  std::memcpy(&state, state_buffer, state.struct_size);
  auto* card = static_cast<PrinterCard_t*>(instance);
  card->data_latch = state.data_latch;
  card->waiting = false;
  card->host->RegisterCxROM(card->slot, printer_rom.data());
  return peripheral_ok;
}

}  // namespace

static const Peripheral_t g_printer_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.printer",
    .name = "Parallel Printer",
    .description = "Apple Parallel Printer Interface Card (A2B0002)",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = 1,
    .init = printer_abi_init,
    .reset = printer_abi_reset,
    .shutdown = printer_abi_shutdown,
    .think = printer_abi_think,
    .on_vblank = nullptr,
    .save_state = printer_abi_save_state,
    .load_state = printer_abi_load_state,
    .command = printer_abi_command,
    .query = printer_abi_query};

// peripheral_register and ActivePeripheral_t::api take a mutable
// Peripheral_t*, so the immutable descriptor is cast the same way
// PERIPHERAL_REGISTER casts it.
auto printer_get_descriptor() -> Peripheral_t* {
  return const_cast<Peripheral_t*>(&g_printer_peripheral);
}

PERIPHERAL_REGISTER(g_printer_peripheral)
