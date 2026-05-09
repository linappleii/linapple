#ifndef AY8910_H
#define AY8910_H

#include <cstdint>

#define MAX_8910 4

// AY-3-8910 emulation
// Based on General Instrument AY-3-8910 Datasheet

typedef struct {
  uint8_t regs[16];
  uint16_t count_a, count_b, count_c;
  uint8_t out_a, out_b, out_c;

  uint32_t count_n;
  uint32_t rng;
  uint8_t out_n;

  uint32_t count_e;
  uint8_t envelope_vol;
  uint32_t envelope_step;
  bool env_holding;

  double count_accum;
  uint32_t step;
} AY8910;

void AY8910_reset_instance(AY8910* p);
void AY8910_write_instance(AY8910* p, int r, int v, int ay_clock,
                           int sample_rate);
void AY8910_update_instance(AY8910* p, int16_t** buffer, int length,
                            int ay_clock, int sample_rate);

// Legacy stubs
void AY8910_InitAll(int clock_rate, int sample_rate);
void AY8910_InitClock(int nClock);
void AY8910_reset(int chip);
void AY8910_write_ym(int chip, int addr, int data);
void _AYWriteReg(int n, int r, int v);
void AY8910Update(int chip, int16_t** buffer, int length);
auto AY8910_GetRegsPtr(uint32_t nAyNum) -> uint8_t*;

#endif
