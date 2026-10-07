// SPDX-License-Identifier: GPL-2.0-only
#include "apple2/peripherals/harddisk/Harddisk.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/Peripheral_Subsystems.h"
#include "apple2/peripherals/Peripheral_Types.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskError.h"
#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"
#include "apple2/peripherals/harddisk/HarddiskLoader.h"

namespace {

namespace physical {
constexpr int block_size = 512;
constexpr size_t rom_size = 256;
// The unit byte is DSSS0000: the drive in bit 7, the slot in bits 6-4
// (ProDOS 8 Technical Reference Manual, 6.3.2); the firmware takes the slot
// from it, the card only the drive.
constexpr uint8_t unit_drive_bit = 0x80;
// ProDOS counts blocks in sixteen bits (6.3.1), so a larger image is served
// to the last block it can name.
constexpr uint32_t max_block_count = 0xFFFF;
}  // namespace physical

namespace regs {
constexpr uint8_t addr_mask = 0x0F;
constexpr uint8_t command = 0x0;
constexpr uint8_t unit = 0x1;
constexpr uint8_t block_low = 0x2;
constexpr uint8_t block_high = 0x3;
constexpr uint8_t data = 0x4;
constexpr uint8_t count_low = 0x5;
constexpr uint8_t count_high = 0x6;
constexpr uint8_t count = 16;
}  // namespace regs

namespace rom {
// The one slot-dependent byte: the operand of the LDA #$Cn at $Cn08, $C0 in
// the array and $C0 | slot in the page the card registers.
constexpr size_t slot_operand = 0x09;
constexpr uint8_t slot_page = 0xC0;
}  // namespace rom

// The commands ProDOS hands a block device in $42 (6.3.2).
enum HarddiskProdosCommand_e {
  prodos_cmd_status = 0x00,
  prodos_cmd_read = 0x01,
  prodos_cmd_write = 0x02,
  prodos_cmd_format = 0x03
};

// The controller's data buffer is the firmware's to fill or drain: read-out
// opens when a READ completes and closes after the 512th byte is taken,
// write-in opens at the first byte pushed and closes when the WRITE executes.
// A unit write closes either, since the firmware writes the unit first in
// every call.
enum HarddiskDataPhase_e {
  harddisk_phase_idle = 0,
  harddisk_phase_read_out = 1,
  harddisk_phase_write_in = 2
};

// ProDOS 8 block-device firmware for a two-volume controller, written from
// the ProDOS 8 Technical Reference Manual (6.3.1: the ID bytes, $CnFC-$CnFF,
// STATUS's X/Y/A/carry; 6.3.2: the parameter block $42-$47, the unit byte,
// the error codes), ProDOS 8 Technical Note #21 ($Cn07; the unit's slot bits
// run one to seven), the 1979 Apple II Reference Manual Autostart listing (p.
// 144) and the Apple IIe Technical Reference Manual Monitor listing (p. 307:
// SLOOP at $FABA, DISKID at $FB02 compared as DISKID-1,Y, $FF59) and p. 136
// ($FBB3).
//
// Nothing in the page names the slot: branches are relative, I/O goes
// through $C080,X with X = slot * 16 taken from the unit byte, and the boot
// entry calls the driver by pushing a return address built from the one
// patched immediate at $08. That immediate stands in for JSR $FF58 / TSX /
// LDA $0100,X, which needs an RTS at $FF58 that a ProDOS language-card bank
// need not hold.
//
// The driver entry sits at $Cn46 because a resident ProDOS re-reads $CnFF
// only at boot: a saved session holds the entry address in its DEVADR table
// and resumes by calling it, so the entry keeps the address those sessions
// hold. The boot's failure path sits behind the driver and is reached through
// the BCS at $3F, always taken because $3F is reached only with the carry
// set.
//
// The register map is the card's own: $C0n0 command (a write executes it) and
// result, $C0n1 unit, $C0n2-$C0n3 block, $C0n4 the data port, $C0n5-$C0n6 the
// block count STATUS loaded. SSS = 0, which ProDOS never issues (TRM 4.7.1,
// TN #21), would address $C080, the language card's page, as on any card
// that finds its page from the unit byte.
//
// The reads of $FBB3 and $FABA assume the Monitor ROM is read-enabled, which
// it is at reset, under the Monitor and under Applesoft; a boot started from
// code running with RAM banked over $F800-$FFFF runs whatever that RAM holds.
// $FBB3 is the machine identification byte: $38 is the original II's Monitor,
// which has no slot scan, so the fallback there is the Monitor's reset entry.
// The J-Plus, Pravets and TK3000 ROMs carry the Autostart scan at $FABA too;
// the Base64A does not, so booting it there with no image has no safe target.
//
// That A = 0 with the carry clear is the success convention for READ, WRITE
// and FORMAT, as it is for STATUS, is inferred: 6.3.2 states only the error
// rule.
//
// ; ---- Boot entry; the ProDOS ID bytes are the immediates -------------
// $Cn00  A9 20        LDA #$20        ; $Cn01 = $20              TRM 6.3.1
// $Cn02  A9 00        LDA #$00        ; $Cn03 = $00              TRM 6.3.1
// $Cn04  A9 03        LDA #$03        ; $Cn05 = $03              TRM 6.3.1
// $Cn06  A9 3C        LDA #$3C        ; $Cn07 = $3C: not SmartPort (TN #21);
//                                     ; the Disk II fourth byte the II Plus
//                                     ; and unenhanced //e scans require
//                                     ; (DISKID $FB02, compared as DISKID-1,Y)
// $Cn08  A9 C0        LDA #$C0        ; $Cn: the low nibble is patched to the
//                                     ; slot at init
// $Cn0A  85 47        STA $47         ; keep $Cn until block 0's number is set
// $Cn0C  0A           ASL A
// $Cn0D  0A           ASL A
// $Cn0E  0A           ASL A
// $Cn0F  0A           ASL A           ; $n0
// $Cn10  85 43        STA $43         ; unit: drive 1, slot n    TRM 6.3.2
// $Cn12  A5 47        LDA $47
// $Cn14  48           PHA             ; return address high ($Cn) for the RTS
// $Cn15  A9 1D        LDA #$1D        ; return address low - 1 ($Cn1E - 1)
// $Cn17  48           PHA
// $Cn18  A9 00        LDA #$00
// $Cn1A  85 42        STA $42         ; STATUS                   TRM 6.3.2
// $Cn1C  F0 28        BEQ DRIVER      ; Z from LDA #0: call the driver
// RET1
// $Cn1E  B0 1F        BCS FAILJ       ; carry set: no device     TRM 6.3.2
// $Cn20  A5 47        LDA $47
// $Cn22  48           PHA
// $Cn23  A9 37        LDA #$37        ; $Cn38 - 1
// $Cn25  48           PHA
// $Cn26  A9 00        LDA #$00
// $Cn28  85 44        STA $44         ; buffer $0800             TRM 6.3.1
// $Cn2A  85 46        STA $46         ; block 0
// $Cn2C  85 47        STA $47
// $Cn2E  A9 08        LDA #$08
// $Cn30  85 45        STA $45
// $Cn32  A9 01        LDA #$01
// $Cn34  85 42        STA $42         ; READ                     TRM 6.3.2
// $Cn36  D0 0E        BNE DRIVER      ; Z clear from LDA #1
// RET2
// $Cn38  B0 05        BCS FAILJ
// $Cn3A  A6 43        LDX $43         ; X = slot * 16 for the boot block
//                                     ; (DISK2.rom $F4 LDX $2B / JMP $0801;
//                                     ; ProDOS block 0 $0807 STX $43)
// $Cn3C  4C 01 08     JMP $0801       ; enter the boot block    TRM 6.3.1
// FAILJ
// $Cn3F  B0 6E        BCS FAIL        ; always: reached with carry set only
// $Cn41  00 00 00 00 00               ; unused
// ; ---- ProDOS driver entry ($CnFF = $46) ------------------------------
// DRIVER
// $Cn46  A5 43        LDA $43
// $Cn48  29 70        AND #$70
// $Cn4A  AA           TAX             ; X = slot * 16: this slot's I/O page
//                                     ; from the unit's SSS bits  TRM 6.3.2
// $Cn4B  A5 43        LDA $43
// $Cn4D  9D 81 C0     STA $C081,X     ; unit: selects the drive, resets the
//                                     ; data index
// $Cn50  A5 46        LDA $46
// $Cn52  9D 82 C0     STA $C082,X     ; block low
// $Cn55  A5 47        LDA $47
// $Cn57  9D 83 C0     STA $C083,X     ; block high
// $Cn5A  A5 42        LDA $42
// $Cn5C  C9 02        CMP #$02
// $Cn5E  D0 18        BNE EXEC        ; only WRITE hands its block over first
// $Cn60  A0 00        LDY #$00
// W1
// $Cn62  B1 44        LDA ($44),Y
// $Cn64  9D 84 C0     STA $C084,X     ; data port
// $Cn67  C8           INY
// $Cn68  D0 F8        BNE W1
// $Cn6A  E6 45        INC $45         ; second page
// W2
// $Cn6C  B1 44        LDA ($44),Y
// $Cn6E  9D 84 C0     STA $C084,X
// $Cn71  C8           INY
// $Cn72  D0 F8        BNE W2
// $Cn74  C6 45        DEC $45         ; the parameter block is the caller's
// $Cn76  A5 42        LDA $42
// EXEC
// $Cn78  9D 80 C0     STA $C080,X     ; command: executes
// $Cn7B  BD 80 C0     LDA $C080,X     ; result: $00, $27, $28, $2B  TRM 6.3.2
// $Cn7E  D0 2D        BNE ERROR
// $Cn80  A5 42        LDA $42
// $Cn82  F0 1E        BEQ STATUS
// $Cn84  C9 01        CMP #$01
// $Cn86  D0 16        BNE DONE        ; WRITE and FORMAT have nothing to fetch
// $Cn88  A0 00        LDY #$00
// R1
// $Cn8A  BD 84 C0     LDA $C084,X
// $Cn8D  91 44        STA ($44),Y
// $Cn8F  C8           INY
// $Cn90  D0 F8        BNE R1
// $Cn92  E6 45        INC $45
// R2
// $Cn94  BD 84 C0     LDA $C084,X
// $Cn97  91 44        STA ($44),Y
// $Cn99  C8           INY
// $Cn9A  D0 F8        BNE R2
// $Cn9C  C6 45        DEC $45
// DONE
// $Cn9E  A9 00        LDA #$00
// $CnA0  18           CLC             ; success: carry clear
// $CnA1  60           RTS
// STATUS
// $CnA2  BC 86 C0     LDY $C086,X     ; block count high         TRM 6.3.1
// $CnA5  BD 85 C0     LDA $C085,X     ; block count low
// $CnA8  AA           TAX
// $CnA9  A9 00        LDA #$00        ; A zero                   TRM 6.3.1
// $CnAB  18           CLC
// $CnAC  60           RTS
// ERROR
// $CnAD  38           SEC             ; A holds the ProDOS code  TRM 6.3.2
// $CnAE  60           RTS
// ; ---- No device: continue the Monitor's own slot scan ----------------
// FAIL
// $CnAF  AD B3 FB     LDA $FBB3       ; machine id              IIe TRM p. 136
// $CnB2  C9 38        CMP #$38        ; $38: the original II's Monitor
// $CnB4  F0 11        BEQ MONITOR     ; has no slot scan
// $CnB6  A5 43        LDA $43
// $CnB8  4A           LSR A
// $CnB9  4A           LSR A
// $CnBA  4A           LSR A
// $CnBB  4A           LSR A           ; $0n
// $CnBC  09 C0        ORA #$C0        ; $Cn
// $CnBE  85 01        STA $01         ; LOC1: the scan's slot pointer, as the
// $CnC0  A9 00        LDA #$00        ; scan itself left it      1979 p. 144
// $CnC2  85 00        STA $00         ; LOC0                     IIe TRM p. 307
// $CnC4  4C BA FA     JMP $FABA       ; SLOOP: DEC $01 and test the next slot
// MONITOR
// $CnC7  4C 59 FF     JMP $FF59       ; the Monitor's reset entry
// ; $CnCA-$CnFB: $00
// $CnFC  00 00                        ; blocks: ask STATUS       TRM 6.3.1
// $CnFE  DF                           ; removable, interruptible, two volumes
//                                     ; (bits 5-4 are volumes minus one: with
//                                     ; 01 ProDOS installs two units; TN #21),
//                                     ; format, write, read, status
// $CnFF  46                           ; driver entry low byte    TRM 6.3.1
const std::array<uint8_t, physical::rom_size> harddisk_rom = {{
    0xA9, 0x20, 0xA9, 0x00, 0xA9, 0x03, 0xA9, 0x3C, 0xA9, 0xC0, 0x85, 0x47,
    0x0A, 0x0A, 0x0A, 0x0A, 0x85, 0x43, 0xA5, 0x47, 0x48, 0xA9, 0x1D, 0x48,
    0xA9, 0x00, 0x85, 0x42, 0xF0, 0x28, 0xB0, 0x1F, 0xA5, 0x47, 0x48, 0xA9,
    0x37, 0x48, 0xA9, 0x00, 0x85, 0x44, 0x85, 0x46, 0x85, 0x47, 0xA9, 0x08,
    0x85, 0x45, 0xA9, 0x01, 0x85, 0x42, 0xD0, 0x0E, 0xB0, 0x05, 0xA6, 0x43,
    0x4C, 0x01, 0x08, 0xB0, 0x6E, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA5, 0x43,
    0x29, 0x70, 0xAA, 0xA5, 0x43, 0x9D, 0x81, 0xC0, 0xA5, 0x46, 0x9D, 0x82,
    0xC0, 0xA5, 0x47, 0x9D, 0x83, 0xC0, 0xA5, 0x42, 0xC9, 0x02, 0xD0, 0x18,
    0xA0, 0x00, 0xB1, 0x44, 0x9D, 0x84, 0xC0, 0xC8, 0xD0, 0xF8, 0xE6, 0x45,
    0xB1, 0x44, 0x9D, 0x84, 0xC0, 0xC8, 0xD0, 0xF8, 0xC6, 0x45, 0xA5, 0x42,
    0x9D, 0x80, 0xC0, 0xBD, 0x80, 0xC0, 0xD0, 0x2D, 0xA5, 0x42, 0xF0, 0x1E,
    0xC9, 0x01, 0xD0, 0x16, 0xA0, 0x00, 0xBD, 0x84, 0xC0, 0x91, 0x44, 0xC8,
    0xD0, 0xF8, 0xE6, 0x45, 0xBD, 0x84, 0xC0, 0x91, 0x44, 0xC8, 0xD0, 0xF8,
    0xC6, 0x45, 0xA9, 0x00, 0x18, 0x60, 0xBC, 0x86, 0xC0, 0xBD, 0x85, 0xC0,
    0xAA, 0xA9, 0x00, 0x18, 0x60, 0x38, 0x60, 0xAD, 0xB3, 0xFB, 0xC9, 0x38,
    0xF0, 0x11, 0xA5, 0x43, 0x4A, 0x4A, 0x4A, 0x4A, 0x09, 0xC0, 0x85, 0x01,
    0xA9, 0x00, 0x85, 0x00, 0x4C, 0xBA, 0xFA, 0x4C, 0x59, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xDF, 0x46,
}};

struct Harddisk_t {
  std::string full_path;
  std::string display_name;
  bool is_loaded = false;
  const HarddiskFormatDriver_t* driver = nullptr;
  void* driver_instance = nullptr;
  bool user_write_protected = false;
  HarddiskError_e last_error = harddisk_err_none;
};

// One register set, not one per drive: a controller has one command
// register, one unit latch, one block register and one buffer, and the unit
// selects the drive when the command executes.
struct HarddiskPeripheral_t {
  std::array<Harddisk_t, harddisk_drive_count> drives{};
  std::array<uint8_t, physical::rom_size> rom{};
  std::array<uint8_t, physical::block_size> buffer{};
  uint16_t block = 0;
  uint16_t data_index = 0;
  uint16_t block_count = 0;
  uint8_t unit = 0;
  uint8_t command = 0;
  uint8_t result = harddisk_prodos_ok;
  uint8_t data_phase = harddisk_phase_idle;
  bool buffer_poisoned = false;
  int slot = 0;
  HostInterface_t* host = nullptr;
};

auto copy_string_to_buffer(const std::string& src, char* dest, size_t capacity)
    -> void {
  if (dest == nullptr || capacity == 0) {
    return;
  }
  const size_t copy_len = std::min(src.size(), capacity - 1);
  std::memcpy(dest, src.data(), copy_len);
  dest[copy_len] = '\0';
}

auto is_drive_valid(int drive_index) -> bool {
  return (drive_index >= 0 && drive_index < harddisk_drive_count);
}

auto selected_drive(HarddiskPeripheral_t* card) -> Harddisk_t& {
  const size_t index = (card->unit & physical::unit_drive_bit) != 0 ? 1 : 0;
  return card->drives.at(index);
}

auto notify_status_changed(const HarddiskPeripheral_t* card) -> void {
  if (card != nullptr && card->host != nullptr &&
      card->host->NotifyStatusChanged != nullptr) {
    card->host->NotifyStatusChanged(card->slot);
  }
}

auto notify_activity_changed(const HarddiskPeripheral_t* card, bool active)
    -> void {
  if (card != nullptr && card->host != nullptr &&
      card->host->NotifyActivityChanged != nullptr) {
    card->host->NotifyActivityChanged(card->slot, active);
  }
}

// The registers above the block count drive nothing onto the data bus, so a
// read there sees whatever the video scanner is fetching that cycle.
auto read_floating_bus(const HarddiskPeripheral_t* card,
                       uint32_t executed_cycles) -> uint8_t {
  if (card == nullptr || card->host == nullptr ||
      card->host->ReadFloatingBus == nullptr) {
    return 0xFF;
  }
  return card->host->ReadFloatingBus(executed_cycles);
}

// One answer to "may this be written": the user's flag, and whatever the
// driver knows about the file and the medium.
auto is_write_protected(const Harddisk_t& drive) -> bool {
  if (drive.user_write_protected) {
    return true;
  }
  return drive.driver != nullptr && drive.driver_instance != nullptr &&
         drive.driver->is_write_protected != nullptr &&
         drive.driver->is_write_protected(drive.driver_instance);
}

auto total_blocks(const Harddisk_t& drive) -> uint32_t {
  if (drive.driver == nullptr || drive.driver_instance == nullptr ||
      drive.driver->get_total_blocks == nullptr) {
    return 0;
  }
  return drive.driver->get_total_blocks(drive.driver_instance);
}

auto end_data_phase(HarddiskPeripheral_t* card) -> void {
  if (card->data_phase == harddisk_phase_read_out) {
    notify_activity_changed(card, false);
  }
  card->data_phase = harddisk_phase_idle;
}

// How a name is shown is the frontend's choice; the card hands over the
// file's own.
auto update_image_metadata(Harddisk_t* drive, const char* path) -> void {
  if (drive == nullptr || path == nullptr) {
    return;
  }

  drive->full_path = path;

  const char* last_sep = strrchr(path, '/');
  std::string name = (last_sep != nullptr) ? last_sep + 1 : path;
  if (name.length() > harddisk_status_name_max) {
    name.resize(harddisk_status_name_max);
  }
  drive->display_name = name;
}

// The loader has no host of its own, so what it and the drivers could only
// record is told here.
auto report_loader_note(void* context, const char* driver_name,
                        const char* reason) -> void {
  auto* card = static_cast<HarddiskPeripheral_t*>(context);
  if (card == nullptr || card->host == nullptr || card->host->Log == nullptr) {
    return;
  }
  card->host->Log(card, log_warn, "Hard disk: format driver '%s': %s\n",
                  driver_name, reason);
}

auto eject_harddisk_from_drive(HarddiskPeripheral_t* card, int drive_index)
    -> void {
  if (card == nullptr || !is_drive_valid(drive_index)) {
    return;
  }
  auto& drive = card->drives.at(static_cast<size_t>(drive_index));

  if (drive.driver != nullptr && drive.driver_instance != nullptr &&
      drive.driver->close != nullptr) {
    drive.driver->close(drive.driver_instance);
    harddisk_loader_drain_rejections(report_loader_note, card);
  }

  drive = Harddisk_t();
}

auto insert_harddisk_into_drive(HarddiskPeripheral_t* card, int drive_index,
                                const char* path, bool write_protected)
    -> HarddiskError_e {
  if (card == nullptr || !is_drive_valid(drive_index) || path == nullptr) {
    return harddisk_err_io;
  }
  auto& drive = card->drives.at(static_cast<size_t>(drive_index));

  if (drive.is_loaded) {
    eject_harddisk_from_drive(card, drive_index);
  }

  drive.user_write_protected = write_protected;
  const HarddiskError_e error =
      harddisk_loader_open(path, &drive.driver, &drive.driver_instance);
  harddisk_loader_drain_rejections(report_loader_note, card);

  drive.last_error = error;

  if (error != harddisk_err_none) {
    notify_status_changed(card);
    return error;
  }

  drive.is_loaded = true;
  update_image_metadata(&drive, path);

  const uint32_t blocks = total_blocks(drive);
  if (blocks > physical::max_block_count) {
    card->host->Log(card, log_warn,
                    "Hard disk: '%s' holds %u blocks; ProDOS can address "
                    "%u, so the rest are not served\n",
                    path, blocks, physical::max_block_count);
  }
  notify_status_changed(card);

  return harddisk_err_none;
}

// The checks run in a fixed order so the code a caller sees is deterministic:
// an unknown command, then an empty drive, then protection, then the block
// range, then the medium itself.
auto execute_command(HarddiskPeripheral_t* card) -> uint8_t {
  Harddisk_t& drive = selected_drive(card);

  if (card->command > prodos_cmd_format) {
    return harddisk_prodos_io_error;
  }
  if (!drive.is_loaded) {
    return harddisk_prodos_no_device;
  }
  const bool writes_medium =
      card->command == prodos_cmd_write || card->command == prodos_cmd_format;
  if (writes_medium && is_write_protected(drive)) {
    return harddisk_prodos_write_protected;
  }
  const uint32_t blocks = total_blocks(drive);
  const bool addresses_block =
      card->command == prodos_cmd_read || card->command == prodos_cmd_write;
  if (addresses_block && card->block >= blocks) {
    return harddisk_prodos_io_error;
  }

  switch (card->command) {
    case prodos_cmd_status:
      card->block_count = static_cast<uint16_t>(
          std::min<uint32_t>(blocks, physical::max_block_count));
      return harddisk_prodos_ok;

    case prodos_cmd_read:
      if (drive.driver->read_block == nullptr ||
          drive.driver->read_block(drive.driver_instance, card->block,
                                   card->buffer.data()) != harddisk_err_none) {
        return harddisk_prodos_io_error;
      }
      card->data_index = 0;
      card->data_phase = harddisk_phase_read_out;
      notify_activity_changed(card, true);
      return harddisk_prodos_ok;

    case prodos_cmd_write:
      // A buffer a save state could not carry must not reach the medium as
      // zeros; the one write that would have used it fails instead.
      if (card->buffer_poisoned) {
        card->buffer_poisoned = false;
        return harddisk_prodos_io_error;
      }
      if (drive.driver->write_block == nullptr ||
          drive.driver->write_block(drive.driver_instance, card->block,
                                    card->buffer.data()) != harddisk_err_none) {
        return harddisk_prodos_io_error;
      }
      card->data_index = 0;
      card->data_phase = harddisk_phase_idle;
      notify_activity_changed(card, true);
      notify_activity_changed(card, false);
      return harddisk_prodos_ok;

    case prodos_cmd_format:
      // A block device "need only lay down address marks if required" (TRM
      // 6.3.2); an image file has none, so a format changes nothing.
      return harddisk_prodos_ok;

    default:
      return harddisk_prodos_io_error;
  }
}

auto harddisk_io_command(void* instance, uint16_t, uint16_t, uint8_t is_write,
                         uint8_t data_value, uint32_t) -> uint8_t {
  auto* card = static_cast<HarddiskPeripheral_t*>(instance);
  if (is_write == 0) {
    return card->result;
  }
  card->command = data_value;
  card->result = execute_command(card);
  return 0;
}

auto harddisk_io_unit(void* instance, uint16_t, uint16_t, uint8_t is_write,
                      uint8_t data_value, uint32_t) -> uint8_t {
  auto* card = static_cast<HarddiskPeripheral_t*>(instance);
  if (is_write == 0) {
    return card->unit;
  }
  card->unit = data_value;
  card->data_index = 0;
  end_data_phase(card);
  return 0;
}

auto harddisk_io_block_low(void* instance, uint16_t, uint16_t, uint8_t is_write,
                           uint8_t data_value, uint32_t) -> uint8_t {
  auto* card = static_cast<HarddiskPeripheral_t*>(instance);
  if (is_write == 0) {
    return static_cast<uint8_t>(card->block & 0xFF);
  }
  card->block = static_cast<uint16_t>((card->block & 0xFF00) | data_value);
  return 0;
}

auto harddisk_io_block_high(void* instance, uint16_t, uint16_t,
                            uint8_t is_write, uint8_t data_value, uint32_t)
    -> uint8_t {
  auto* card = static_cast<HarddiskPeripheral_t*>(instance);
  if (is_write == 0) {
    return static_cast<uint8_t>(card->block >> 8);
  }
  card->block =
      static_cast<uint16_t>((card->block & 0x00FF) | (data_value << 8));
  return 0;
}

auto harddisk_io_data(void* instance, uint16_t, uint16_t, uint8_t is_write,
                      uint8_t data_value, uint32_t) -> uint8_t {
  auto* card = static_cast<HarddiskPeripheral_t*>(instance);
  const size_t index = card->data_index;
  card->data_index =
      static_cast<uint16_t>((card->data_index + 1) % physical::block_size);

  if (is_write != 0) {
    // Loading the controller is not disk activity; the light goes out until
    // the WRITE executes.
    if (card->data_phase != harddisk_phase_write_in) {
      end_data_phase(card);
      card->data_phase = harddisk_phase_write_in;
    }
    card->buffer.at(index) = data_value;
    return 0;
  }

  const uint8_t value = card->buffer.at(index);
  if (card->data_phase == harddisk_phase_read_out && card->data_index == 0) {
    end_data_phase(card);
  }
  return value;
}

auto harddisk_io_count_low(void* instance, uint16_t, uint16_t, uint8_t is_write,
                           uint8_t, uint32_t) -> uint8_t {
  auto* card = static_cast<HarddiskPeripheral_t*>(instance);
  if (is_write != 0) {
    return 0;
  }
  return static_cast<uint8_t>(card->block_count & 0xFF);
}

auto harddisk_io_count_high(void* instance, uint16_t, uint16_t,
                            uint8_t is_write, uint8_t, uint32_t) -> uint8_t {
  auto* card = static_cast<HarddiskPeripheral_t*>(instance);
  if (is_write != 0) {
    return 0;
  }
  return static_cast<uint8_t>(card->block_count >> 8);
}

auto harddisk_io_floating(void* instance, uint16_t, uint16_t, uint8_t is_write,
                          uint8_t, uint32_t executed_cycles) -> uint8_t {
  if (is_write != 0) {
    return 0;
  }
  return read_floating_bus(static_cast<HarddiskPeripheral_t*>(instance),
                           executed_cycles);
}

using HarddiskIoHandler_t = auto (*)(void* instance, uint16_t program_counter,
                                     uint16_t memory_address, uint8_t is_write,
                                     uint8_t data_value,
                                     uint32_t executed_cycles) -> uint8_t;

constexpr std::array<HarddiskIoHandler_t, regs::count> k_harddisk_io_handlers =
    {harddisk_io_command,    harddisk_io_unit,     harddisk_io_block_low,
     harddisk_io_block_high, harddisk_io_data,     harddisk_io_count_low,
     harddisk_io_count_high, harddisk_io_floating, harddisk_io_floating,
     harddisk_io_floating,   harddisk_io_floating, harddisk_io_floating,
     harddisk_io_floating,   harddisk_io_floating, harddisk_io_floating,
     harddisk_io_floating};

static_assert(regs::command == 0 && regs::unit == 1 && regs::block_low == 2 &&
                  regs::block_high == 3 && regs::data == 4 &&
                  regs::count_low == 5 && regs::count_high == 6,
              "the handler table is indexed by register offset");

auto harddisk_io_read(void* instance, uint16_t program_counter,
                      uint16_t memory_address, uint8_t is_write, uint8_t,
                      uint32_t executed_cycles) -> uint8_t {
  if (instance == nullptr || is_write != 0) {
    return read_floating_bus(static_cast<HarddiskPeripheral_t*>(instance),
                             executed_cycles);
  }
  const size_t handler_index = memory_address & regs::addr_mask;
  return k_harddisk_io_handlers.at(handler_index)(
      instance, program_counter, memory_address, 0, 0, executed_cycles);
}

auto harddisk_io_write(void* instance, uint16_t program_counter,
                       uint16_t memory_address, uint8_t is_write,
                       uint8_t data_value, uint32_t executed_cycles)
    -> uint8_t {
  if (instance == nullptr || is_write == 0) {
    return 0;
  }
  const size_t handler_index = memory_address & regs::addr_mask;
  return k_harddisk_io_handlers.at(handler_index)(instance, program_counter,
                                                  memory_address, 1, data_value,
                                                  executed_cycles);
}

// Better no card than a phantom one: without these members the firmware
// cannot be seen, the registers cannot be reached or the host cannot be told
// what the card is doing, and the log names the missing one.
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
  if (host->NotifyActivityChanged == nullptr) {
    return "NotifyActivityChanged";
  }
  if (host->NotifyStatusChanged == nullptr) {
    return "NotifyStatusChanged";
  }
  return nullptr;
}

