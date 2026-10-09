# LinApple: Apple II Emulator

LinApple is an emulator for Apple ][[, Apple ]][+, Apple //e, and Enhanced
Apple //e computers, originally ported from AppleWin to Linux and now
supporting multiple frontends.

## Project Overview

- **Main Technologies:** C++11 (with a preference for procedural C-like
  patterns), CMake, SDL3, SDL3_image, libcurl, libzip, zlib.
- **Architecture:** Decoupled tiered architecture:
  - **Tier 1: User (I/O)** ⇆ **Tier 2: Frontend (SDL3/Headless)** ⇆
    **Tier 3: Logic (Core Bridge)** ⇆ **Tier 4: Hardware
    (Registers/Emulation)**.
- **Key Directories:**
  - `src/core/`: Core emulator logic and the "Core Bridge"
    (`LinAppleCore.h`).
  - `src/apple2/`: Hardware-level emulation (6502 CPU, Disk, Video, etc.).
  - `src/apple2/chips/`: Silicon shared by cards (6522, AY8910, 6821).
  - `src/apple2/media/`: Format, container and codec libraries shared by
    cards, built only when an enabled peripheral lists them.
  - `src/Debugger/`: Integrated assembly-level debugger.
  - `src/frontends/`: Host-specific frontend implementations.
    `src/frontends/sdl3/`, `src/frontends/sdl2/`, and `src/frontends/sdl1/`
    are three co-equal, fully supported SDL frontends. The choice between
    them is dictated by the host — older operating systems and embedded
    devices may only be able to provide an older SDL. All three are expected
    to be functional and are maintained to the same standard. Shared logic
    lives in `src/frontends/common/`; only genuine per-SDL-version
    divergence (such as rendering pipeline differences in `Frame.cpp`)
    belongs in the individual frontend directories.
  - `res/`: Emulator assets (ROMs, Master disk, fonts, icons).
  - `tests/`: Integration and unit tests using `doctest`.

## Building and Running

### Prerequisites

Requires `cmake`, `SDL3`, `SDL3_image`, `libcurl`, `libzip`, `zlib`, and
`ImageMagick` (for asset conversion). `libzip` and `zlib` are needed only
when the Disk II or Harddisk peripheral is enabled.

### Build Commands

```bash
# Configure for SDL3 frontend, skip tests for faster builds
cmake -B build -DBUILD_TESTING=OFF

# Build
cmake --build build -j$(nproc)

# Install (XDG compliant)
cmake --install build
```

### Build Presets

LinApple provides CMake presets (`CMakePresets.json`) for standard workflows:

- **Local (Default):** `cmake --preset local` (Release, native host CPU `-march=native`, LTO).
- **Debug:** `cmake --preset dev_debug` (Debug, debugger enabled, tracing, no optimization).
- **CI:** `cmake --preset ci` (Release, portable CPU, LTO, strict warnings, tests enabled).
- **Distributable:** `cmake --preset dist` (Release, portable CPU, LTO, tests disabled).

Compile and test using presets:

```bash
cmake --build --preset local
ctest --preset local
```

More on building in <!-- Imported from: INSTALL.md -->

### Running

```bash
# Run from build directory
./build/linapple

# Run with autoboot and a specific disk
./build/linapple --autoboot --d1 res/Master.dsk
```

## Testing

- All test plans must be approved by the test_architect sub-agent.
- **Prerequisite:** CMake must be configured with testing enabled
  (`cmake -B build` or explicitly `-DBUILD_TESTING=ON`).
- **Iterative Testing & Verification:** During normal development, ALWAYS
  favor targeted testing and building specific targets (e.g.
  `cmake --build build --target test-integration`,
  `ctest --test-dir build -R <pattern>`, or running `clang-tidy` on
  individual modified files).
- **Full End-to-End Build Rule:** Never run a full, global project rebuild
  with `CMAKE_CXX_CLANG_TIDY` enabled across all targets willy-nilly. Full
  end-to-end static analysis builds across all 53+ test targets take hours
  and must ONLY be run when explicitly requested by the user.
- **Integration Tests:** `ctest --test-dir build` or run individual test
  binaries like `build/test-integration`.
- **CPU Tests:** `scripts/run_cpu_tests.sh` (requires `EMULATOR` env var
  pointing to the binary).
- **CI/CD Workflows:** Use `act` to run GitHub Actions locally. Run `act -l`
  to list jobs, and `act -j <job_id>` (e.g., `act -j headless-build`) to run
  a specific job using Docker.
- **Visual Verification:**
  - Test "Number Munchers": `build/linapple -b --d1 "res/Master.dsk"` (or
    relevant .dsk) and verify startup.
  - Test PAL mode: `build/linapple -b --d1 res/Master.dsk --pal`.

## Development Conventions

- **Coding Style:** Favor a **procedural C-like coding style** for all new
  development. Use `structs` and plain functions instead of `classes` and
  methods where possible to improve simplicity and portability.
- **Naming Conventions:** All identifier naming conventions are authoritatively
  defined and enforced by `.clang-tidy`. In summary:
  - `snake_case` for all functions, variables, and constants (including
    `constexpr` values, bitmasks, hardware registers, and vectors).
  - `PascalCase` for types, structs, classes, and enums.
  - Hungarian notation (`k_`, `_t`, `g_`, `s_`, `e_`) is eliminated across the
    entire codebase without exception.
  - Legacy internal functions in `src/Debugger/` originally ported from AppleWin
    retain existing `PascalCase` names until individually modernized (exempted
    locally in `src/Debugger/.clang-tidy`).
- **Function Syntax:** Use trailing return types (`auto func() -> type`) for
  all new and modernized functions.
- **Resource Safety & RAII:** Ensure 100% RAII compliance. Avoid raw
  `new`/`delete` and manual file handles; use `std::unique_ptr` and
  `FilePtr`.
- **Code Structure:** Prefer guard clauses (flattening) over deeply nested
  conditionals. Maintain defensive null and bounds checks.
- **Pre-processor:** Avoid pre-processor meta-programming except where it is
  strictly necessary. Prefer type-safe C++ alternatives like `constexpr` and
  `enums` for constants.
- **Header Guards:** Use `#pragma once` for all new header files instead of
  traditional `#ifndef` guards.
- **Static Analysis & Tooling:** Project-wide `.clang-tidy` and `.clang-format`
  configurations are the authoritative single source of truth for code style,
  syntax enforcement, and formatting. Code must adhere to them cleanly.
  Avoid `NOLINT` markers except where strictly necessary to suppress false
  positives or unavoidable architectural constraints; any `NOLINT` must be
  accompanied by a comment explaining the justification.
- Files that act as a C99/C++11 ABI (`Peripheral_Types.h`, command headers)
  use `NOLINTBEGIN`/`NOLINTEND` blocks to maintain C-compatible symbols
  and linkage.
- **Architecture:** Strictly adhere to the tiered decoupling. Host-specific
  logic (SDL, file I/O) must stay in the Frontend layer; hardware logic must
  stay in `src/apple2/`. The one exception is I/O on image paths: the disk
  `formats/` drivers and the `src/apple2/media/` libraries open the files
  they parse.
- **Peripheral dependency rule:**
  - A peripheral under `src/apple2/peripherals/<card>/` may include the
    peripheral ABI headers (`Peripheral_Types.h` among them),
    `src/apple2/chips/`, `src/apple2/media/` and these `core/` headers:
    `Util_Path.h`, `Util_Endian.h`, `Util_Text.h`, `Util_Crc32.h`; card
    code may add `Log.h` and `Registry.h`. It never includes another
    `peripherals/<x>/`.
  - `media/` and `chips/` never include `peripherals/` (other than
    `Peripheral_Types.h`), `core/LinAppleCore.h` or `frontends/`.
  - Code shared by cards becomes a `media/` library when it parses untrusted
    input; otherwise only when it has a specification of its own and two
    consumers. Anything under a screenful (60 lines) stays in the card.
  - A library is C99-includable, uses its own error enum and symbol prefix,
    holds no policy constants or save state, is reentrant, and states its
    temp-file prefix and cleanup contract in its header. A parser of
    untrusted input has a fuzzer.
  - CMake: declare a library with `add_peripheral_library` and name it in
    the `add_peripheral` call of every card that uses it;
    `finalize_peripheral_libraries` builds only the libraries an enabled
    card named and looks up their externals there. `add_peripheral` refuses
    a source outside the card's own directory, `chips/` or `media/`.
  - Library and chip objects are compiled with hidden visibility, so the
    executable and the plugins never export them.
  - A card's byte output (a printer's, a serial line's) goes through the
    host's byte sink members of `HostInterface`: `SinkOpen`, `SinkWrite`,
    `SinkReady` and `SinkClose`. A serial line also receives: it pulls
    received bytes through `SinkRead`, reports its format through
    `SinkSetLine` and reads its modem inputs through `SinkGetLines`. The
    frontend installs the one `ByteSink` through `linapple_set_byte_sink`;
    the file or device, its path, its mode (append or overwrite) and its
    flushing belong to the frontend, never to the card. The slot comes from
    the token `SinkOpen` returned, never from the card's own bookkeeping.
  - A line two devices drive is the motherboard's, never a card's. The //e
    wires Open and Solid Apple in parallel with the game connector's PB0 and
    PB1, and the shift-key mod puts shift on PB2 (IIe Technical Reference
    p. 41), and the resting level depends on which devices are plugged in
    (the keyboard's 470 ohm pull-downs, a controller plug's 560 ohm, the
    revision C board's 12 k pull-ups), so `src/apple2/SwitchInputs.{h,cpp}`
    holds both sides of each wired-OR, the plug's mask and the jumper, and
    `Memory.cpp`'s `io_read_c06x` answers `$C061-$C063` and their mirrors
    from it. No card registers those addresses. Each device closes its own
    side through the bridge: the keyboard's through `linapple_set_modifiers`,
    the connector's through `linapple_set_game_switch`,
    `linapple_set_game_pulldowns` and `linapple_set_shift_key_mod`. No two
    cards register one address: the direct-I/O bridge serves the first
    registrant, and registration order is not a contract.
  - Peripheral command headers (e.g., `DiskCommands.h`, `KeyboardCommands.h`,
    `HarddiskCommands.h`, `MockingboardCommands.h`, `MouseCommands.h`) are
    included directly by consumers that send or handle those commands, rather
    than being re-exported through `Peripheral_Types.h`.
  - The two greps below print nothing on a conforming tree; any line they
    print is a violation:

    ```bash
    for d in src/apple2/peripherals/*/; do n=$(basename "$d")
      grep -rnE '#include\s*"apple2/peripherals/[a-z_]+/' "$d" \
        | grep -vE "\"apple2/peripherals/$n/"; done
    grep -rnE '#include\s*"(apple2/peripherals/[a-z_]+/|core/LinAppleCore|frontends/)' \
      src/apple2/chips src/apple2/media
    ```

- **Designated initialisers** in the driver and peripheral descriptors are a
  gnu++11 extension the project relies on (`CMAKE_CXX_EXTENSIONS` stays at
  its default); set descriptor members by name, never by position.
- **Commenting:** Focus on "why" rather than "what". Avoid comments that
  explain obvious code, reference old versions, or describe changes (that's
  for commit messages).
- **Commit Guidelines:**
  - Do not use prefixes like `feat:`, `bug:`, or `fix:`.
  - Do not mention "phases" or "milestones" in commit messages.
  - Keep titles under 70 characters and focus on a high-level overview.
- **Safety:** Do not commit `PLAN.md` to git.
- Things to check before committing, but only for the changes that you made:
  - Is `.editorconfig`'s directives being enforced?
  - Are new `clang-tidy` violations being introduced from your changes?
  - Are new `clang-format` violations being introduced from your changes?
  - Have you cleaned up the comments based on the "Commenting" section
    above?
  - Does the project build without errors or warnings?
  - Are all tests passing?
  - Are you sure you are not introducing any of the following potential
    security & stability issues?
    - NullPointer exceptions
    - Buffer over/underflows
    - Array out of bounds
    - Use-After-Free (Dangling pointers)
    - Double free
    - Memory leaks
    - Uninitialized variables
    - Integer over/underflows
    - Signed/Unsigned mismatches
    - Divison by zero
    - Format string vulns
    - Path traversal
    - Race conditions
    - Deadlocks
    - Ignoring return values
    - Resource leaks
    - Anything else you think should be checked

## Disk Support Roadmap

Eventually add write support and missing formats:

| Extension | Format Name | Read Support | Write Support |
| :--- | :--- | :---: | :---: |
| **.dsk**, **.do** | DOS Order | Yes | Yes |
| **.po** | ProDOS Order | Yes | Yes |
| **.nib** | Nibblized (6656 bytes/track) | Yes | Yes |
| **.nb2** | Nibblized (6384 bytes/track) | Yes | Yes |
| **.woz** | WOZ v1 | Yes | **No** |
| **.woz** | WOZ v2 | Yes | **No** (Plan to add write/fractional) |
| **.iie** | SimSystem //e | Yes | **No** (Stubbed) |
| **.apl** | Raw Program Image | Yes (Boot only) | No |
| **.prg** | ProDOS Program Image | Yes (Boot only) | No |
| **.2mg** | 2MG (wrapped images) | Partial | **No** (Plan native support) |
