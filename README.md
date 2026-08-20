# LinApple

[![Build & Tests](https://github.com/linappleii/linapple/actions/workflows/build.yml/badge.svg)](https://github.com/linappleii/linapple/actions/workflows/build.yml)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](LICENSE)
[![Language](https://img.shields.io/badge/C%2B%2B-11-blue.svg)](https://en.cppreference.com/w/cpp/11)
[![Frontend](https://img.shields.io/badge/Frontend-SDL3%20%7C%20SDL2%20%7C%20SDL1%20%7C%20TUI%20%7C%20Headless-brightgreen.svg)](INSTALL.md)

**LinApple** is a high-fidelity Apple ][, Apple ][+, Apple //e, and
Enhanced Apple //e emulator for modern Linux and POSIX systems.

## Quick Start

### 1. Install Prerequisites

Choose your distribution family:

```bash
# Debian / Ubuntu / Linux Mint / Pop!_OS / RetroPie
sudo apt-get update && sudo apt-get install -y git g++ cmake libzip-dev \
    libsdl3-dev libsdl3-image-dev libcurl4-openssl-dev zlib1g-dev imagemagick

# Arch Linux / CachyOS / Manjaro / EndeavourOS
sudo pacman -Syu --needed base-devel git cmake libzip libcurl-gnutls zlib \
    imagemagick sdl3 sdl3_image

# Fedora / RHEL / AlmaLinux / Rocky Linux
sudo dnf install -y git gcc-c++ cmake libzip-devel libcurl-devel zlib-devel \
    ImageMagick SDL3-devel SDL3_image-devel
```

*(For openSUSE, Alpine, or older distributions without SDL3, see
[INSTALL.md](INSTALL.md).)*

### 2. Clone & Build

```bash
git clone https://github.com/linappleii/linapple.git
cd linapple
cmake -B build -DBUILD_TESTING=OFF
cmake --build build -j$(nproc)
```

### 3. Run

```bash
# Boot the bundled Apple II Master floppy disk
./build/linapple --autoboot --d1 res/Master.dsk

# Or boot a hard disk image (e.g., Total Replay / 2MG)
./build/linapple --autoboot --hd1 /path/to/image.2mg
```

## Key Features

* **Authentic Hardware Emulation:**
  * MOS 6502 and 65C02 CPUs with cycle-accurate timing.
  * 128K memory, 80-column text card, and auxiliary RAM bank-switching.
  * Apple Mouse Card, Mockingboard / Phasor multi-channel sound, Super Serial
    Card (SSC), and No-Slot Clock.
  * Native analog & USB joystick support with paddle calibration.

* **Modern Multi-Frontend Architecture:**
  * **SDL3 Frontend (Default):** Hardware-accelerated Wayland, X11, and KMSDRM
    display output.
  * **Terminal TUI Frontend:** 24-bit Truecolor & Unicode rendering with zero
    GUI/SDL dependencies—play Apple II games directly inside your SSH terminal.
  * **Headless Frontend:** Fast CLI execution for CI test automation and batch
    scripts.
  * **SDL2 & SDL1 Frontends:** Available for older distributions and vintage
    embedded systems.

* **Flexible Storage & Disk Formats:**
  * Full read/write support for standard floppy images (`.dsk`, `.do`, `.po`,
    `.nib`, `.woz` v2).
  * Native SmartPort hard disk emulation with `.2mg` container parsing and raw
    `.hdv` block images.
  * Built-in direct FTP disk image streaming.

* **Configurable Keyboard System:**
  * **Symbolic & Positional** keyboard mapping modes.
  * **Custom Key Mapping (`[Keyboard.Custom]`):** Remap any host physical key
    to any Apple II character, control code, or Open/Closed Apple button.
  * Configurable Quick Save hotkeys (`Alt+0..9`) to eliminate conflicts with
    Apple II games (like *Lode Runner*).
  * Virtual character-set Rocker Switch for international IIe models.

* **Integrated Assembly Debugger & Diagnostics:**
  * Full-featured interactive disassembly viewer, memory inspector,
    breakpoint/watchpoint engine, and CPU benchmark.
  * `--list-hardware` and `--hardware-info` CLI flags for inspecting emulator
    internals.

* **Standards-Compliant:**
  * Fully XDG Base Directory compliant (`~/.config/linapple/`,
    `~/.local/share/linapple/`).

## Project Background

LinApple originally began as a Linux port of the classic [AppleWin] emulator.
Over the years, the original SourceForge project was abandoned, resulting in
dozens of fragmented independent forks scattered across GitHub.

This repository under the [linappleii] organization is an active effort to
unify those community improvements and modernize the emulator—introducing a
tiered modular architecture, modern SDL3 and Terminal TUI frontends, 2MG hard
disk support, XDG compliance, and comprehensive automated test suites.

[AppleWin]: https://github.com/AppleWin/AppleWin
[linappleii]: https://github.com/linappleii

## Controls & Hotkeys

### Apple II Special Keys

| Apple II Key                       | Host Keyboard Equivalent                |
| :--------------------------------- | :-------------------------------------- |
| **Open Apple (Paddle 0 Button)**   | `Super` / `GUI` (Windows / Command key) |
| **Closed Apple (Paddle 1 Button)** | `Alt` (Left or Right Alt)               |
| **Reset (Ctrl + Reset)**           | `Ctrl + F10`                            |

### Emulator Shortcuts

| Shortcut                        | Action                                                          |
| :------------------------------ | :-------------------------------------------------------------- |
| **`F1`**                        | Show in-emulator Help screen                                    |
| **`F2`** / **`Ctrl + F2`**      | Restart emulator / Cold reboot                                  |
| **`F3` / `F4`**                 | Insert disk into Floppy Drive 1 / Drive 2                       |
| **`Shift + F3` / `Shift + F4`** | Insert hard disk into Drive 1 / Drive 2 (Slot 7)                |
| **`Ctrl + F3` / `Ctrl + F4`**   | Eject floppy disk from Drive 1 / Drive 2                        |
| **`Ctrl + Shift + F3` / `F4`**  | Eject hard disk from Drive 1 / Drive 2                          |
| **`F5`**                        | Swap Drive 1 and Drive 2 floppy disks                           |
| **`F6`**                        | Toggle Fullscreen mode                                          |
| **`Shift + F6`**                | Toggle international keyboard/video Rocker Switch               |
| **`F7`**                        | Toggle integrated assembly debugger                             |
| **`F8`**                        | Save screenshot (`.bmp`)                                        |
| **`F9`**                        | Cycle video rendering modes (Monochrome, Color, Composite, RGB) |
| **`F10` / `F11`**               | Load / Save snapshot state file                                 |
| **`Alt + 0..9`**                | Quick Load state slot 0–9 *(configurable in `linapple.conf`)*   |
| **`Alt + Shift + 0..9`**        | Quick Save state slot 0–9                                       |
| **`Pause`**                     | Pause / Resume emulation                                        |
| **`Scroll Lock`**               | Toggle unthrottled maximum emulation speed                      |
| **`Numpad +` / `-` / `*`**      | Increase / Decrease / Reset emulation speed                     |
| **`F12`**                       | Quit LinApple                                                   |

*(Note: If function keys conflict with your Linux window manager, set
`Enable Hotkeys = 0` in `linapple.conf`.)*

## Command Line Usage

```bash
linapple [options]
```

### Common Options

| Option                        | Description                                                     |
| :---------------------------- | :-------------------------------------------------------------- |
| **`-1`, `--d1 <file>`**       | Insert floppy disk image in Drive 1 (Slot 6)                    |
| **`-2`, `--d2 <file>`**       | Insert floppy disk image in Drive 2 (Slot 6)                    |
| **`--hd1 <file>`**            | Insert hard disk image in Drive 1 (Slot 7, e.g. `.2mg`, `.hdv`) |
| **`--hd2 <file>`**            | Insert hard disk image in Drive 2 (Slot 7)                      |
| **`-a`, `-b`, `--autoboot`**  | Automatically boot into inserted disk on startup                |
| **`-c`, `--config <file>`**   | Load specific configuration file                                |
| **`-f`, `--fullscreen`**      | Start in fullscreen mode                                        |
| **`-p`, `--pal`**             | Enable PAL (50Hz) video timing instead of NTSC (60Hz)           |
| **`-P`, `--program <file>`**  | Load and execute raw `.apl` / `.prg` program image              |
| **`-s`, `--snapshot <file>`** | Restore emulator state from snapshot file                       |
| **`-x`, `--script <file>`**   | Run debugger batch script on startup                            |
| **`--list-hardware`**         | Print all emulated hardware modules and exits                   |
| **`--no-debugger`**           | Disable debugger shortcuts and memory overhead                  |
| **`-h`, `--help`**            | Show full command-line help and target frontend                 |

## Configuration

LinApple loads configuration settings from `linapple.conf`. The search order
follows standard XDG rules:

1. Path supplied via `--config <path>`
2. User configuration: `~/.config/linapple/linapple.conf` (or
   `$XDG_CONFIG_HOME/linapple/linapple.conf`)
3. System configuration: `/etc/xdg/linapple/linapple.conf` (or
   `/etc/linapple/linapple.conf`)
4. Embedded application defaults

A fully commented reference configuration template is available in
[`res/linapple.conf`](res/linapple.conf).

## License

LinApple is distributed under the **GNU General Public License v2.0 (GPL-2.0)**.
See [LICENSE](LICENSE) for details.