// Power-on state. Whether a real controller's RESET' clears its latches is
// not known; ProDOS rewrites every register per call, so nothing depends on
// it.
auto power_on(HarddiskPeripheral_t* card) -> void {
  card->unit = 0;
  card->command = 0;
  card->result = harddisk_prodos_ok;
  card->block = 0;
  card->data_index = 0;
  card->block_count = 0;
  card->data_phase = harddisk_phase_idle;
  card->buffer_poisoned = false;
}

auto harddisk_abi_init(int slot, HostInterface_t* host) -> void* {
  if (host == nullptr || host->Log == nullptr) {
    return nullptr;
  }
  const char* missing = missing_host_member(host);
  if (missing != nullptr) {
    host->Log(nullptr, log_error,
              "Hard disk in slot %d: the host offers no %s\n", slot, missing);
    return nullptr;
  }

  auto card = std::unique_ptr<HarddiskPeripheral_t>(new (std::nothrow)
                                                        HarddiskPeripheral_t());
  if (!card) {
    return nullptr;
  }
  card->host = host;
  card->slot = slot;

  harddisk_loader_drain_rejections(report_loader_note, card.get());

  card->rom = harddisk_rom;
  card->rom.at(rom::slot_operand) =
      static_cast<uint8_t>(rom::slot_page | (slot & 0x0F));
  host->RegisterCxROM(slot, card->rom.data());
  host->RegisterIO(slot, harddisk_io_read, harddisk_io_write, nullptr, nullptr);

  return card.release();
}

