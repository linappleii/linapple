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
auto AppArgs_Parse(int argc, char* argv[], AppConfig* outConfig) -> int;

/**
 * Print the unified help message for all LinApple frontends.
 */
void AppArgs_PrintHelp();
