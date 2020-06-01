#pragma once

void SysClk_WaitTimer();

bool SysClk_InitTimer();

void SysClk_UninitTimer();

void SysClk_StartTimerUsec(unsigned int dwUsecPeriod);

void SysClk_StopTimer();
