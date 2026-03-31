#ifndef PIA6821_H
#define PIA6821_H

#include <stdint.h>

// Motorola MC6821 Peripheral Interface Adapter (PIA)
// Implementation based on official MC6821 datasheet.

typedef void (*PiaOutputCallback)(void* objTo, uint8_t data);

typedef struct {
  void* objTo;
  PiaOutputCallback func;
} PiaWriteHandler;

typedef struct {
  // Internal Registers
  uint8_t ora;  // Output Register A
  uint8_t orb;  // Output Register B
  uint8_t ddra; // Data Direction Register A
  uint8_t ddrb; // Data Direction Register B
  uint8_t cra;  // Control Register A
  uint8_t crb;  // Control Register B

  // External Line States (Inputs)
  uint8_t port_a_in;
  uint8_t port_b_in;
  bool ca1_in;
  bool ca2_in;
  bool cb1_in;
  bool cb2_in;

  // Internal State
  uint8_t oca2; // Output CA2 state
  uint8_t ocb2; // Output CB2 state
  uint8_t irq_a_state;
  uint8_t irq_b_state;

  // Callbacks
  PiaWriteHandler out_a;
  PiaWriteHandler out_b;
  PiaWriteHandler out_ca2;
  PiaWriteHandler out_cb2;
  PiaWriteHandler out_irqa;
  PiaWriteHandler out_irqb;
} Pia6821;

// Interface
void Pia6821_Reset(Pia6821* p);
uint8_t Pia6821_Read(Pia6821* p, uint8_t addr);
void Pia6821_Write(Pia6821* p, uint8_t addr, uint8_t val);

// Signal Injection
void Pia6821_SetPortA(Pia6821* p, uint8_t val);
void Pia6821_SetPortB(Pia6821* p, uint8_t val);
void Pia6821_SetCA1(Pia6821* p, bool level);
void Pia6821_SetCA2(Pia6821* p, bool level);
void Pia6821_SetCB1(Pia6821* p, bool level);
void Pia6821_SetCB2(Pia6821* p, bool level);

// Data Retrieval
uint8_t Pia6821_GetPortA(Pia6821* p);
uint8_t Pia6821_GetPortB(Pia6821* p);

// Configuration
void Pia6821_SetListenerA(Pia6821* p, void* objTo, PiaOutputCallback func);
void Pia6821_SetListenerB(Pia6821* p, void* objTo, PiaOutputCallback func);
void Pia6821_SetListenerCA2(Pia6821* p, void* objTo, PiaOutputCallback func);
void Pia6821_SetListenerCB2(Pia6821* p, void* objTo, PiaOutputCallback func);
void Pia6821_SetListenerIRQA(Pia6821* p, void* objTo, PiaOutputCallback func);
void Pia6821_SetListenerIRQB(Pia6821* p, void* objTo, PiaOutputCallback func);

#endif // PIA6821_H
