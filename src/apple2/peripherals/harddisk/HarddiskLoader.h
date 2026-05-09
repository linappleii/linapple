/*
 * HarddiskLoader.h - Centralised harddisk image loading and format detection
 */

#pragma once

#include "apple2/peripherals/harddisk/HarddiskFormatDriver.h"

#ifdef __cplusplus
extern "C" {
#endif

void HarddiskLoader_Init(void);
void HarddiskLoader_Shutdown(void);

void HarddiskLoader_Register(HarddiskFormatDriver_t* driver);

HarddiskError_e HarddiskLoader_Open(const char* filename, bool* out_os_readonly,
                                    HarddiskFormatDriver_t** out_driver,
                                    void** out_instance);

#ifdef __cplusplus
}
#endif
