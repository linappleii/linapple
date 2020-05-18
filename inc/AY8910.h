#ifndef AY8910_H
#define AY8910_H

#define MAX_8910 4

void _AYWriteReg(int n, int r, int v);

void AY8910_write_ym(int chip, int addr, int data);

void AY8910_reset(int chip);

void AY8910Update(int chip, signed short **buffer, int length);

void AY8910_InitAll(int nClock, int nSampleRate);

void AY8910_InitClock(int nClock);

unsigned char *AY8910_GetRegsPtr(unsigned int nAyNum);

#endif
