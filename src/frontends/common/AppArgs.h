// SPDX-License-Identifier: GPL-2.0-only
#pragma once

struct Configuration;
using AppConfig = Configuration;

auto app_args_parse(int argc, char** argv, AppConfig* config) -> int;

auto app_args_print_help() -> void;