auto harddisk_abi_reset(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  power_on(static_cast<HarddiskPeripheral_t*>(instance));
}

auto harddisk_abi_shutdown(void* instance) -> void {
  if (instance == nullptr) {
    return;
  }
  const std::unique_ptr<HarddiskPeripheral_t> card(
      static_cast<HarddiskPeripheral_t*>(instance));
  for (int i = 0; i < harddisk_drive_count; ++i) {
    eject_harddisk_from_drive(card.get(), i);
  }
}

auto harddisk_abi_command(void* instance, uint32_t cmd_id, const void* payload,
                          size_t payload_size) -> PeripheralStatus_t {
  if (instance == nullptr) {
    return peripheral_error;
  }
  auto* card = static_cast<HarddiskPeripheral_t*>(instance);

  if (!peripheral_cmd_is_mine(cmd_id, PERIPHERAL_SUBSYSTEM_HARDDISK)) {
    return peripheral_incompatible;
  }

  switch (static_cast<HarddiskCmd_t>(cmd_id)) {
    case harddisk_cmd_insert: {
      if (payload == nullptr || payload_size != sizeof(HarddiskInsertCmd_t)) {
        return peripheral_error;
      }
      const auto* cmd = static_cast<const HarddiskInsertCmd_t*>(payload);
      if (!is_drive_valid(cmd->drive) ||
          memchr(cmd->path, '\0', sizeof(cmd->path)) == nullptr) {
        return peripheral_error;
      }
      // A refusal is the caller's to hear; through the queue it reaches the
      // status as the drive's last error.
      if (insert_harddisk_into_drive(card, cmd->drive, cmd->path,
                                     cmd->write_protected != 0) !=
          harddisk_err_none) {
        return peripheral_error;
      }
      return peripheral_ok;
    }
    case harddisk_cmd_eject: {
      if (payload == nullptr || payload_size != sizeof(HarddiskEjectCmd_t)) {
        return peripheral_error;
      }
      const auto* cmd = static_cast<const HarddiskEjectCmd_t*>(payload);
      if (!is_drive_valid(cmd->drive)) {
        return peripheral_error;
      }
      eject_harddisk_from_drive(card, cmd->drive);
      notify_status_changed(card);
      return peripheral_ok;
    }
    case harddisk_cmd_set_protect: {
      if (payload == nullptr ||
          payload_size != sizeof(HarddiskSetProtectCmd_t)) {
        return peripheral_error;
      }
      const auto* cmd = static_cast<const HarddiskSetProtectCmd_t*>(payload);
      if (!is_drive_valid(cmd->drive)) {
        return peripheral_error;
      }
      card->drives.at(static_cast<size_t>(cmd->drive)).user_write_protected =
          (cmd->write_protected != 0);
      notify_status_changed(card);
      return peripheral_ok;
    }
    default:
      break;
  }
  return peripheral_incompatible;
}

