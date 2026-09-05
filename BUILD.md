# Building LinApple

LinApple uses **CMake** (3.12+) as its build system.

## 1. Quick Start (The Happy Path)

If you just cloned the repository and want to compile and run LinApple with
default settings (modern SDL3 frontend and all hardware enabled):

### Step 1: Install Dependencies

Choose your Linux distribution:

```bash
# Debian 13+ / Ubuntu 24.10+ / Linux Mint / Pop!_OS / Raspberry Pi OS
sudo apt-get update && sudo apt-get install -y git g++ cmake libzip-dev libcurl4-openssl-dev zlib1g-dev imagemagick libsdl3-dev libsdl3-image-dev

# Debian 12 / Ubuntu 24.04 / Linux Mint / Pop!_OS
sudo apt-get update && sudo apt-get install -y git g++ cmake libzip-dev libcurl4-openssl-dev zlib1g-dev imagemagick libsdl2-dev libsdl2-image-dev

# Arch Linux / Manjaro / EndeavourOS / CachyOS
sudo pacman -Syu --needed base-devel git cmake libzip libcurl-gnutls zlib imagemagick sdl3 sdl3_image

# Fedora / RHEL / AlmaLinux / Rocky Linux
sudo dnf install -y git gcc-c++ cmake libzip-devel libcurl-devel zlib-devel ImageMagick SDL3-devel SDL3_image-devel

# openSUSE (Tumbleweed / Leap)
sudo zypper install -y git gcc-c++ cmake libzip-devel libcurl-devel zlib-devel ImageMagick SDL3-devel SDL3_image-devel

# Alpine Linux
sudo apk add git g++ cmake make libzip-dev curl-dev zlib-dev imagemagick sdl3-dev sdl3_image-dev
```

