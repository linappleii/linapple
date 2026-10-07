# LinApple

[![Build & Tests](https://github.com/linappleii/linapple/actions/workflows/build.yml/badge.svg)](https://github.com/linappleii/linapple/actions/workflows/build.yml)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](COPYING.txt)
[![Language](https://img.shields.io/badge/C%2B%2B-11-blue.svg)](https://en.cppreference.com/w/cpp/11)
[![Language](https://img.shields.io/badge/C-99-blue.svg)](https://en.cppreference.com/w/c/99)
[![Frontend](https://img.shields.io/badge/Frontend-SDL3%20%7C%20SDL2%20%7C%20SDL1%20%7C%20TUI%20%7C%20Headless-brightgreen.svg)](BUILD.md)

**LinApple** is a high-fidelity Apple ][, Apple ][+, Apple //e, and
Enhanced Apple //e emulator for modern Linux and POSIX systems.

## Installation & Quick Start

### Pre-built Packages & Binaries (Recommended)

Pre-compiled packages and portable binaries for Linux **x86_64** and **ARM64**
(Raspberry Pi OS 64-bit / Ubuntu ARM) are available on the
[Releases](https://github.com/linappleii/linapple/releases) page.

#### Native Packages (SDL3 Default)

* **Debian / Ubuntu / Raspberry Pi OS (`.deb`):**

  ```bash
  sudo apt-get install ./linapple-<tag>-linux-x86_64.deb   # on x86_64
  sudo apt-get install ./linapple-<tag>-linux-arm64.deb    # on ARM64
  ```

* **Fedora / RHEL (`.rpm`):**

  ```bash
  sudo dnf install ./linapple-<tag>-linux-x86_64.rpm
  ```

* **Arch Linux (`.pkg.tar.zst`):**

  ```bash
  sudo pacman -U ./linapple-<tag>-1-x86_64.pkg.tar.zst
  ```

#### Portable Release (No Installation Required)

Download and extract the portable archive:

```bash
tar -xvf linapple-<tag>-linux-x86_64.tar.gz
cd linapple-<tag>-linux-x86_64
./bin/linapple --autoboot --d1 share/linapple/Master.dsk
```

#### Standalone Binaries & Assets Archive

If using a standalone frontend binary (such as `linapple-sdl2`, `linapple-sdl1`,
`linapple-headless`, or `linapple-tui`), download the corresponding binary
archive alongside `linapple-<tag>-assets.tar.gz`:

1. **Extract assets to the standard user data directory:**

   ```bash
   mkdir -p ~/.local/share/linapple
   tar -xvf linapple-<tag>-assets.tar.gz -C ~/.local/share/linapple/
   ```

2. **Run your standalone binary:**

   ```bash
   ./bin/linapple-sdl2 --autoboot --d1 ~/.local/share/linapple/Master.dsk
   ```

### Building from Source

#### 1. Install Prerequisites

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
[BUILD.md](BUILD.md).)*

#### 2. Clone & Build

```bash
git clone https://github.com/linappleii/linapple.git
cd linapple
cmake -B build -DBUILD_TESTING=OFF
cmake --build build -j$(nproc)
```

#### 3. Run

```bash
# Boot the bundled Apple II Master floppy disk
./build/linapple --autoboot --d1 res/Master.dsk

# Or boot a ProDOS hard disk image (e.g., Total Replay as .hdv or .2mg)
./build/linapple --autoboot --hd1 /path/to/image.2mg
```

## Key Features

* **Authentic Hardware Emulation:**
  * MOS 6502 and 65C02 CPUs with cycle-accurate timing.
  * 128K memory, 80-column text card, and auxiliary RAM bank-switching.
  * AppleMouse II interface card running Apple's firmware, installed in any
    slot with `Slot n = Mouse Interface` under `[Slots]` in `linapple.conf`
    (the legacy `Mouse in slot 4 = 1` key still puts it in slot 4); the host
    mouse is captured only while a mouse card or a mouse-emulated joystick
    is there to use it. In the TUI the terminal's mouse drives the card, in
    pixels where the terminal reports them and in character cells otherwise,
    and the Apple pointer follows the host pointer: it sits where the host
    pointer points within the Apple screen, to about one hires pixel in a
    terminal that reports pixels and about 3.5 hires pixels per cell
    otherwise.
  * Mockingboard / Phasor multi-channel sound, and a ThunderClock-compatible
    ProDOS clock card.
  * Super Serial Card (SSC) running Apple's firmware, its RS-232 line cabled
    to a host serial port, a pseudo-terminal (`pty`) for terminal programs
    and `tcpser`, a loopback plug, or a file.
  * Native analog & USB joystick support with paddle calibration.

* **Modern Multi-Frontend Architecture:**
  * **SDL3 Frontend (Default):** Hardware-accelerated Wayland, X11, and KMSDRM
    display output.
  * **Terminal TUI Frontend:** 24-bit Truecolor & Unicode rendering with zero
    GUI/SDL dependencies—play Apple II games directly inside your SSH terminal.
  * **Headless Frontend:** Fast CLI execution for CI test automation and batch
    scripts.
  * **SDL2 & SDL1 Frontends:** Available for older distributions and vintage
    embedded systems. SDL 1.2 reads key positions (positional mode and
    `[Keyboard.Custom]`) through X11 only; under another video driver it
    falls back to the key's symbol.
  * **Peripheral cards as plugins:** every card builds into the binary or,
    with `BUILD_SHARED_PERIPHERALS`, as a plugin; a plugin and its host must
    come from the same build. Any card can be compiled out
    (`ENABLE_PERIPHERAL_<CARD>=OFF`). A //e built without the keyboard card
    behaves like one with its keyboard unplugged: with no game controller
    configured either (`Joystick 0 = 0`), the pull-up resistors of the
    revision C logic board make it run its self-test at every reset and loop
    in it, as the real board does; configure a controller or keep the
    keyboard to boot a disk. A II Plus built that way boots regardless.

* **Flexible Storage & Disk Formats:**
  * Full read/write support for standard floppy images (`.dsk`, `.do`, `.po`,
    `.nib`, `.woz` v2).
  * A ProDOS 8 block-device hard disk controller with firmware of its own,
    in any slot (`Slot n = Harddisk`; on a //e slot 3's page belongs to the
    built-in 80-column firmware, so the card is not reached there), with two
    drives.
    It serves `.hdv` / `.po` / `.img` block images, `.2mg` containers in
    ProDOS or DOS order, and 140 K floppy images (`.dsk`, `.do`) decoded
    through the DOS 3.3 sector map; nibble images are refused as holding no
    blocks. ProDOS sees the volume's real size and the real errors (`WRITE
    PROTECTED`, `NO DEVICE CONNECTED`), and with no image in drive 1 the
    boot moves on to the next slot down.
  * Built-in direct FTP disk image streaming.

* **Bidirectional Applesoft BASIC Live-Sync:**
  * Write and edit Applesoft BASIC code in any host text editor (VS Code,
    Vim) with real-time automatic synchronization into emulated memory
    (`--basic-sync <file>`).

* **Configurable Keyboard System:**
  * **Symbolic & Positional** keyboard mapping modes, with twelve national
    tables (`Keyboard Type`, 0 US to 11 Japanese kana) read in positional
    mode through the //e's rocker switch.
  * **Custom Key Mapping (`[Keyboard.Custom]`):** Remap any host physical key
    to any Apple II character, control code, Open/Solid Apple button or the
    REPT key of a II or II Plus keyboard; a remapped key applies whatever the
    mode and leaves every other key as it was.
  * Configurable Quick Save hotkeys (`Alt+0..9`) to eliminate conflicts with
    Apple II games (like *Lode Runner*).
  * Virtual character-set Rocker Switch for international IIe models.
  * The keyboard of each model: a //e repeats a held key after half a second,
    fifteen times a second on NTSC and twelve and a half on PAL; a II or II
    Plus types upper case and never repeats a key by itself, only through its
    REPT key.

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

| Apple II Key                      | Host Keyboard Equivalent                                    |
| :-------------------------------- | :---------------------------------------------------------- |
| **Open Apple (Paddle 0 Button)**  | `Left Alt` or `Left Super` / `GUI`; terminal: `Alt + key`   |
| **Solid Apple (Paddle 1 Button)** | `Right Alt` or `Right Super` / `GUI`; not in the terminal   |
| **Reset (Ctrl + Reset)**          | `Ctrl + F10`                                                |
| **REPT (II and II Plus)**         | Any key bound to `Rept` in `[Keyboard.Custom]`; none by default |

The Super (Windows / Command) key is usually claimed by the window manager
and may never reach the emulator, so Alt is the dependable choice.

In the terminal frontend `Alt + key` is Open Apple held with that key. It
arrives as `ESC` followed by the key in terminals that send a meta prefix
(xterm with `metaSendsEscape`, and most terminal emulators by default) or as
the key with its eighth bit set (stock xterm); a lone `ESC` still types
Escape. The terminal has no Solid Apple yet. Every control key, `Ctrl + C`
included, reaches the Apple from a terminal, so Applesoft's break and a
game's control bindings work there; `F12` quits. On a II or II Plus the
host's Alt keys are visible to a program only while a game controller is
configured (`Joystick 0` other than `0`): with nothing plugged into the game
connector its three pushbutton inputs float high and read as pressed, as on
the hardware. A //e reads them released through its keyboard's pull-down
resistors.

### Emulator Shortcuts

| Shortcut                        | Action                                                          |
| :------------------------------ | :-------------------------------------------------------------- |
| **`F1`**                        | Show in-emulator Help screen                                    |
| **`F2`** / **`Ctrl + F2`**      | Restart emulator / Cold reboot                                  |
| **`F3` / `F4`**                 | Insert disk into Floppy Drive 1 / Drive 2                       |
| **`Shift + F3` / `Shift + F4`** | Insert hard disk into Drive 1 / Drive 2 (the hard disk's slot) |
| **`Alt + F3` / `Alt + F4`**     | Browse and insert floppy disk image via FTP                     |
| **`Shift + Alt + F3 / F4`**     | Browse and insert hard disk image via FTP (the hard disk's slot) |
| **`Ctrl + F3` / `Ctrl + F4`**   | Eject floppy disk from Drive 1 / Drive 2                        |
| **`Ctrl + Shift + F3 / F4`**    | Eject hard disk from Drive 1 / Drive 2                          |
| **`F5`**                        | Swap Drive 1 and Drive 2 floppy disks                           |
| **`F6`**                        | Toggle Fullscreen mode                                          |
| **`Shift + F6`**                | Toggle the keyboard rocker switch (TUI only, no visible effect in this release; does nothing in the SDL frontends yet) |
| **`F7`**                        | Toggle integrated assembly debugger                             |
| **`F8`**                        | Save screenshot (`.bmp`)                                        |
| **`F9`**                        | Cycle video rendering modes (Monochrome, Color, Composite, RGB) |
| **`F10` / `F11`**               | Load / Save snapshot state file                                 |
| **`Alt + 0..9`**                | Quick Load state slot 0–9 *(configurable in `linapple.conf`)*   |
| **`Alt + Shift + 0..9`**        | Quick Save state slot 0–9                                       |
| **`Pause`**                     | Pause / Resume emulation                                        |
| **`Scroll Lock`**               | Toggle unthrottled maximum emulation speed                      |
| **`Numpad +` / `-` / `*`**      | Increase / Decrease / Reset emulation speed                     |
| **Middle / Shift- / Ctrl-click** | Capture or release the mouse (mouse card or mouse joystick)     |
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
| **`--hd1 <file>`**            | Insert hard disk image in Drive 1 (e.g. `.2mg`, `.hdv`, `.po`)  |
| **`--hd2 <file>`**            | Insert hard disk image in Drive 2 of the same card              |
| **`-a`, `-b`, `--autoboot`**  | Automatically boot into inserted disk on startup                |
| **`-c`, `--config <file>`**   | Load specific configuration file                                |
| **`-f`, `--fullscreen`**      | Start in fullscreen mode                                        |
| **`-p`, `--pal`**             | Enable PAL (50Hz) video timing instead of NTSC (60Hz)           |
| **`-P`, `--program <file>`**  | Load and execute raw `.apl` / `.prg` program image              |
| **`-s`, `--snapshot <file>`** | Restore emulator state from snapshot file                       |
| **`-x`, `--script <file>`**   | Run debugger batch script on startup                            |
| **`-m`, `--benchmark`**       | Run automated 1,000-frame video benchmark and exit              |
| **`-A`, `--audio-dump <f>`**  | Dump audio output to a RIFF WAV file                            |
| **`--basic-sync <file>`**     | Enable real-time bidirectional host Applesoft BASIC sync        |
| **`--basic-line-mode <m>`**   | Set BASIC sync line numbering (`explicit` or `positional`)      |
| **`--list-hardware`**         | Print all emulated hardware modules and exit                    |
| **`--hardware-info <name>`**  | Show detailed configuration for a specific hardware component   |
| **`--no-debugger`**           | Disable debugger shortcuts and memory overhead                  |
| **`--caps-mode <mode>`**      | Caps Lock mode: `host` (default) or `emulated`                  |
| **`-h`, `--help`**            | Show full command-line help and target frontend                 |

### Hard Disk Images

`--hd1` and `--hd2` are the two drives of one hard disk card. When the
configuration already defines the card, in any slot, the images are mounted
into it. When it does not, the card is installed for this run only: in slot
7 if that slot is empty, otherwise in the highest free slot below it (slot 3
is skipped on a //e, whose built-in 80-column firmware owns that page); below
slot 7, a warning names the slot it took. A card already in a slot is never
displaced, and when no slot is free the error says so and nothing is
mounted. `--hd2` alone installs the card the same way and fills drive 2.
Nothing of a run's install is written to the configuration; only the image a
drive holds is remembered, under `Harddisk Image 1` and `Harddisk Image 2`.

The Monitor's boot scan starts at slot 7 and boots the first card that
answers, so a hard disk installed below a Disk II is not reached by
`--autoboot`; boot it with `PR#n` or let ProDOS find it, since ProDOS
installs every block device it sees.

An image that cannot be mounted is reported and LinApple runs on: `--hd1
missing.hdv` logs `ERROR: could not insert hard disk image 'missing.hdv':
file not found or unreadable`, and the SDL3 and SDL2 frontends also show the
error in a dialog. A failed image is not remembered. A `Harddisk Image` saved
in the configuration mounts only into a hard disk the configuration defines;
with none, the log says the image was not mounted.

The hard disk reports its activity as the Disk II does, so `Disk Turbo = 1`
runs a hard disk transfer at full speed too, and `Disk Turbo = 0` turns that
off for both cards. A save state holds the controller's registers but not
which images were mounted: it resumes against whatever images the
configuration mounts, and the log (`--log`) names them.

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
See [COPYING.txt](COPYING.txt) for details.
