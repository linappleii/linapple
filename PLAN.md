# Staged Plan for Win32 Removal and Unix Optimization

This plan outlines the steps to refactor the `linapple` codebase to remove Win32 dependencies, optimize for Unix systems, and transition towards pure C code.

Whenever implementing something you expect to work you should make sure to test it. You will do this by first compiling the program with `make clean && make  -j$(nproc)`. If that works you should then try to run Number Munchers by running `build/bin/linapple -b --d1 "../Number Munchers v1.2 (1986)(MECC)(US).dsk" > nm_output.log 2>&1 & sleep 5 && grim -o eDP-1 monitor_0.png && cat nm_output.log && kill $!`. If that shows Number Munchers successfully started up, then try `build/bin/linapple -b --d1 ../MAD3.dsk --pal > nm_output.log 2>&1 & sleep 10 && grim -o eDP-1 monitor_0.png && cat nm_output.log && kill $!`

Anytime you make a commit you should review all of the comments you added. First of all, comments should always be towards the end goal of readable and reasonable code. We don't want comments that state what is obvious by just looking at the code, or comments that explain a change, or reference how code used to be. We just want comments that explain complex code, or non-obvious choices, or things along that tract.

For all new code and architectural changes, favor a **procedural C-like coding style** over modern C++ patterns. Prefer using `structs` and plain functions instead of `classes` and methods. This is a general direction for new development to improve core simplicity and portability, and is not a directive to refactor stable existing C++ code unless required for decoupling.

## Phase 1: Standardize Types and Macros [COMPLETED]
**Goal:** Replace Win32-style type aliases and macros with standard C equivalents.

1.  **Replace Win32 Types:** [DONE]
    - Replace `BYTE` with `uint8_t`.
    - Replace `WORD` with `uint16_t`.
    - Replace `DWORD` with `uint32_t`.
    - Replace `LONG` with `int32_t` and `ULONG` with `uint32_t`.
    - Replace `BOOL` with `bool` (from `<stdbool.h>`).
    - Replace Win32 pointer aliases (`LPVOID`, `LPCSTR`, etc.) with standard pointers (`void*`, `const char*`, etc.).
2.  **Update Macros:** [DONE]
    - Remove `TEXT()` macro usage.
    - Replace `MAKEWORD`, `LOBYTE`, etc., with standard bitwise operations or clean inline functions.
    - Replace `ZeroMemory`, `CopyMemory`, etc., with `memset` and `memcpy`.
3.  **Hardware Decoupling Standards:**
    - **Speaker:** Core tracks cycle-exact "toggles" (high/low). Frontend (Applewin.cpp) owns the `SDL_AudioStream` and converts bitstream to S16 samples.
    - **Joystick:** Frontend handles all SDL joystick/gamepad events and normalizes to 0-255. Core implements the 555-timer simulation.
    - **Video:** Core maintains cycle-exact CRT beam timing and renders to a raw RGB `uint32_t` backbuffer. Frontend handles the presentation layer.
    - **Mouse:** Core implements the Apple II Mouse Card firmware protocol. Frontend feeds raw deltas into core registers.
    - **Mockingboard:** Core emulates the 6522 and AY-3-8910 chips. Frontend manages final audio mixing.
    - **Sound Core:** Core handles multi-channel mixing of all sound sources (Speaker, Mockingboard). Frontend manages the final SDL audio callback and stream buffer.
    - **Serial Port (SSC):** Core emulates the 6551 ACIA logic and registers. Frontend manages the actual host-side TTY, TCP socket, or null-modem connection.
    - **Parallel Printer:** Core emulates the Centronics interface logic. Frontend manages the host-side output (to file, stdout, or system printer).

## Phase 2: Refactor Filesystem and Memory Abstractions [COMPLETED]
**Goal:** Remove `wwrapper.cpp` and use native POSIX/C library functions.

1.  **Filesystem IO:** [DONE]
    - Replace `ReadFile`/`WriteFile` with `fread`/`fwrite`.
    - Replace `SetFilePointer` with `fseek`.
    - Replace `GetFileSize` with a standard POSIX `stat` call or `fseek`/`ftell`.
    - Replace `CloseHandle` with `fclose`.
2.  **Memory Management:** [DONE]
    - Replace `VirtualAlloc`/`VirtualFree` with `malloc`/`free`.
3.  **Time/Ticks:** [DONE]
    - Replace `GetTickCount` with `SDL_GetTicks`.

