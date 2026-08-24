# Installing and Building LinApple

## 1. Pre-built Packages & Binaries (Recommended)

Pre-built packages and portable binary archives for **x86_64** and **ARM64** (Raspberry Pi OS 64-bit / Ubuntu ARM) are available on the [Releases](https://github.com/linappleii/linapple/releases) page.

### Native Packages (SDL3 Default)

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

### Portable Tarballs

Download and extract the portable archive without needing root:

```bash
tar -xvf linapple-<tag>-linux-x86_64.tar.gz
cd linapple-<tag>-linux-x86_64
./bin/linapple --autoboot --d1 share/linapple/Master.dsk
```

### Standalone Legacy Binaries & Assets Archive

If you downloaded a standalone frontend binary (such as `linapple-sdl2`, `linapple-sdl1`, `linapple-headless`, or `linapple-tui`), download `linapple-<tag>-assets.tar.gz` and extract it to the standard XDG data directory:

```bash
mkdir -p ~/.local/share/linapple
tar -xvf linapple-<tag>-assets.tar.gz -C ~/.local/share/linapple/
./bin/linapple-sdl2 --autoboot --d1 ~/.local/share/linapple/Master.dsk
```

---

## 2. Building from Source

LinApple uses modern **CMake** (3.12+) as its build system.

### Prerequisites & Dependencies

Install the required compiler, build tools, and development libraries for your
distribution:

### Debian / Ubuntu / Linux Mint / Pop!_OS

```bash
sudo apt-get update
sudo apt-get install -y git g++ cmake libzip-dev libcurl4-openssl-dev zlib1g-dev \
                        imagemagick libsdl3-dev libsdl3-image-dev
```

*(For older distributions like Ubuntu 22.04 LTS or Debian 12 where SDL3 is not
pre-packaged, install `libsdl2-dev` and build with `-DFRONTEND=sdl2`.)*

### Fedora / RHEL / CentOS / AlmaLinux

```bash
sudo dnf install -y git gcc-c++ cmake libzip-devel libcurl-devel zlib-devel \
                    ImageMagick SDL3-devel SDL3_image-devel
```

### Arch Linux / Manjaro

```bash
sudo pacman -Syu --needed base-devel git cmake libzip libcurl-gnutls zlib \
                          imagemagick sdl3 sdl3_image
```

### openSUSE (Tumbleweed / Leap)

```bash
sudo zypper install -y git gcc-c++ cmake libzip-devel libcurl-devel zlib-devel \
                       ImageMagick SDL3-devel SDL3_image-devel
```

### Alpine Linux

```bash
sudo apk add git g++ cmake make libzip-dev curl-dev zlib-dev imagemagick \
             sdl3-dev sdl3_image-dev
```

## 2. Clone the Repository

```bash
git clone https://github.com/linappleii/linapple.git
cd linapple
```

## 3. Configure and Build

### Quick Build (Default SDL3 Frontend)

```bash
# Configure build directory (skipping tests for fastest build time)
cmake -B build -DBUILD_TESTING=OFF

# Compile using all available CPU cores
cmake --build build -j$(nproc)
```

### Frontend Options

LinApple supports several dedicated frontends via `-DFRONTEND=<name>`:

| Option                    | Description                                  | Target Use Case                                                                                 |
| :------------------------ | :------------------------------------------- | :---------------------------------------------------------------------------------------------- |
| **`-DFRONTEND=sdl3`**     | *(Default)* Modern SDL3 graphical frontend   | Modern Linux desktops (Wayland / X11 / KMSDRM)                                                  |
| **`-DFRONTEND=all`**      | Build separate binaries for all frontends    | Produces `linapple-sdl3`, `linapple-sdl2`, `linapple-sdl1`, `linapple-tui`, `linapple-headless` |
| **`-DFRONTEND=tui`**      | Terminal UI using 24-bit Truecolor & Unicode | SSH sessions, headless servers, lightweight consoles                                            |
| **`-DFRONTEND=headless`** | Minimal CLI build with no video/audio        | Automated CI test runners and batch scripts                                                     |
| **`-DFRONTEND=sdl2`**     | Legacy SDL2 graphical frontend               | Older Linux distributions without native SDL3                                                   |
| **`-DFRONTEND=sdl1`**     | Legacy SDL1.2 graphical frontend             | Embedded/retro Linux devices and vintage systems                                                |

#### Example: Building All Frontends

```bash
cmake -B build -DFRONTEND=all -DBUILD_TESTING=OFF
cmake --build build -j$(nproc)
```

### Additional Build Flags

* **`-DBUILD_TESTING=ON`** : Build unit and integration tests (uses `doctest`).
* **`-DBUILD_SHARED_PERIPHERALS=ON`** : Build expansion cards as dynamic `.so`
  plugins installed to `lib/linapple/plugins/` rather than built-in.
* **`-DENABLE_PERIPHERAL_<NAME>=OFF`** : Disable specific peripheral cards at
  compile time (e.g. `-DENABLE_PERIPHERAL_PRINTER=OFF`).
* **`-DENABLE_DEBUGGER=OFF`** : Disable the assembly debugger to minimize binary
  size.
* **`-DREGISTRY_WRITEABLE=ON`** : Enable persisting runtime settings directly
  to `linapple.conf`.
* **`-DENABLE_ASAN=ON`** : Enable AddressSanitizer for memory bug diagnostics.
* **`-DCMAKE_BUILD_TYPE=Debug`** : Build with debug symbols and without
  optimizations.

## 4. Running LinApple

### Running from the Build Directory

You can run the compiled binary immediately without installing:

```bash
# Launch with splash screen
./build/linapple

# Boot directly into the included Apple II Master disk
./build/linapple --autoboot --d1 res/Master.dsk

# Boot a hard disk image (2MG / HDV)
./build/linapple --autoboot --hd1 /path/to/disk.2mg
```

## 5. Installation (XDG Compliant)

To install LinApple so that the `linapple` command and desktop assets are
available system-wide:

```bash
cmake --install build
```

### Installation Modes

* **User-Local Install (Default / Non-Root):**
  Running `cmake --install build` as a standard user installs without `sudo`
  directly to:
  * Binaries: `~/.local/bin/`
  * Assets & ROMs: `~/.local/share/linapple/`
  * Configuration: `~/.config/linapple/linapple.conf`

* **System-Wide Install (Root):**
  Running `sudo cmake --install build` installs system-wide to:
  * Binaries: `/usr/local/bin/`
  * Assets & ROMs: `/usr/local/share/linapple/`
  * Configuration: `/usr/local/etc/linapple/`

* **Custom Prefix (e.g. Package Maintainers):**

  ```bash
  cmake -B build -DCMAKE_INSTALL_PREFIX=/usr
  sudo cmake --install build
  ```

## 6. Running Tests

To build and execute the automated test suite (including CPU verification and
peripheral tests):

```bash
# Configure with testing enabled
cmake -B build -DBUILD_TESTING=ON

# Build and execute all test suites
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```
