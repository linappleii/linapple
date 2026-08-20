# LinApple Hardware Modernization Protocol

This document defines the "Platinum Standard" routine for reviewing, modernizing, and hardening LinApple hardware peripherals and their associated drivers.

## Core Mandates ("The Gold Standard")
- **Language**: Procedural C-like C++11; favor `structs` and plain functions over classes.
- **Naming**: Strict `snake_case` for all functions, variables, and constants. `PascalCase_t` for types.
- **Resource Safety**: 100% RAII-compliant. No raw `new`/`delete` or manual `fclose`. Use `std::unique_ptr` and `FilePtr`.
- **Syntax**: Use Trailing Return Types (`auto -> type`) for all new/modernized functions.
- **Documentation**: "Why, not What." Prune all descriptive labels; keep only non-obvious architectural justifications.

---

## The Workflow

### Step 1: The Header (.h) Review
1. **Clang Hygiene**: Run `clang-format` and `clang-tidy` to ensure stylistic visual parity and technical safety.
2. **ABI Stability**: Ensure the header remains C99-compatible (using standard return types and `extern "C"` where applicable).
3. **Surgical Linting**: Add/Refine the `NOLINTBEGIN/END` block with precise rules and detailed architectural justifications.
4. **Comment Pruning**: Remove all Doxygen, labels, or "What" descriptions. Retain only essential domain-scoped constants.

### Step 2: The Surgical Implementation (.cpp) Walkthrough
Walk through every item in the file sequentially:
1. **Header Hygiene**: Use `clangd` to clean includes (remove unused, add missing).
2. **NOLINT Audit**: Ensure the implementation's lint block is surgically precise for C++11/ABI debt.
3. **Domain Structures**: Harden the `Instance_t` struct with RAII members and correct move lifecycle rules.
4. **Constant Scoping**: Move all constants into nested domain namespaces (e.g., `namespace macbinary`).
5. **Functional Units**: Review every helper and public function one-by-one:
   - Implement **Flattening** (Guard clauses over nested `if`s).
   - Add **Defensive Guards** (Null checks, bounds checks).
   - Ensure **Hardware Accuracy** (Correct return values for strobes/switches).

### Step 3: Final Hygiene & Subsystem Validation
1. **The Final Prune**: List all comments in the file and aggressively remove any "What" descriptions added during dev.
2. **Visual Parity**: Perform a final `clang-format` pass.
3. **Exhaustive Testing**: Run the complete suite of specialized test binaries (e.g., `test-disk-drivers`) to verify 100% functional integrity.

---
*Follow this protocol strictly for every file to ensure the highest standard of engineering quality.*
