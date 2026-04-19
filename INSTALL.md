Installation
============

Prerequisites
-------------

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


Fetching and Building
---------------------

### Clone

```bash
git clone https://github.com/linappleii/linapple.git
cd linapple
```

### Configure and Compile

```bash
# Create a build directory and configure with CMake
cmake -B build

# Compile using all available CPU cores
cmake --build build -j$(nproc)
```

#### Build Options

You can pass various options to the `cmake` configuration step:
- `-DFRONTEND=sdl3` : (Default) Build the emulator with the SDL3-based graphical frontend.
- `-DFRONTEND=headless` : Build the emulator without GUI or SDL dependencies (useful for automated testing or server environments).
- `-DREGISTRY_WRITEABLE=ON` : Enable saving emulator configuration settings back to the config file.
- `-DPROFILING=ON` : Enable `gprof` profiling output.
- `-DCMAKE_BUILD_TYPE=Debug` : Build with debugging symbols instead of release optimizations.


Execution and Installation
--------------------------

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


Configuration Notes
-------------------

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
