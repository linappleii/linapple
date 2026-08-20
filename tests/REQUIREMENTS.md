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
*   **Hardware Feature:** Keyboard Input Register ($C000) and Clear Strobe ($C010).
*   **Functional Requirement:**
    *   $C000 must reflect the last key pressed. Bit 7 must be '1' if a new key is available.
    *   $C010 must reset bit 7 of $C000 to '0' upon access (read or write).
*   **Expected Behavior:** If bit 7 of $C000 is high, accessing $C010 must immediately cause bit 7 of $C000 to become low.

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

### [IOU-01] VBL (Vertical Blanking) Interrupts
*   **Hardware Feature:** VBL Signal ($C019).
*   **Functional Requirement:**
    *   Bit 7 of $C019 must be high during the vertical blanking interval (approximately 60Hz NTSC).
    *   The signal must remain high for the duration of the non-visible scanlines.
*   **Expected Behavior:** Software polling $C019 must be able to synchronize screen updates to avoid tearing.

## 3. Peripheral Hardware (Common)

### [DSK-01] Disk II Controller Interaction
*   **Hardware Feature:** Disk II Soft-switches ($C0E0-$C0EF).
*   **Functional Requirement:**
    *   Accessing $C0E8/$C0E9 must toggle the disk motor state.
    *   Accessing $C0EA/$C0EB must select the active drive (1 or 2).
    *   Stepper phases ($C0E0-$C0E7) must be toggled in sequence to move the drive head across tracks.
*   **Expected Behavior:** Accessing $C0E9 followed by $C0E8 must result in the drive motor being enabled and then disabled. Sequential access to stepper phases (e.g., Phase 0, then Phase 1) must be registered by the hardware as a head movement request.
