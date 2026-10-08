// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <type_traits>

// Motorola MC6821 Peripheral Interface Adapter. The "Datasheet Page N"
// citations in 6821.cpp refer to
// https://colorcomputerarchive.com/repo/Documents/Datasheets/MC6821%20NMOS%20Peripheral%20Interface%20Adapter%20(Motorola).pdf

using PiaOutputCallback = void (*)(void* obj_to, uint8_t data);

struct PiaWriteHandler {
  void* obj_to = nullptr;
  PiaOutputCallback func = nullptr;
};

struct Pia6821 {
  uint8_t ora = 0;
  uint8_t orb = 0;
  uint8_t ddra = 0;
  uint8_t ddrb = 0;
  uint8_t cra = 0;
  uint8_t crb = 0;

  // Port A's inputs have internal pull-ups (MC6821 data sheet, "Section A
  // Peripheral Data").
  uint8_t port_a_in = 0xFF;
  uint8_t port_b_in = 0xFF;
  bool ca1_in = false;
  bool ca2_in = false;
  bool cb1_in = false;
  bool cb2_in = false;

  uint8_t oca2 = 0;
  uint8_t ocb2 = 0;
  uint8_t irq_a_state = 0;
  uint8_t irq_b_state = 0;

  PiaWriteHandler out_a{};
  PiaWriteHandler out_b{};
  PiaWriteHandler out_ca2{};
  PiaWriteHandler out_cb2{};
  PiaWriteHandler out_irqa{};
  PiaWriteHandler out_irqb{};
};

static_assert(std::is_standard_layout<Pia6821>::value,
              "Pia6821 must satisfy standard layout requirements");

auto pia_6821_reset(Pia6821* p) noexcept -> void;
auto pia_6821_read(Pia6821* p, uint8_t addr) noexcept -> uint8_t;
auto pia_6821_write(Pia6821* p, uint8_t addr, uint8_t val) noexcept -> void;

auto pia_6821_set_port_a(Pia6821* p, uint8_t val) noexcept -> void;
auto pia_6821_set_port_b(Pia6821* p, uint8_t val) noexcept -> void;
auto pia_6821_set_ca1(Pia6821* p, bool level) noexcept -> void;
auto pia_6821_set_ca2(Pia6821* p, bool level) noexcept -> void;
auto pia_6821_set_cb1(Pia6821* p, bool level) noexcept -> void;
auto pia_6821_set_cb2(Pia6821* p, bool level) noexcept -> void;

auto pia_6821_get_port_a(const Pia6821* p) noexcept -> uint8_t;
auto pia_6821_get_port_b(const Pia6821* p) noexcept -> uint8_t;

auto pia_6821_set_listener_a(Pia6821* p, void* obj_to,
                             PiaOutputCallback func) noexcept -> void;
auto pia_6821_set_listener_b(Pia6821* p, void* obj_to,
                             PiaOutputCallback func) noexcept -> void;
auto pia_6821_set_listener_ca2(Pia6821* p, void* obj_to,
                               PiaOutputCallback func) noexcept -> void;
auto pia_6821_set_listener_cb2(Pia6821* p, void* obj_to,
                               PiaOutputCallback func) noexcept -> void;
auto pia_6821_set_listener_irqa(Pia6821* p, void* obj_to,
                                PiaOutputCallback func) noexcept -> void;
auto pia_6821_set_listener_irqb(Pia6821* p, void* obj_to,
                                PiaOutputCallback func) noexcept -> void;