*(For older distributions where SDL3 is not yet packaged, see the
[Frontend Selection](#frontend-selection--dfrontend) section below.)*

### Step 2: Clone & Compile

```bash
git clone https://github.com/linappleii/linapple.git
cd linapple

# Configure build directory (skipping tests for fastest build time)
cmake -B build -DBUILD_TESTING=OFF

# The following must build for SDL2: Debian 12 / Ubuntu 24.04 / Linux Mint / Pop!_OS
cmake -B build -DBUILD_TESTING=OFF -DFRONTEND=sdl2

# Compile across all CPU cores
cmake --build build -j$(nproc)
```

### Step 3: Run

You can run the compiled binary immediately directly from the build folder:

```bash
# Launch with splash screen
./build/linapple

# Boot directly into the included Apple II Master floppy disk
./build/linapple --autoboot --d1 res/Master.dsk

# Boot a hard disk image (2MG / HDV)
./build/linapple --autoboot --hd1 /path/to/disk.2mg
```

## 2. Installing System-Wide (XDG Compliant)

LinApple fully complies with XDG Base Directory standards. You can install it
either user-locally (without root) or system-wide:

* **User-Local Install (Default / No Root):**

  ```bash
  cmake --install build
  ```

  Installs without `sudo` to:
  * Binary: `~/.local/bin/linapple`
  * Assets & Disk Images: `~/.local/share/linapple/`
  * Default Config: `~/.config/linapple/linapple.conf`

* **System-Wide Install (Root):**

  ```bash
  sudo cmake --install build
  ```

  Installs to:
  * Binary: `/usr/local/bin/linapple`
  * Assets: `/usr/local/share/linapple/`
  * Config: `/usr/local/etc/linapple/linapple.conf`

* **Distribution Packaging Prefix:**

  ```bash
  cmake -B build -DCMAKE_INSTALL_PREFIX=/usr
  sudo cmake --install build
  ```

## 3. Running the Test Suite

LinApple includes comprehensive unit, integration, and CPU test suites
(powered by `doctest`):

```bash
# Configure with testing enabled
cmake -B build -DBUILD_TESTING=ON

# Build and execute all test suites
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

## 4. Advanced Customization & Build Options

For package maintainers, embedded developers, or power users wanting to tailor
LinApple, CMake provides fine-grained compile-time flags.

Pass flags to CMake during configuration using `-D<OPTION>=<VALUE>`.

### Frontend Selection (`-DFRONTEND=...`)

LinApple features a decoupled architecture supporting multiple frontend
implementations:

| Target            | CMake Flag                    | Audio / Video Dependencies | Primary Use Case                                     |
| :---------------- | :---------------------------- | :------------------------- | :--------------------------------------------------- |
| **SDL3**          | `-DFRONTEND=sdl3` *(Default)* | SDL3, SDL3_image           | Modern Linux desktops (Wayland / X11 / KMSDRM)       |
| **Terminal TUI**  | `-DFRONTEND=tui`              | PulseAudio / ALSA (No GUI) | Terminal/SSH play with 24-bit Truecolor & Unicode    |
| **Headless**      | `-DFRONTEND=headless`         | None (No video/audio)      | Automated CI testing and headless batch scripting    |
| **SDL2**          | `-DFRONTEND=sdl2`             | SDL2                       | Legacy Linux distros without native SDL3             |
| **SDL1**          | `-DFRONTEND=sdl1`             | SDL1.2, SDL_image 1.2      | Retro handhelds, vintage Linux, and embedded systems |
| **All Frontends** | `-DFRONTEND=all`              | All frontend dependencies  | Builds separate binaries for every frontend          |

### Hardware & Expansion Cards (`-DENABLE_PERIPHERAL_<NAME>=ON|OFF`)

By default, all standard Apple II hardware is compiled directly into the binary.
You can selectively enable or disable individual expansion cards:

| Card / Subsystem        | Option                           | Default | Description                                                                              |
| :---------------------- | :------------------------------- | :-----: | :--------------------------------------------------------------------------------------- |
| **Disk II Controller**  | `ENABLE_PERIPHERAL_DISK`         |   ON    | 5.25" floppy controller & image drivers (`.dsk`, `.do`, `.po`, `.nib`, `.woz`, FTP)      |
| **SmartPort Hard Disk** | `ENABLE_PERIPHERAL_HARDDISK`     |   ON    | SmartPort hard drive controller, block storage drivers, `.2mg`, `.hdv`, and `.po` images |
| **Mockingboard Sound**  | `ENABLE_PERIPHERAL_MOCKINGBOARD` |   ON    | Dual AY-3-8910 sound chips and 6522 VIA multi-channel audio synthesis                    |
| **Built-in Speaker**    | `ENABLE_PERIPHERAL_SPEAKER`      |   ON    | Standard 1-bit Apple II toggle speaker and audio DAC mixer                               |
| **Keyboard Encoder**    | `ENABLE_PERIPHERAL_KEYBOARD`     |   ON    | Apple II keyboard matrix encoder and international layout maps                           |
| **Apple Mouse Card**    | `ENABLE_PERIPHERAL_MOUSE`        |   ON    | 6821 PIA-based Apple II mouse interface card                                             |
| **Super Serial Card**   | `ENABLE_PERIPHERAL_SUPER_SERIAL` |   ON    | 6551 ACIA communications interface card (SSC)                                            |
| **Joystick Port**       | `ENABLE_PERIPHERAL_JOYSTICK`     |   ON    | Analog gameport timers, paddles, and button inputs                                       |
| **No-Slot Clock**       | `ENABLE_PERIPHERAL_CLOCK`        |   ON    | Dallas DS1216 / DS1315 real-time clock under ROM                                         |
| **Parallel Printer**    | `ENABLE_PERIPHERAL_PRINTER`      |   ON    | Parallel printer card dumping text output to file/stdout                                 |

#### Modular Shared Plugins (`-DBUILD_SHARED_PERIPHERALS=ON`)

By default, all enabled peripherals are statically compiled into the main
executable. Setting `-DBUILD_SHARED_PERIPHERALS=ON` compiles compatible
peripheral cards as dynamically loaded `.so` plugins installed to
`lib/linapple/plugins/`.

### Core Features & Diagnostics

| Option               | Default              | Description                                                                         |
| :------------------- | :------------------- | :---------------------------------------------------------------------------------- |
| `ENABLE_DEBUGGER`    | `ON` *(GUI)* / `OFF` | Builds the interactive assembly debugger, disassembly engine, and memory inspector  |
| `REGISTRY_WRITEABLE` | `OFF`                | Allows emulator runtime settings to be saved directly back to `linapple.conf`       |
| `PROFILING`          | `OFF`                | Enables compiler profiling flags (`-pg`)                                            |
| `ENABLE_ASAN`        | `OFF`                | Compiles with AddressSanitizer (`-fsanitize=address`) for memory safety diagnostics |
| `ENABLE_UBSAN`       | `OFF`                | Compiles with UndefinedBehaviorSanitizer (`-fsanitize=undefined`)                   |
| `ENABLE_FUZZING`     | `OFF`                | Builds `libFuzzer` harnesses with Clang (`fuzz-memory`, `fuzz-snapshot`, etc.)      |
| `ENABLE_CLANG_TIDY`  | `OFF`                | Runs `clang-tidy` static analysis checks during the build process                   |

## 5. Common Configuration Recipes

Here are ready-to-use build commands for common scenarios:

### Full Desktop Build (Default SDL3)

```bash
cmake -B build -DFRONTEND=sdl3 -DBUILD_TESTING=OFF
cmake --build build -j$(nproc)
```

### Distribution Package Build (Shared Plugins & System Prefix)

```bash
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DFRONTEND=sdl3 \
  -DBUILD_SHARED_PERIPHERALS=ON
cmake --build build -j$(nproc)
```

### Lightweight Terminal Build (No GUI Dependencies)

```bash
cmake -B build \
  -DFRONTEND=tui \
  -DENABLE_DEBUGGER=OFF \
  -DBUILD_TESTING=OFF
cmake --build build -j$(nproc)
```

### CI / Developer Diagnostics (Headless + Sanitizers + Tests)

```bash
cmake -B build \
  -DFRONTEND=headless \
  -DENABLE_ASAN=ON \
  -DENABLE_UBSAN=ON \
  -DBUILD_TESTING=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```
