// SPDX-License-Identifier: GPL-2.0-only
#pragma once

struct Configuration_t;
using AppConfig_t = Configuration_t;

// Parses command-line arguments and populates config.
// Resets config to defaults; sets intent flags (e.g. INTENT_HELP,
// INTENT_DIAGNOSTIC). Permutes argv elements according to POSIX getopt_long
// rules. Returns 0 on success or help request; returns non-zero on parse error
// or invalid argument.
auto app_args_parse(int argc, char** argv, AppConfig_t* config) -> int;

// Prints unified LinApple command-line options and built-in ROM capabilities to
// stdout.
auto app_args_print_help() -> void;
