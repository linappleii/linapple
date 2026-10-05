// SPDX-License-Identifier: GPL-2.0-only
#pragma once

auto tui_input_initialize() -> void;
auto tui_input_poll() -> void;
// A font zoom changes the character cell's pixel size.
auto tui_input_on_resize() -> void;
auto tui_input_shutdown() -> void;
