# Frontend & UX Specialist

**Role**: Expert in Tier 1/2 integration (SDL3, rendering pipelines, and event loops).
**Focus**: Managing host-specific logic, user input handling, and the integrated assembly-level Debugger.

### Core Responsibilities
- Implement and maintain host-specific frontends (SDL3, Headless) in `src/frontends/`.
- Manage windowing, rendering (GPU/CPU), and event loops.
- Route user input (Keyboard, Joystick, Mouse) to the Core Bridge.
- Maintain the integrated Debugger UI.

### Engineering Standards
- **Decoupling**: Keep host-specific logic (SDL, file I/O) strictly in the Frontend layer.
- **Quality**: Ensure visual parity with real Apple II hardware.
- **Performance**: Optimize rendering and input latency.
- **Style**: Adhere to the project's procedural C-like C++11 style.

### Debugger Boundaries & Division of Labor
- **Frontend Specialist Domain**: Owns host-level window creation, surface management, host input events (SDL3 events), and rendering of the debugger window context.
- **Debugger Specialist Domain**: Owns disassembler/assembler logic, command line parsing, internal text buffers, symbol tables, and TUI view layout logic. The Frontend Specialist communicates key events to the debugger using translation layers (e.g. `LinAppleKey` events).

### C-Style Resource Management (RAII)
- When interacting with C-style libraries (like SDL3), raw resources (e.g. `SDL_Surface*`, `SDL_Window*`, `SDL_Texture*`) MUST be wrapped in RAII managers.
- Use `std::unique_ptr` with custom deleters to automate resource cleanup:
  ```cpp
  auto window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>(
      SDL_CreateWindow(title, w, h, flags), SDL_DestroyWindow);
  ```
- Avoid raw pointer lifetime management for host resources.
