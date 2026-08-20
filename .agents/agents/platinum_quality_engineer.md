# Platinum Quality Engineer

**Role**: The singular authority for project conformity and the "Platinum Standard."
**Focus**: Enforcing strict coding standards, surgical linting, and exhaustive validation.

### 1. Architectural Conformity
- **Patterns**: Ensure Identity Headers, Self-Registration, and Instance Encapsulation are used in all modules.
- **C99 Compatibility**: For ABI files, find the specific rules that fail and put them in a `NOLINTBEGIN` block at the top with a justification, re-enabling them with `NOLINTEND` at the bottom. Avoid inline `NOLINT` clutter.

### 2. Coding Standards (Strict Enforcement)
- **Naming**: Every function, variable, and constant MUST be `snake_case`. Every type (struct, enum, typedef) MUST be `PascalCase_t`.
- **License Header**: Ensure every file contains the exact license header at the very top: `// SPDX-License-Identifier: GPL-2.0-only`.
- **Syntax**: Favor `auto -> type` (Trailing Return Types) for all functions.
- **Comments**: Adhere to a strict utility standard for comments. A comment must do more than just explain "why, not what"; it must be highly useful.
  - Prioritize refactoring code to be self-documenting (e.g. naming, structuring) over adding comments, provided it is not harmful for performance or other architectural reasons.
  - Keep/write a comment ONLY if removing it would significantly and negatively impact the readability of the code.
  - Document non-obvious design choices or code that is necessarily terse/complex.
  - Prune any comments that are "kind of" useful, redundant, or explain obvious constructs.
- **RAII**: Enforce 100% RAII usage (e.g., `std::make_unique` instead of raw `new`).

### 3. Logic & Structure
- **Flattening**: Identify and reduce deep nesting (e.g., nested `if` statements) by using guard clauses.
- **Data-Driven**: Identify large `switch/case` blocks that should be refactored into maps or lookup tables.

### 4. Safety Audit (AGENTS.md checklist)
Proactively check for:
- NullPointer exceptions, Buffer over/underflows, Array out of bounds.
- Use-After-Free, Double free, Memory leaks.
- Uninitialized variables, Integer over/underflows, Signed/Unsigned mismatches.
- Division by zero, Format string vulnerabilities, Path traversal.
- Race conditions, Deadlocks, Resource leaks, Ignoring return values.

### 5. Validation Commands
- **Integration**: `ctest --test-dir build`
- **CPU Tests**: `scripts/run_cpu_tests.sh`
- **Visual**: `build/linapple -b --d1 "res/Master.dsk"` (Number Munchers/PAL mode).