auto activity_status(const HarddiskPeripheral_t* card) -> uint8_t {
  switch (card->data_phase) {
    case harddisk_phase_read_out:
      return harddisk_status_read;
    case harddisk_phase_write_in:
      return harddisk_status_write;
    default:
      return harddisk_status_off;
  }
}

auto harddisk_abi_query(void* instance, uint32_t cmd_id, void* data,
                        size_t* size) -> PeripheralStatus_t {
  if (size == nullptr) {
    return peripheral_error;
  }

  if (!peripheral_cmd_is_mine(cmd_id, PERIPHERAL_SUBSYSTEM_HARDDISK)) {
    return peripheral_incompatible;
  }

  if (cmd_id == harddisk_query_supported_extensions) {
    constexpr size_t required_ext_size = 256;
    if (data == nullptr || *size == 0) {
      *size = required_ext_size;
      return peripheral_ok;
    }
    harddisk_loader_get_supported_extensions(static_cast<char*>(data), *size);
    *size = strlen(static_cast<const char*>(data)) + 1;
    return peripheral_ok;
  }

  if (cmd_id != harddisk_query_status) {
    return peripheral_incompatible;
  }

  constexpr size_t required_size = sizeof(HarddiskStatus_t);
  if (data == nullptr) {
    *size = required_size;
    return peripheral_ok;
  }

  if (*size < required_size) {
    *size = required_size;
    return peripheral_error;
  }

  if (instance == nullptr) {
    return peripheral_error;
  }

  const auto* card = static_cast<const HarddiskPeripheral_t*>(instance);
  auto* status = static_cast<HarddiskStatus_t*>(data);
  std::memset(status, 0, required_size);

  const Harddisk_t& drive0 = card->drives.at(0);
  status->drive0_last_error = static_cast<int32_t>(drive0.last_error);
  status->drive0_loaded = drive0.is_loaded ? 1 : 0;
  status->drive0_write_protected = is_write_protected(drive0) ? 1 : 0;
  copy_string_to_buffer(drive0.display_name, status->drive0_name,
                        harddisk_status_name_max);
  copy_string_to_buffer(drive0.full_path, status->drive0_full_path,
                        harddisk_status_path_max);

  const Harddisk_t& drive1 = card->drives.at(1);
  status->drive1_last_error = static_cast<int32_t>(drive1.last_error);
  status->drive1_loaded = drive1.is_loaded ? 1 : 0;
  status->drive1_write_protected = is_write_protected(drive1) ? 1 : 0;
  copy_string_to_buffer(drive1.display_name, status->drive1_name,
                        harddisk_status_name_max);
  copy_string_to_buffer(drive1.full_path, status->drive1_full_path,
                        harddisk_status_path_max);

  status->activity_status = activity_status(card);
  *size = required_size;

  return peripheral_ok;
}

