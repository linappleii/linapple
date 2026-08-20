#pragma once

#include <cstdint>

// Motorola MC6821 Peripheral Interface Adapter (PIA)
// Implementation based on official MC6821 datasheet.

using PiaOutputCallback_t = void (*)(void* obj_to, uint8_t data);

typedef struct PiaWriteHandlerTag_t {
  void* obj_to;
  PiaOutputCallback_t func;
} PiaWriteHandler_t;

typedef struct Pia6821Tag_t {
  // Internal Registers
  uint8_t ora;   // Output Register A
  uint8_t orb;   // Output Register B
  uint8_t ddra;  // Data Direction Register A
  uint8_t ddrb;  // Data Direction Register B
  uint8_t cra;   // Control Register A
  uint8_t crb;   // Control Register B

  // External Line States (Inputs)
  uint8_t port_a_in;
  uint8_t port_b_in;
  bool ca1_in;
  bool ca2_in;
  bool cb1_in;
  bool cb2_in;

  // Internal State
  uint8_t oca2;  // Output CA2 state
  uint8_t ocb2;  // Output CB2 state
  uint8_t irq_a_state;
  uint8_t irq_b_state;

  // Callbacks
  PiaWriteHandler_t out_a;
  PiaWriteHandler_t out_b;
  PiaWriteHandler_t out_ca2;
  PiaWriteHandler_t out_cb2;
  PiaWriteHandler_t out_irqa;
  PiaWriteHandler_t out_irqb;
} Pia6821_t;

// Interface
void pia_6821_reset(Pia6821_t* p);
auto pia_6821_read(Pia6821_t* p, uint8_t addr) -> uint8_t;
void pia_6821_write(Pia6821_t* p, uint8_t addr, uint8_t val);

// Signal Injection
void pia_6821_set_port_a(Pia6821_t* p, uint8_t val);
void pia_6821_set_port_b(Pia6821_t* p, uint8_t val);
void pia_6821_set_ca1(Pia6821_t* p, bool level);
void pia_6821_set_ca2(Pia6821_t* p, bool level);
void pia_6821_set_cb1(Pia6821_t* p, bool level);
void pia_6821_set_cb2(Pia6821_t* p, bool level);

// Data Retrieval
auto pia_6821_get_port_a(Pia6821_t* p) -> uint8_t;
auto pia_6821_get_port_b(Pia6821_t* p) -> uint8_t;

// Configuration
void pia_6821_set_listener_a(Pia6821_t* p, void* obj_to,
                             PiaOutputCallback_t func);
void pia_6821_set_listener_b(Pia6821_t* p, void* obj_to,
                             PiaOutputCallback_t func);
void pia_6821_set_listener_ca2(Pia6821_t* p, void* obj_to,
                               PiaOutputCallback_t func);
void pia_6821_set_listener_cb2(Pia6821_t* p, void* obj_to,
                               PiaOutputCallback_t func);
void pia_6821_set_listener_irqa(Pia6821_t* p, void* obj_to,
                                PiaOutputCallback_t func);
void pia_6821_set_listener_irqb(Pia6821_t* p, void* obj_to,
                                PiaOutputCallback_t func);
