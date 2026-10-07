# Clean-Room Hardware Verification Requirements

This document specifies the functional behavior of Apple II series hardware, derived from official diagnostic suites. It serves as the "black box" specification for hardware-accurate emulation.

## 1. Apple II / Apple II Plus Generation

### [MEM-01] Language Card RAM Banking
*   **Hardware Feature:** 16K expansion RAM in the $D000-$FFFF address space.
*   **Functional Requirement:**
    *   The hardware must support two independent 4K banks at $D000-$DFFF.
    *   State switching is controlled by reading specific addresses in the $C080-$C08F range.
    *   **RAM Read Enable:** A single read to an "even" address (e.g., $C080, $C088) must enable RAM reading.
    *   **RAM Write Enable:** Two consecutive reads to a "write-enable" address (e.g., $C081, $C083) must allow subsequent writes to RAM.
    *   **ROM/RAM Toggle:** Accessing $C082 or $C08A must disable RAM reading and enable motherboard ROM reading.
*   **Expected Behavior:** Sequence `LDA $C081 / LDA $C081` followed by `STA $D000 / LDA $D000` must result in the value read matching the value written, while `LDA $C082` followed by `LDA $D000` must return the value from the system ROM.

### [ROM-01] Firmware Integrity
*   **Hardware Feature:** System ROMs ($D000-$FFFF).
*   **Functional Requirement:**
    *   The $F800-$FFFF region (Autostart ROM) must contain specific entry points (e.g., $FF65 for Monitor) and a constant checksum.
    *   The $D000-$F7FF region must correctly map either Applesoft BASIC or Integer BASIC depending on the motherboard configuration.
*   **Expected Behavior:** Summation of bytes in the $F800-$FFFF range must match the specific "Apple II" or "Apple II Plus" signature values.

### [IO-01] Keyboard Strobe and Data
*   **Hardware Feature:** Keyboard Input Register ($C000) and Clear Strobe ($C010-$C01F).
*   **Functional Requirement:**
    *   $C000 must reflect the last key pressed. Bit 7 must be '1' if a new key is available.
    *   Any access to $C010, read or write, must reset bit 7 of $C000 to '0' on every model.
    *   On the II and II Plus any access to $C011-$C01F, read or write, must reset it as well, and a read of $C010-$C01F returns the undriven bus; on the //e only a write to $C011-$C01F resets it, a read there returning an MMU or IOU flag in bit 7 and leaving the strobe set, and a read of $C010 returns the any-key-down flag in bit 7.
*   **Expected Behavior:** If bit 7 of $C000 is high, accessing $C010 must immediately cause bit 7 of $C000 to become low; on a II Plus `LDA $C011` does the same, while on a //e it does not.

---

## 2. Apple IIe Generation

### [MMU-01] Auxiliary Memory Management
*   **Hardware Feature:** Bank-switching between 64K Main RAM and 64K Auxiliary RAM.
*   **Functional Requirement:**
    *   **RAMRD ($C002/$C003):** Reading from $0200-$BFFF must be switchable between Main and Aux RAM.
    *   **RAMWRT ($C004/$C005):** Writing to $0200-$BFFF must be switchable between Main and Aux RAM.
    *   **ALTZP ($C008/$C009):** The Zero Page ($00-$FF) and Stack ($0100-$01FF) must be switchable as a single unit between Main and Aux RAM.
    *   **80STORE ($C000/$C001):** When ON, video page switching ($C054/$C055) must redirect video RAM access to Aux RAM, overriding RAMRD/RAMWRT for those specific regions.
*   **Expected Behavior:** With RAMWRT Aux ($C005) and RAMRD Main ($C002), a write to $2000 followed by a read from $2000 must return the *original* Main RAM value, not the newly written Aux RAM value.

### [VID-01] 80-Column and Double Hi-Res Logic
*   **Hardware Feature:** Extended video modes using interleaved memory.
*   **Functional Requirement:**
    *   **80COL ($C00C/$C00D):** In 80-column mode, the hardware must fetch even-numbered columns from Aux RAM and odd-numbered columns from Main RAM.
    *   **DHIRES ($C05E/$C05F):** When enabled alongside HIRES and 80COL, the hardware must double the horizontal resolution by interleaving Main and Aux Hi-Res Page 1 ($2000-$3FFF).