static_assert(sizeof(HarddiskSaveState_t) == harddisk_save_state_size,
              "the frame is twenty bytes");
static_assert(offsetof(HarddiskSaveState_t, version) == 0 &&
                  offsetof(HarddiskSaveState_t, struct_size) == 4 &&
                  offsetof(HarddiskSaveState_t, unit) == 8 &&
                  offsetof(HarddiskSaveState_t, command) == 9 &&
                  offsetof(HarddiskSaveState_t, result) == 10 &&
                  offsetof(HarddiskSaveState_t, data_phase) == 11 &&
                  offsetof(HarddiskSaveState_t, block) == 12 &&
                  offsetof(HarddiskSaveState_t, data_index) == 14 &&
                  offsetof(HarddiskSaveState_t, block_count) == 16 &&
                  offsetof(HarddiskSaveState_t, reserved) == 18,
              "every field sits where a file written earlier put it");

auto harddisk_abi_save_state(void* instance, void* buffer, size_t* size)
    -> PeripheralStatus_t {
  if (size == nullptr) {
    return peripheral_error;
  }

  constexpr size_t required = sizeof(HarddiskSaveState_t);

  if (buffer == nullptr) {
    *size = required;
    return peripheral_ok;
  }

  if (*size < required) {
    *size = required;
    return peripheral_error;
  }

  if (instance == nullptr) {
    return peripheral_error;
  }

  const auto* card = static_cast<const HarddiskPeripheral_t*>(instance);
  auto* ss = static_cast<HarddiskSaveState_t*>(buffer);

  std::memset(ss, 0, required);
  ss->version = HARDDISK_STATE_VERSION;
  ss->struct_size = harddisk_save_state_size;
  ss->unit = card->unit;
  ss->command = card->command;
  ss->result = card->result;
  ss->data_phase = card->data_phase;
  ss->block = card->block;
  ss->data_index = card->data_index;
  ss->block_count = card->block_count;

  *size = required;
  return peripheral_ok;
}

