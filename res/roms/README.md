# LinApple ROM Image Provenance & Licensing

This directory contains Apple II firmware and peripheral ROM binary images utilized by the emulator.

## License & Legal Notice

All original Apple II, Apple ][+, Apple //e, Disk II, Super Serial Card, Mouse Interface, and Parallel interface firmware remain the intellectual property and copyright of **Apple Inc.** (formerly Apple Computer, Inc.).

Third-party peripheral firmware (Mockingboard, ThunderClock, TKClock) and international/clone computer system ROMs (Base64A, Microdigital TK3000 //e, Pravetz 82/8M/8C) are the copyright of their respective original manufacturers.

These binary images are distributed for non-commercial preservation, interoperability, and emulation purposes. If you are building custom distribution packages with strict free-software licensing guidelines (such as Debian Main or Fedora Free), you can configure CMake to exclude embedded ROMs or build LinApple using the `--rom <path>` CLI option to supply external user-provided ROM files.

---

## Catalog of Included ROM Images

### Official Apple System ROMs (Relocated from Upstream Byte Arrays)

| Filename | Size | Hardware Model | Description | Copyright / Source |
| :--- | :--- | :--- | :--- | :--- |
| `Apple2.rom` | 12,288 B | Apple ][ | Original Apple II system ROM ($D000-$FFFF) with Programmer's BASIC and Monitor | Apple Inc. (1977) |
| `Apple2_Plus.rom` | 12,288 B | Apple ][+ | Apple II Plus system ROM ($D000-$FFFF) with Applesoft II BASIC and Autostart Monitor | Apple Inc. (1979) |
| `Apple2e.rom` | 16,384 B | Apple //e | Apple //e Unenhanced system ROM (CD & EF ROMs, $C000-$FFFF) | Apple Inc. (1982) |
| `Apple2e_Enhanced.rom` | 16,384 B | Apple //e Enhanced | Apple //e Enhanced system ROM (CD & EF ROMs with 65C02 support and Mini-Assembler) | Apple Inc. (1985) |

### Video & Character Generator ROMs

| Filename | Size | Hardware Model | Description | Copyright / Source |
| :--- | :--- | :--- | :--- | :--- |
| `Apple2_Video.rom` | 2,048 B | Apple ][ / ][+ | Standard 2513/2716 5x7 uppercase character generator ROM | Apple Inc. |
| `Apple2e_Enhanced_Video.rom` | 4,096 B | Apple //e Enhanced | Enhanced 2732 character ROM containing uppercase, lowercase, and MouseText glyphs | Apple Inc. (1985) |
| `Apple2_JPlus_Video.rom` | 2,048 B | Apple II J-Plus | Character generator ROM with Japanese Katakana glyphs | Apple Inc. / Toray Industries |
| `Base64A_German_Video.rom` | 4,096 B | Base64A | Character generator ROM with German character set and umlauts | Base Computers |

### Peripheral & Expansion Card ROMs

| Filename | Size | Peripheral Card | Description | Copyright / Source |
| :--- | :--- | :--- | :--- | :--- |
| `DISK2.rom` | 256 B | Disk II Interface | Standard 16-sector Disk II controller boot ROM (Part 341-0027) | Apple Inc. (1980) |
| `DISK2-13sector.rom` | 256 B | Disk II Interface | Early 13-sector Disk II controller boot ROM (Part 341-0013 for DOS 3.2) | Apple Inc. (1978) |
| `SSC.rom` | 2,048 B | Super Serial Card | Apple Super Serial Card 6551 ACIA firmware ROM (Part 341-0065) | Apple Inc. (1981) |
| `MouseInterface.rom` | 2,048 B | Mouse Card | Apple Mouse Interface Card 6821 firmware ROM (Part 342-0285) | Apple Inc. (1984) |
| `Parallel.rom` | 256 B | Parallel Interface | Apple Parallel Printer Interface firmware ROM | Apple Inc. |
| `Mockingboard-D.rom` | 2,048 B | Mockingboard "D" | Sweet Micro Systems Sound/Speech peripheral firmware | Sweet Micro Systems |
| `ThunderClockPlus.rom` | 2,048 B | ThunderClock Plus | Thunderware Real-Time Clock firmware ROM | Thunderware Inc. (1980) |
| `TKClock.rom` | 2,304 B | TK Clock | Microdigital / Microtek Clock card firmware | Microdigital / Microtek |

### International & Clone System ROMs

| Filename | Size | Computer Model | Description | Origin / Copyright |
| :--- | :--- | :--- | :--- | :--- |
| `Apple2_JPlus.rom` | 12,288 B | Apple II J-Plus | Japanese localized Apple II system ROM with Katakana Monitor | Apple Inc. / Toray (Japan) |
| `Base64A.rom` | 49,152 B | Base64A | German Apple II / CP/M dual-architecture computer system firmware | Base Computers (Germany) |
| `TK3000e.rom` | 16,384 B | Microdigital TK3000 //e | Brazilian Apple //e compatible microcomputer system ROM | Microdigital Eletronica (Brazil) |
| `PRAVETS82.ROM` | 12,288 B | Pravetz 82 (ПРАВЕЦ 82) | Bulgarian Apple ][ clone system ROM with Cyrillic character support | IMKO / Pravetz (Bulgaria) |
| `PRAVETS8M.ROM` | 12,288 B | Pravetz 8M (ПРАВЕЦ 8M) | Bulgarian Apple ][+ clone dual-CPU (6502/Z80) system ROM | IMKO / Pravetz (Bulgaria) |
| `PRAVETS8C.ROM` | 16,384 B | Pravetz 8C (ПРАВЕЦ 8C) | Bulgarian Apple //e clone system ROM with 128KB memory banking | IMKO / Pravetz (Bulgaria) |