## Phase 3: Modernize Configuration and Registry Shim [COMPLETED]
**Goal:** Clean up `Registry.cpp` and move to a more idiomatic Unix configuration system.

1.  **Clean API:** [DONE] Rename `RegLoadValue`, `RegSaveValue`, etc., to `ConfigLoadInt`, `ConfigSaveInt`.
2.  **Path Handling:** [DONE] Ensure configuration and data files follow XDG Base Directory Specification.
3.  **Support Spaces in Paths:** [DONE] [Issue #141] Refactor the configuration parser to handle directory paths containing spaces and optional quotes.

## Phase 4: Clean Game Loop & Modular Init/Shutdown [COMPLETED]
**Goal:** Restructure the application entry point and main loop for clarity, state-driven execution, and modularity.

1.  **SysInit/SysShutdown Stubs:** [DONE]
    - Create `SysInit()` and `SysShutdown()` in `src/Applewin.cpp`.
    - Move global resource initialization (Log, Assets, SDL, CURL) to `SysInit()`.
    - Move global resource cleanup to `SysShutdown()`.
2.  **Separate Session Life-cycle:** [DONE]
    - Create `SessionInit()` and `SessionShutdown()` to encapsulate everything inside the `restart` loop.
    - Ensure `SessionInit` handles configuration loading and hardware resets.
3.  **Encapsulate State Management:** [DONE]
    - Introduce a `SystemState` struct to track `g_nAppMode`, `restart`, and `fullscreen`.
    - Replace the `do...while(restart)` loop with a controlled state transition.
4.  **Refactor Main Loop:** [DONE]
    - Deconstruct `EnterMessageLoop()` into `Sys_Input()`, `Sys_Think()`, and `Sys_Draw()`.
    - Implement a fixed-timestep or throttled loop in `main()`.
5.  **De-modalize Sub-systems:** [DONE]
    - Refactor `DiskChoose` and `DiskFTP` to use the main loop's Input/Draw calls instead of blocking `SDL_PollEvent` loops.

## Phase 5: CPU Behavioral Verification [COMPLETED]
**Goal:** Exhaustively verify the correctness of the 6502/65C02 implementation using industry-standard test suites.

1.  **Integrate Functional Tests:** [DONE]
    - Source and integrate `6502_functional_test.bin` from the `Klaus2m5/6502_65C02_functional_tests` project.
2.  **Implement Test Runner:** [DONE]
    - Created a permanent "headless" diagnostic mode (`--test-cpu`) to load functional tests directly into memory.
    - Implemented a shell script (`scripts/run_cpu_tests.sh`) to automate fetching and running tests.
3.  **Validate and Fix:** [DONE]
    - Verified the NMOS and CMOS cores pass the functional suite. Added behavioral fixes for decimal mode.

## Phase 6: Improve CPU Timing Accuracy [COMPLETED]
**Goal:** Resolve cycle-counting discrepancies to ensure compatibility with timing-sensitive software. [Issue #149]

1.  **Fix Page-Crossing Logic:** [DONE]
    - Removed the redundant `bSlowerOnPagecross` flag and associated cruft.
    - Updated `CHECK_PAGE_CHANGE` to handle penalties automatically via addressing modes.
    - Implemented `_NP` (No Penalty) addressing modes for instructions like `STA` that don't have page-cross penalties.
2.  **Validation:** [DONE]
    - Verified timing accuracy using the `MAD3` demo.



## Phase 7: Transition from C++ to C [ON HOLD]
**Goal:** Refactor C++ features to pure C to meet the "C code" objective.

1.  **Convert Classes to Structs:** Transform classes like `CMouseInterface`, etc., into structs with associated functions. (Note: `C6821` is already converted).
2.  **Replace STL Containers:** Replace `std::string`, `std::vector`, and `std::map` with standard C strings and appropriate C data structures.
3.  **Update Build System:** Adjust `Makefile` to use `gcc` instead of `g++` and remove C++-specific flags.

## Phase 8: Unix-Specific Optimizations [COMPLETED]
**Goal:** Final performance tuning for Unix systems.

1.  **Profiling and Tuning:** [DONE]
    - Optimized `copy_row1to4` using a Palette Look-Up Table (LUT) and 1:1 scale specialization.
    - Refactored `stretch.cpp` code generation macros into C++ function templates.
    - Improved `Timer.cpp` performance by reducing clock polling frequency.
    - Enabled Link Time Optimization (LTO) and `-march=native` in the build system.
    - Refactored the logging system to support runtime verbosity, hiding high-frequency performance logs by default.

## Phase 9: Automated Code Cleanup and Static Analysis [COMPLETED]
**Goal:** Use `clang-tidy`, `clangd`, and build compilation databases to systematically clean up the codebase and enforce modern standards.

1.  **Generate Compilation Database:** [DONE]
    - Used `compiledb` with `make` to generate `compile_commands.json`.
    - This ensures `clangd` and `clang-tidy` have the correct include paths and compiler flags.
2.  **Integrate clang-tidy:** [DONE]
    - Created `.clang-tidy` configuration.
    - Ran `clang-tidy --fix` on the codebase to apply 200+ automated fixes for common issues.
3.  **Establish clangd Workflow:** [DONE]
    - Use `clangd` as the LSP for real-time diagnostics and refactoring support.
4.  **Continuous Analysis:**
    - Static analysis infrastructure integrated into build workflow.

## Phase 10: Unified I/O Map & Hardware Separation [IN PROGRESS]
**Goal:** Decouple the Apple II core logic from SDL3 and implement a clean, unified hardware dispatch table.

1.  **Centralize I/O Dispatch (Memory.cpp):** [DONE]
    *   Implemented a 512-entry `IORead`/`IOWrite` table.
    *   Indices 0x000-0x0FF provide 1:1 byte mapping for $C000-$C0FF.
    *   Indices 0x100-0x10F provide page-based dispatch for $C100-$CFFF.
    *   Refactored `CPU.cpp` `Fetch` and `READ`/`WRITE` macros to call the centralized `IOMap_Dispatch`.
2.  **Decouple Hardware Modules:**
    *   **Keyboard:** [DONE] Moved SDL translation to `Applewin.cpp`. Core is a pure C-style state machine.
    *   **Speaker:** [DONE] Moved SDL_AudioStream management to frontend. Core tracks cycle-exact "toggles" and provides a bitstream-like interface via an event queue.
    *   **Joystick:** [DONE] Moved SDL joystick polling to frontend (`JoystickFrontend.cpp`). Core manages 555-timer logic with raw 0-255 inputs.
    *   **Video:** [DONE] Core renders to raw RGB `uint32_t` backbuffer. Frontend handles SDL presentation using `VideoSoftStretch` and `SDL_UpdateTexture`. Removed SDL dependencies from `Video.cpp` and `stretch.cpp`.
    *   **Mouse:** [DONE] Moved SDL mouse input to frontend (`Frame.cpp`). Core implements firmware protocol using C-style `struct MouseInterface` and receives deltas/positions via a procedural API.
    *   **Mockingboard:** [DONE] Core emulates 6522/AY-3-8910 using procedural C-style code. Removed SDL dependencies and legacy global buffers. Samples are submitted to `SoundCore` for mixing.
    *   **Sound Core:** [DONE] Core handles multi-channel mixing of all sound sources (Speaker, Mockingboard) into internal buffers. Frontend manages the final SDL audio callback.
    *   **Serial Port (SSC):** Core emulates 6551 ACIA; Frontend handles TTY/Socket.
    *   **Parallel Printer:** Core emulates interface; Frontend handles output target.
3.  **Restore/Verify POST:** [DONE] Verified that Number Munchers and MAD3 boot correctly through the unified map.

## Phase 11: Clean-Room Hardware Integration Tests
**Goal:** Implement a suite of clean-room integration tests in C++ that verify hardware-level behavior (MMU, Video timing, Softswitches) without using proprietary Apple binaries.

1.  **Requirement Specification:**
    *   Document hardware behaviors derived from technical manuals (e.g., Apple IIe Technical Reference).
    *   Focus on "Side-Effect" behaviors (Softswitches, MMU banking, Floating Bus).
2.  **Establish C++ Test Harness:**
    *   Create a minimal test runner in `tests/` that links against internal objects (`CPU.o`, `Memory.o`).
    *   Implement a "Synthetic Assembly" helper to load and run small machine-code payloads.
3.  **Implement Verification Suites:**
    *   **MMU Suite:** Verify Language Card banking, Aux Memory shadowing, and ROM masking.
    *   **Video Suite:** Verify cycle-exact Vertical Blanking (VBL) and mode-switching states.
    *   **I/O Suite:** Verify softswitch state transitions and joystick/timer logic.
