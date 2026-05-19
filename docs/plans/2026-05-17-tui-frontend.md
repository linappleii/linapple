# LinApple TUI Frontend Implementation Plan

> **For Gemini:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Create a high-performance, Truecolor TUI frontend for LinApple that supports hybrid rendering, mouse tracking, and tiered audio.

**Architecture:** Peer to SDL3/Headless frontends. Leverages `LinAppleCore` bridge. Uses a modular approach for terminal handling (`TuiTerminal`), video (`TuiVideo`), input (`TuiInput`), and audio (`TuiAudio`).

**Tech Stack:** C++11, `termios.h`, ANSI/VT escape sequences, `linux/joystick.h`, tiered audio (libpulse/libasound).

---

### Task 1: Scaffolding and Build System

**Files:**
- Create: `src/frontends/tui/Main.cpp`
- Modify: `CMakeLists.txt`

**Step 1: Create minimal Main.cpp**
Implement a basic main loop that initializes the core and runs for 60 frames (headless style).

**Step 2: Update CMakeLists.txt**
Add `tui` to the valid frontends list and create the `linapple-tui` executable target.

**Step 3: Build and Verify**
Run: `cmake -B build -DFRONTEND=tui && cmake --build build`
Expected: `linapple-tui` binary created.

---

### Task 2: Terminal Lifecycle Management (`TuiTerminal`)

**Files:**
- Create: `src/frontends/tui/TuiTerminal.h`, `src/frontends/tui/TuiTerminal.cpp`

**Step 1: Implement Raw Mode and Alternate Buffer**
Use `termios` to set raw mode and emit ANSI sequences for alternate buffer and hiding the cursor.

**Step 2: Implement Signal Handling**
Catch `SIGINT` and `SIGWINCH` to ensure clean terminal restoration and resize detection.

**Step 3: Verify**
Run the app; verify the terminal clears and restores correctly upon exit.

---

### Task 3: Adaptive Video Rendering (`TuiVideo`)

**Files:**
- Create: `src/frontends/tui/TuiVideo.h`, `src/frontends/tui/TuiVideo.cpp`

**Step 1: Implement Adaptive Sampling Engine**
Implement the mode-aware rendering logic:
- **Text Modes:** Direct Memory Access (DMA) from `g_pTextBank0/1` for 100% fidelity.
- **Lo-Res:** High-performance Point Sampling.
- **Hi-Res/DHi-Res:** Area Majority Sampling to prevent aliasing and preserve artifact colors.
- **Mixed Mode:** Hybrid split (Graphics top, DMA Text bottom).

**Step 2: Implement Half-Block Rasterizer**
Convert sampled/selected colors into 24-bit ANSI Truecolor sequences using Unicode `▄`. Use a pre-calculated LUT for the Apple II 16-color palette.

**Step 3: Implement Dirty-Cell Tracking**
Maintain a cache of the previous frame's terminal state and only emit escape sequences for changed cells to minimize terminal I/O.

---

### Task 4: Advanced Input Mapping (`TuiInput`)

**Files:**
- Create: `src/frontends/tui/TuiInput.h`, `src/frontends/tui/TuiInput.cpp`

**Step 1: ANSI Escape Sequence Parser**
Parse `CSI` and `SS3` sequences to detect Arrow keys, Function keys, and modifiers.

**Step 2: Mouse and Joystick Integration**
Enable SGR mouse tracking. Map events to Apple II Mouse/Joystick coordinates. Integrate `/dev/input/js*`.

---

### Task 5: Tiered Audio Strategy (`TuiAudio`)

**Files:**
- Create: `src/frontends/tui/TuiAudio.h`, `src/frontends/tui/TuiAudio.cpp`

**Step 1: Implement Audio Detection**
Check for PipeWire (via PulseAudio API), then ALSA.

**Step 2: Fallback to Terminal Bell**
Implement a minimal "beep" via `\a` if no audio hardware is found.

---

### Task 6: UI and Final Integration

**Files:**
- Modify: `src/frontends/tui/Main.cpp`
- Create: `src/frontends/tui/TuiUI.cpp`

**Step 1: Implement Overlay Menus**
Re-implement the disk browser and settings overlays using box-drawing characters.

**Step 2: Status Bar**
Implement the bottom status line showing disk activity and emulator state.

**Step 3: Final Verification**
Run a full session: boot a disk, navigate menus, test audio, and exit via F12.
