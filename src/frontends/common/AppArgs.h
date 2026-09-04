// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "frontends/common/AppConfig.h"

/**
 * Shared CLI argument parser.
 *
 * @param argc Number of arguments
 * @param argv Argument vector
 * @param outConfig Configuration struct to populate
 * @return 0 on success, non-zero on error
 */
auto app_args_parse(int argc, char** argv, AppConfig_t* outConfig) -> int;

/**
 * Print the unified help message for all LinApple frontends.
 */
void app_args_print_help();
