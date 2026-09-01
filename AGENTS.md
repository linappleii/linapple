# LinApple: Apple II Emulator

LinApple is an emulator for Apple ][[, Apple ]][+, Apple //e, and Enhanced Apple //e computers, originally ported from AppleWin to Linux and now supporting multiple frontends.

## Project Overview
- **Main Technologies:** C++11 (with a preference for procedural C-like patterns), CMake, SDL3, SDL3_image, libcurl, libzip, zlib.
- **Architecture:** Decoupled tiered architecture:
  - **Tier 1: User (I/O)** ⇆ **Tier 2: Frontend (SDL3/Headless)** ⇆ **Tier 3: Logic (Core Bridge)** ⇆ **Tier 4: Hardware (Registers/Emulation)**.
- **Key Directories:**
  - `src/core/`: Core emulator logic and the "Core Bridge" (`LinAppleCore.h`).
  - `src/apple2/`: Hardware-level emulation (6502 CPU, Disk, Video, etc.).
  - `src/Debugger/`: Integrated assembly-level debugger.
  - `src/frontends/`: Host-specific frontend implementations (SDL3 and Headless).
  - `res/`: Emulator assets (ROMs, Master disk, fonts, icons).
  - `tests/`: Integration and unit tests using `doctest`.

## Building and Running
### Prerequisites
Requires `cmake`, `SDL3`, `SDL3_image`, `libcurl`, `libzip`, `zlib`, and `ImageMagick` (for asset conversion).

### Build Commands
```bash
# Configure for SDL3 frontend, skip tests for faster builds
cmake -B build -DBUILD_TESTING=OFF

# Build
cmake --build build -j$(nproc)

# Install (XDG compliant)
cmake --install build
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
- **Prerequisite:** CMake must be configured with testing enabled (`cmake -B build` or explicitly `-DBUILD_TESTING=ON`).
- **Iterative Testing & Verification:** During normal development, ALWAYS favor targeted testing and building specific targets (e.g. `cmake --build build --target test-integration`, `ctest --test-dir build -R <pattern>`, or running `clang-tidy` on individual modified files).
- **Full End-to-End Build Rule:** Never run a full, global project rebuild with `CMAKE_CXX_CLANG_TIDY` enabled across all targets willy-nilly. Full end-to-end static analysis builds across all 53+ test targets take hours and must ONLY be run when explicitly requested by the user.
- **Integration Tests:** `ctest --test-dir build` or run individual test binaries like `build/test-integration`.
- **CPU Tests:** `scripts/run_cpu_tests.sh` (requires `EMULATOR` env var pointing to the binary).
- **CI/CD Workflows:** Use `act` to run GitHub Actions locally. Run `act -l` to list jobs, and `act -j <job_id>` (e.g., `act -j headless-build`) to run a specific job using Docker.
- **Visual Verification:**
  - Test "Number Munchers": `build/linapple -b --d1 "res/Master.dsk"` (or relevant .dsk) and verify startup.
  - Test PAL mode: `build/linapple -b --d1 res/Master.dsk --pal`.

## Development Conventions
- **Coding Style:** Favor a **procedural C-like coding style** for all new development. Use `structs` and plain functions instead of `classes` and methods where possible to improve simplicity and portability.
- **Naming Conventions:** Use strict `snake_case` for functions, variables, and constants. Use `PascalCase_t` for types and structs. (Exception: hardware register bitmasks, 6502 CPU status flags, and Apple II architecture vector definitions in hardware emulation layers may use `SCREAMING_SNAKE_CASE` to maintain 1:1 fidelity with hardware technical references).
- **Function Syntax:** Use trailing return types (`auto func() -> type`) for all new and modernized functions.
- **Resource Safety & RAII:** Ensure 100% RAII compliance. Avoid raw `new`/`delete` and manual file handles; use `std::unique_ptr` and `FilePtr`.
- **Code Structure:** Prefer guard clauses (flattening) over deeply nested conditionals. Maintain defensive null and bounds checks.
- **Pre-processor:** Avoid pre-processor meta-programming except where it is strictly necessary. Prefer type-safe C++ alternatives like `constexpr` and `enums` for constants.
- **Header Guards:** Use `#pragma once` for all new header files instead of traditional `#ifndef` guards.
- **Static Analysis:** Avoid `NOLINT` markers except where they are absolutely necessary to suppress false positives or unavoidable architectural constraints. When `NOLINT` is used, it must be accompanied by a comment explaining the justification.
- Files that act as a C99/C++11 ABI should have `NOLINTBEGIN` at the top that removes any clang-tidy rules that would break the C99 functionality. The bottom of the file should turn those lints back on with `NOLINTEND`.
- **Architecture:** Strictly adhere to the tiered decoupling. Host-specific logic (SDL, file I/O) must stay in the Frontend layer; hardware logic must stay in `src/apple2/`.
- **Commenting:** Focus on "why" rather than "what". Avoid comments that explain obvious code, reference old versions, or describe changes (that's for commit messages).
- **Commit Guidelines:**
  - Do not use prefixes like `feat:`, `bug:`, or `fix:`.
  - Do not mention "phases" or "milestones" in commit messages.
  - Keep titles under 70 characters and focus on a high-level overview.
- **Safety:** Do not commit `PLAN.md` to git.
- Things to check before committing, but only for the changes that you made:
  - Is `.editorconfig`'s directives being enforced?
  - Are new `clang-tidy` violations being introduced from your changes?
  - Are new `clang-format` violations being introduced from your changes?
  - Have you cleaned up the comments based on the "Commenting" section above?
  - Does the project build without errors or warnings?
  - Are all tests passing?
  - Are you sure you are not introducing any of the following potential security & stability issues?
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
| **.woz** | WOZ v1 | **No** | **No** |
| **.woz** | WOZ v2 | Yes | **No** (Plan to add write/fractional) |
| **.iie** | SimSystem //e | Yes | **No** (Stubbed) |
| **.apl** | Raw Program Image | Yes (Boot only) | No |
| **.prg** | ProDOS Program Image | Yes (Boot only) | No |
| **.2mg** | 2MG (wrapped images) | Partial | **No** (Plan native support) |