*   **Expected Behavior:** Enabling 80-column mode and writing different values to Main $0400 and Aux $0400 must result in two distinct characters appearing in the first two horizontal positions of the first text row.

### [KBD-01] Apple Keys and Joystick Integration
*   **Hardware Feature:** Open-Apple (OA) and Closed-Apple (CA) keys.
*   **Functional Requirement:**
    *   The OA key must be electrically mapped to Game Button 0 ($C061).
    *   The CA key must be electrically mapped to Game Button 1 ($C062).
*   **Expected Behavior:** Depressing the Open-Apple key must cause bit 7 of $C061 to transition from '0' to '1'.

### [IOU-01] Vertical Blanking Status (RDVBLBAR)
*   **Hardware Feature:** VBL status soft switch ($C019).
*   **Functional Requirement:**
    *   $C019 is VBL inverted (RDVBLBAR): bit 7 must be high (value 128 or more) while the visible lines 0-191 are drawn and low (value below 128) during the vertical blanking lines 192-261, as the Apple IIe Technical Reference (p. 170) specifies.
    *   On the NTSC frame of 262 lines at 65 cycles each, bit 7 must be low from frame cycle 12,480 through 17,029 and high from 0 through 12,479.
*   **Expected Behavior:** Software polling $C019 for bit 7 falling finds the start of blanking and can change display data there without tearing.

## 3. Peripheral Hardware (Common)

### [DSK-01] Disk II Controller Interaction
*   **Hardware Feature:** Disk II Soft-switches ($C0E0-$C0EF).
*   **Functional Requirement:**
    *   Accessing $C0E8/$C0E9 must toggle the disk motor state.
    *   Accessing $C0EA/$C0EB must select the active drive (1 or 2).
    *   Stepper phases ($C0E0-$C0E7) must be toggled in sequence to move the drive head across tracks.
*   **Expected Behavior:** Accessing $C0E9 followed by $C0E8 must result in the drive motor being enabled and then disabled. Sequential access to stepper phases (e.g., Phase 0, then Phase 1) must be registered by the hardware as a head movement request.

### [HDD-01] ProDOS Block Device Firmware

* **Hardware Feature:** A block-device controller's 256-byte firmware page at
  $Cn00, as the ProDOS 8 Technical Reference Manual specifies it (6.3.1 and
  6.3.2).
* **Functional Requirement:**
  * $Cn01, $Cn03 and $Cn05 must read $20, $00 and $03, the bytes ProDOS
    identifies a block device by; $Cn07 must not read $00, which would name a
    SmartPort interface (ProDOS 8 Technical Note #21).
  * $CnFF must hold the low byte of the driver entry, neither $00 nor $FF;
    the entry is $Cn46. $CnFE must have bits 0 and 1 set; $CnFC-$CnFD hold
    the block count or $0000, in which case ProDOS asks STATUS for it.
  * The driver takes its parameters at $42 (command: 0 STATUS, 1 READ, 2
    WRITE, 3 FORMAT), $43 (unit: drive in bit 7, slot in bits 6-4), $44-$45
    (a 512-byte buffer) and $46-$47 (a block number), and must leave them as
    it found them.
  * On success the carry is clear and A is zero (6.3.1 states this for
    STATUS; for READ, WRITE and FORMAT it is inferred, 6.3.2 giving only the
    error rule); STATUS also returns the block count low in X and high in Y.
    On failure the carry is set and A
    holds $27 (I/O error), $28 (no device connected) or $2B (write
    protected), the codes the MLI passes to the program (4.7.1-4.7.2, 4.8).
  * Entered at $Cn00, the firmware must read block 0 of drive 1 to $0800 and
    jump to $0801 with X holding the slot times 16; when STATUS or the READ
    of block 0 fails it must continue the Autostart Monitor's slot scan at
    $FABA, or enter the Monitor at $FF59 on an original II, whose Monitor has
    no scan.
* **Expected Behavior:** With `$42 = 0` and `$43 = $70`, `JSR $C746` returns
  with the carry clear, A zero and X/Y the volume's block count; with
  `$42 = 2` on a protected volume it returns with the carry set and A = $2B;
  with `$42 = 1` and `$46-$47` past the volume's end it returns with the
  carry set and A = $27 and leaves the buffer untouched.
