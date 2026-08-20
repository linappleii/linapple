# Installation

### Prerequisites

LinApple uses **CMake** as its build system.

#### Debian / Ubuntu / RetroPie

```bash
sudo apt-get update
sudo apt-get install git g++ cmake libzip-dev libsdl3-dev libsdl3-image-dev libcurl4-openssl-dev zlib1g-dev imagemagick
```

#### Fedora / RHEL / CentOS

```bash
sudo dnf install git gcc-c++ cmake SDL3-devel SDL3_image-devel libcurl-devel libzip-devel ImageMagick
```

#### Arch Linux

```bash
sudo pacman -Syu
sudo pacman -S base-devel cmake imagemagick libzip sdl3 sdl3_image libcurl-gnutls zlib
```

### Clone

```bash
git clone https://github.com/linappleii/linapple.git
cd linapple
```

### Configure and Compile

```bash
# Create a build directory and configure with CMake (skipping tests for a faster build)
cmake -B build -DBUILD_TESTING=OFF

# Compile using all available CPU cores
cmake --build build -j$(nproc)
```

#### Build Options
You can pass various options to the `cmake` configuration step:
- `-DFRONTEND=sdl3` : (Default) Build the emulator with the modern SDL3-based graphical frontend (recommended for Ubuntu 26.04+, Fedora 40+, Arch Linux).
- `-DFRONTEND=sdl2` : Build with the legacy SDL2 graphical frontend (recommended for Debian 12, Ubuntu 22.04 LTS, or older distributions where SDL3 is not pre-packaged).
- `-DFRONTEND=sdl1` : Build with the legacy SDL1.2 graphical frontend for vintage/embedded systems.
- `-DFRONTEND=tui` : Build for the terminal using 24-bit color and Unicode characters (no GUI/SDL dependencies required).
- `-DFRONTEND=headless` : Build the emulator without GUI or SDL dependencies (useful for automated testing or server environments).
- `-DFRONTEND=all` : Build separate binaries for all supported frontends (`linapple-sdl3`, `linapple-sdl2`, `linapple-sdl1`, `linapple-tui`, `linapple-headless`).
- `-DENABLE_ASAN=ON` : Enable AddressSanitizer for memory bug and leak detection.
- `-DENABLE_UBSAN=ON` : Enable UndefinedBehaviorSanitizer for undefined behavior detection.
- `-DENABLE_CLANG_TIDY=ON` : Enable `clang-tidy` static analysis during build.
- `-DBUILD_TESTING=OFF` : Skip building the test suite (saves compilation time and dependency fetching).
- `-DREGISTRY_WRITEABLE=ON` : Enable saving emulator configuration settings back to the config file.
- `-DPROFILING=ON` : Enable `gprof` profiling output.
- `-DCMAKE_BUILD_TYPE=Debug` : Build with debugging symbols instead of release optimizations.

### Run Locally

After building, you can run the emulator directly from the build output directory:

```bash
cd build
./linapple
```

Or, to boot automatically into the standard Apple floppy disk provided with LinApple:

```bash
./linapple --autoboot --d1 ../res/Master.dsk
```

### Installation

To install LinApple so it can be run from anywhere, use the `install` target.

```bash
cmake --install build
```

#### XDG Compliance (Linux)
The build system uses standard `GNUInstallDirs` and automatically adapts to your privileges:
- **Non-root install (Default):** If you run `cmake --install build` as a regular user without overriding the prefix, it will install to `~/.local/bin/`, `~/.local/share/linapple/`, and `~/.config/linapple/`. This is fully compliant with XDG Base Directory specifications and does not require `sudo`.
- **System-wide install (Root):** If you run `sudo cmake --install build`, it will install system-wide to `/usr/local/bin/`, `/usr/local/share/linapple/`, and `/usr/local/etc/linapple/`.

You can also explicitly define your installation prefix during configuration:
```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr
sudo cmake --install build
```

After installation, simply run:
```bash
linapple
```

### Configuration and Assets

LinApple expects to find its configuration file (`linapple.conf`) in your configuration directory (e.g., `~/.config/linapple/linapple.conf`).

If the emulator cannot find a required asset (like `Master.dsk` or character fonts) in the current directory, it will automatically search the `share` and `config` directories established during installation.
