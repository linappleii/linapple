Installation
============

Prerequisites
-------------

LinApple uses __CMake__ as its build system.

    #   Debian / Ubuntu / RetroPie
    sudo apt update
    sudo apt install git g++ cmake \
        libzip-dev libcurl4-openssl-dev zlib1g-dev imagemagick
    sudo apt install libsdl3-dev libsdl3-image-dev      # Debian 13+

    #   Fedora / RHEL / CentOS
    sudo dnf install git gcc-c++ cmake libcurl-devel libzip-devel ImageMagick
    sudo dnf SDL3-devel SDL3_image-devel

    #   Arch Linux
    sudo pacman -Syu
    sudo pacman -S base-devel cmake libcurl-gnutls zlib libzip imagemagick \
        sdl3 sdl3_image

Clone the repo with:

    git clone https://github.com/linappleii/linapple.git
    cd linapple


Configure and Compile
---------------------

    #   Create a build directory and configure with CMake (skipping tests
    #   for a faster build). For further configuration options, see below.
    cmake -B build -DBUILD_TESTING=OFF

    # Compile using all available CPU cores
    cmake --build build -j$(nproc)

#### Configuration Options

You can pass various options to the `cmake` configuration step:
- `-DFRONTEND=sdl3` : (Default) Build the emulator with the SDL3-based
  graphical frontend.
- `-DFRONTEND=tui` : Build for the terminal using 24-bit color and Unicode
  characters (no GUI dependencies required).
- `-DFRONTEND=headless` : Build the emulator without GUI or SDL
  dependencies (useful for automated testing or server environments).
- `-DBUILD_TESTING=OFF` : Skip building the test suite (saves compilation
  time and dependency fetching).
- `-DREGISTRY_WRITEABLE=ON` : Enable saving emulator configuration settings
  back to the config file.
- `-DFRONTEND=sdl3` : (Default) Build the emulator with the SDL3-based
  graphical frontend.
- `-DFRONTEND=headless` : Build the emulator without GUI or SDL
  dependencies (useful for automated testing or server environments).
- `-DBUILD_TESTING=OFF` : Skip building the test suite (saves compilation
  time and dependency fetching).
- `-DREGISTRY_WRITEABLE=ON` : Enable saving emulator configuration settings
  back to the config file.
- `-DPROFILING=ON` : Enable `gprof` profiling output.
- `-DCMAKE_BUILD_TYPE=Debug` : Build with debugging symbols instead of
  release optimizations.

### Run Locally to Test

After building, you can run the emulator directly from the build output
directory:

    cd build
    ./linapple

    #   Or to use the standard floppy image provided with LinApple:
    ./linapple --autoboot --d1 ../res/Master.dsk


Installation
------------

To install LinApple so it can be run from anywhere, use the `install` target.

    cmake --install build

#### XDG Compliance (Linux)

The build system uses standard `GNUInstallDirs` and automatically adapts to
your privileges:
- __Non-root install (Default):__ If you run `cmake --install build` as a
  regular user without overriding the prefix, it will install to
  `~/.local/bin/`, `~/.local/share/linapple/`, and `~/.config/linapple/`.
  This is fully compliant with XDG Base Directory specifications and does
  not require `sudo`.
- __System-wide install (Root):__ If you run `sudo cmake --install build`,
  it will install system-wide to `/usr/local/bin/`,
  `/usr/local/share/linapple/`, and `/usr/local/etc/linapple/`.

You can also explicitly define your installation prefix during configuration:

    cmake -B build -DCMAKE_INSTALL_PREFIX=/usr
    sudo cmake --install build

After installation, simply run:

    linapple

### Configuration and Assets

LinApple expects to find its configuration file (`linapple.conf`) in your
configuration directory (e.g., `~/.config/linapple/linapple.conf`).

If the emulator cannot find a required asset (like `Master.dsk` or
character fonts) in the current directory, it will automatically search the
`share` and `config` directories established during installation.
