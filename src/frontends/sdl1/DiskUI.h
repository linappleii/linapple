#pragma once

#include "apple2/peripherals/disk/DiskFormatDriver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Map disk error codes to human-readable strings for UI display.
 *
 * @param error_code The DiskError_e code returned by commands or queries.
 * @return A static string describing the error.
 */
const char* DiskUI_GetErrorMessage(int error_code);

#ifdef __cplusplus
}
#endif
