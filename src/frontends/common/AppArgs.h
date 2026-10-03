// SPDX-License-Identifier: GPL-2.0-only
#pragma once

struct Configuration_t;
using AppConfig_t = Configuration_t;

auto app_args_parse(int argc, char** argv, AppConfig_t* config) -> int;

auto app_args_print_help() -> void;
