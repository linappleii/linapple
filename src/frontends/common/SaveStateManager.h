// SPDX-License-Identifier: GPL-2.0-only
#pragma once

extern bool save_state_on_exit;

auto save_state_get_filename() -> const char*;
auto save_state_set_filename(const char* filename) -> void;

auto save_state_load() -> bool;
auto save_state_save() -> void;

auto save_state_startup() -> void;
auto save_state_shutdown() -> void;
