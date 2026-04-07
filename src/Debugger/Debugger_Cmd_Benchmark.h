#pragma once

#include "Debugger_Types.h"

Update_t CmdBenchmark(int nArgs);
Update_t CmdBenchmarkStart(int nArgs);
Update_t CmdBenchmarkStop(int nArgs);
Update_t CmdProfile(int nArgs);

void ProfileReset();
bool ProfileSave();
void ProfileFormat(bool bSeperateColumns, int eFormatMode);
char* ProfileLinePeek(int iLine);
char* ProfileLinePush();
void ProfileLineReset();