auto harddisk_abi_load_state(void* instance, const void* buffer, size_t size)
    -> PeripheralStatus_t {
  if (instance == nullptr || buffer == nullptr ||
      size < sizeof(HarddiskSaveState_t)) {
    return peripheral_error;
  }

  HarddiskSaveState_t ss{};
  std::memcpy(&ss, buffer, sizeof(ss));
  if (ss.version != HARDDISK_STATE_VERSION ||
      ss.struct_size != harddisk_save_state_size ||
      ss.data_phase > harddisk_phase_write_in) {
    return peripheral_error;
  }

  auto* card = static_cast<HarddiskPeripheral_t*>(instance);
  card->unit = ss.unit;
  card->command = ss.command;
  card->result = ss.result;
  card->block = ss.block;
  card->data_index =
      static_cast<uint16_t>(ss.data_index % physical::block_size);
  card->block_count = ss.block_count;
  card->data_phase = ss.data_phase;
  card->buffer_poisoned = false;
  card->buffer.fill(0);

  // The frame names no image: whatever the host mounted from its
  // configuration is what the saved session resumes against, and the log is
  // the only record of which that was.
  if (card->host != nullptr && card->host->Log != nullptr) {
    const Harddisk_t& drive0 = card->drives.at(0);
    const Harddisk_t& drive1 = card->drives.at(1);
    card->host->Log(card, log_info,
                    "Hard disk: resuming against drive 1 '%s' and drive 2 "
                    "'%s'\n",
                    drive0.is_loaded ? drive0.full_path.c_str() : "(empty)",
                    drive1.is_loaded ? drive1.full_path.c_str() : "(empty)");
  }

  // A read in flight is re-read from the block the frame names; an empty
  // drive or a failed read leaves zeros, which is what an unplugged drive
  // gives. A write in flight is gone with its bytes: the buffer is marked so
  // the one WRITE that would have used it fails instead of writing zeros.
  if (card->data_phase == harddisk_phase_read_out) {
    Harddisk_t& drive = selected_drive(card);
    if (drive.is_loaded &&
        drive.driver->read_block(drive.driver_instance, card->block,
                                 card->buffer.data()) != harddisk_err_none) {
      card->buffer.fill(0);
    }
  } else if (card->data_phase == harddisk_phase_write_in) {
    card->buffer_poisoned = true;
    if (card->host != nullptr && card->host->Log != nullptr) {
      card->host->Log(card, log_warn,
                      "Hard disk: a block write in flight at the save was "
                      "lost; the write fails with an I/O error\n");
    }
  }

  notify_status_changed(card);
  return peripheral_ok;
}

}  // namespace

static const Peripheral_t g_harddisk_peripheral = {
    .abi_version = LINAPPLE_ABI_VERSION,
    .id = "linapple.harddisk",
    .name = "Harddisk",
    .description = "SmartPort hard disk controller emulation",
    .author = "LinApple Contributors",
    .version = VERSIONSTRING,
    .compatible_slots = PERIPHERAL_MASK_EXPANSION,
    .default_slot = harddisk_default_slot,
    .init = harddisk_abi_init,
    .reset = harddisk_abi_reset,
    .shutdown = harddisk_abi_shutdown,
    .think = nullptr,
    .on_vblank = nullptr,
    .save_state = harddisk_abi_save_state,
    .load_state = harddisk_abi_load_state,
    .command = harddisk_abi_command,
    .query = harddisk_abi_query};

// peripheral_register and ActivePeripheral_t::api still take a mutable
// Peripheral_t*, so the immutable descriptor is cast the same way
// PERIPHERAL_REGISTER casts it.
extern "C" auto harddisk_get_descriptor() -> Peripheral_t* {
  return const_cast<Peripheral_t*>(&g_harddisk_peripheral);
}

PERIPHERAL_REGISTER(g_harddisk_peripheral)
