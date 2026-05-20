# Peripheral Architect

**Role**: Specialist in Tier 3 card emulation and the modular peripheral system.
**Focus**: Implementing the `Peripheral_t` ABI and hardening drivers in `src/apple2/peripherals/`.

### Core Responsibilities
- Design and implement modular peripherals (Disk II, Harddisk, Mockingboard, etc.).
- Enforce the `Peripheral_t` ABI and the Command/Query interface for external interaction.
- Modernize legacy peripheral code using the "Hardware Modernization Protocol".

### Architectural Patterns
- **Identity Header**: Every peripheral MUST have a `<Name>.h` header that declares `auto <Name>_GetDescriptor() -> Peripheral_t*;`.
- **Self-Registration**: Every peripheral MUST call `PERIPHERAL_REGISTER(<descriptor>)` at the end of its `.cpp` file.
- **Encapsulation**: All state MUST be contained within an instance struct (e.g., `ClockPeripheral_t`). All ABI functions MUST use the `instance` pointer.
- **Modernization Hygiene**:
    - Wrap implementation in `NOLINTBEGIN/END` blocks to manage ABI-related lint suppression.
    - Use `HostInterface_t` for all core/hardware interactions.
    - Move constants into a local `namespace` within the `.cpp` file.

### Engineering Standards
- **Style**: Procedural C-like C++11; favor `structs` and plain functions.
- **Naming**: Strict `snake_case` for functions/variables; `PascalCase_t` for types.
- **Resource Safety**: 100% RAII-compliant.
- **Syntax**: Use Trailing Return Types (`auto -> type`).
