// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <type_traits>

// Motorola MC6821 Peripheral Interface Adapter. The "Datasheet Page N"
// citations in 6821.cpp refer to
// https://colorcomputerarchive.com/repo/Documents/Datasheets/MC6821%20NMOS%20Peripheral%20Interface%20Adapter%20(Motorola).pdf

using PiaOutputCallback_t = void (*)(void* obj_to, uint8_t data);

struct PiaWriteHandler_t {
  void* obj_to = nullptr;
  PiaOutputCallback_t func = nullptr;
};

struct Pia6821_t {
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

  PiaWriteHandler_t out_a{};
  PiaWriteHandler_t out_b{};
  PiaWriteHandler_t out_ca2{};
  PiaWriteHandler_t out_cb2{};
  PiaWriteHandler_t out_irqa{};
  PiaWriteHandler_t out_irqb{};
};

static_assert(std::is_standard_layout<Pia6821_t>::value,
              "Pia6821_t must satisfy standard layout requirements");

auto pia_6821_reset(Pia6821_t* p) noexcept -> void;
auto pia_6821_read(Pia6821_t* p, uint8_t addr) noexcept -> uint8_t;
auto pia_6821_write(Pia6821_t* p, uint8_t addr, uint8_t val) noexcept -> void;

auto pia_6821_set_port_a(Pia6821_t* p, uint8_t val) noexcept -> void;
auto pia_6821_set_port_b(Pia6821_t* p, uint8_t val) noexcept -> void;
auto pia_6821_set_ca1(Pia6821_t* p, bool level) noexcept -> void;
auto pia_6821_set_ca2(Pia6821_t* p, bool level) noexcept -> void;
auto pia_6821_set_cb1(Pia6821_t* p, bool level) noexcept -> void;
auto pia_6821_set_cb2(Pia6821_t* p, bool level) noexcept -> void;

auto pia_6821_get_port_a(const Pia6821_t* p) noexcept -> uint8_t;
auto pia_6821_get_port_b(const Pia6821_t* p) noexcept -> uint8_t;

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
