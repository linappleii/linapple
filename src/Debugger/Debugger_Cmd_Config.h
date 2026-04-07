#pragma once

#include "Debugger_Types.h"

Update_t CmdConfigColorMono(int nArgs);
Update_t CmdConfigHColor(int nArgs);
Update_t CmdConfigLoad(int nArgs);
Update_t CmdConfigSave(int nArgs);
Update_t CmdConfigDisasm(int nArgs);
Update_t CmdConfigFontLoad(int nArgs);
Update_t CmdConfigFontSave(int nArgs);
Update_t CmdConfigFontMode(int nArgs);
Update_t CmdConfigFont(int nArgs);
Update_t CmdConfigSetFont(int nArgs);
Update_t CmdConfigGetFont(int nArgs);
Update_t CmdConfigSetDebugDir(int nArgs);

bool ConfigSave_BufferToDisk(const char* pFileName, ConfigSave_t eConfigSave);
void ConfigSave_PrepareHeader(const Parameters_e eCategory, const Commands_e eCommandClear);
void _UpdateWindowFontHeights(int nFontHeight);
