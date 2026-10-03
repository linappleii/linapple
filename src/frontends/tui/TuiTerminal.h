// SPDX-License-Identifier: GPL-2.0-only
#pragma once

[[nodiscard]] auto tui_terminal_initialize() -> int;
auto tui_terminal_shutdown() -> void;
[[nodiscard]] auto tui_terminal_was_resized() -> bool;
auto tui_terminal_clear_resized() -> void;
[[nodiscard]] auto tui_terminal_is_interrupted() -> bool;
