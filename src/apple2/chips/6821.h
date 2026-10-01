// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <type_traits>

// Motorola MC6821 Peripheral Interface Adapter (PIA)
// Implementation based on official MC6821 datasheet.

using PiaOutputCallback_t = void (*)(void* obj_to, uint8_t data);

struct PiaWriteHandler_t {
  void* obj_to = nullptr;
  PiaOutputCallback_t func = nullptr;
};

struct Pia6821_t {
  // Internal Registers
  uint8_t ora = 0;   // Output Register A
  uint8_t orb = 0;   // Output Register B
  uint8_t ddra = 0;  // Data Direction Register A
  uint8_t ddrb = 0;  // Data Direction Register B
  uint8_t cra = 0;   // Control Register A
  uint8_t crb = 0;   // Control Register B

  // External Line States (Inputs)
  uint8_t port_a_in = 0xFF;  // MC6821 Port A internal pull-ups
  uint8_t port_b_in = 0xFF;
  bool ca1_in = false;
  bool ca2_in = false;
  bool cb1_in = false;
  bool cb2_in = false;

  // Internal State
  uint8_t oca2 = 0;  // Output CA2 state
  uint8_t ocb2 = 0;  // Output CB2 state
  uint8_t irq_a_state = 0;
  uint8_t irq_b_state = 0;

  // Callbacks
  PiaWriteHandler_t out_a{};
  PiaWriteHandler_t out_b{};
  PiaWriteHandler_t out_ca2{};
  PiaWriteHandler_t out_cb2{};
  PiaWriteHandler_t out_irqa{};
  PiaWriteHandler_t out_irqb{};
};

static_assert(std::is_standard_layout<Pia6821_t>::value,
              "Pia6821_t must satisfy standard layout requirements");

// Interface
auto pia_6821_reset(Pia6821_t* p) noexcept -> void;
[[nodiscard]] auto pia_6821_read(Pia6821_t* p, uint8_t addr) noexcept
    -> uint8_t;
auto pia_6821_write(Pia6821_t* p, uint8_t addr, uint8_t val) noexcept -> void;

// Signal Injection
auto pia_6821_set_port_a(Pia6821_t* p, uint8_t val) noexcept -> void;
auto pia_6821_set_port_b(Pia6821_t* p, uint8_t val) noexcept -> void;
auto pia_6821_set_ca1(Pia6821_t* p, bool level) noexcept -> void;
auto pia_6821_set_ca2(Pia6821_t* p, bool level) noexcept -> void;
auto pia_6821_set_cb1(Pia6821_t* p, bool level) noexcept -> void;
auto pia_6821_set_cb2(Pia6821_t* p, bool level) noexcept -> void;

// Data Retrieval
[[nodiscard]] auto pia_6821_get_port_a(const Pia6821_t* p) noexcept -> uint8_t;
[[nodiscard]] auto pia_6821_get_port_b(const Pia6821_t* p) noexcept -> uint8_t;

// Configuration
auto pia_6821_set_listener_a(Pia6821_t* p, void* obj_to,
                             PiaOutputCallback_t func) noexcept -> void;
auto pia_6821_set_listener_b(Pia6821_t* p, void* obj_to,
                             PiaOutputCallback_t func) noexcept -> void;
auto pia_6821_set_listener_ca2(Pia6821_t* p, void* obj_to,
                               PiaOutputCallback_t func) noexcept -> void;
auto pia_6821_set_listener_cb2(Pia6821_t* p, void* obj_to,
                               PiaOutputCallback_t func) noexcept -> void;
auto pia_6821_set_listener_irqa(Pia6821_t* p, void* obj_to,
                                PiaOutputCallback_t func) noexcept -> void;
auto pia_6821_set_listener_irqb(Pia6821_t* p, void* obj_to,
                                PiaOutputCallback_t func) noexcept -> void;
