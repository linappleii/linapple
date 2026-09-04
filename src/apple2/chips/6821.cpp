// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers) Justification: Hardware register addresses, bitmasks, and control line states from MC6821 datasheet
// NOLINTBEGIN(bugprone-easily-swappable-parameters) Justification: Hardware signal interface parameters and callback bindings
/*
LinApple : Apple ][ emulator for Linux

Copyright (C) 2026, LinApple Team

LinApple is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

LinApple is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with LinApple; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: MC6821 PIA Emulation */

#include "apple2/chips/6821.h"

#include <cstdint>
#include <cstring>

constexpr uint8_t CRA_IRQ1 = 0x80;
constexpr uint8_t CRA_IRQ2 = 0x40;
constexpr uint8_t CRA_CA2_OUT = 0x20;
constexpr uint8_t CRA_CA2_SEL = 0x10;
constexpr uint8_t CRA_CA2_LVL = 0x08;
constexpr uint8_t CRA_DDR_SEL = 0x04;
constexpr uint8_t CRA_CA1_SEL = 0x02;
constexpr uint8_t CRA_CA1_EN = 0x01;

constexpr uint8_t CRB_IRQ1 = 0x80;
constexpr uint8_t CRB_IRQ2 = 0x40;
constexpr uint8_t CRB_CB2_OUT = 0x20;
constexpr uint8_t CRB_CB2_SEL = 0x10;
constexpr uint8_t CRB_CB2_LVL = 0x08;
constexpr uint8_t CRB_DDR_SEL = 0x04;
constexpr uint8_t CRB_CB1_SEL = 0x02;
constexpr uint8_t CRB_CB1_EN = 0x01;

static inline auto pia_call(const PiaWriteHandler_t& h, uint8_t val) -> void {
  if (h.func != nullptr) {
    h.func(h.obj_to, val);
  }
}

static auto update_interrupts(Pia6821_t* p) -> void {
  uint8_t irq_a = 0;
  if (((p->cra & CRA_IRQ1) && (p->cra & CRA_CA1_EN)) ||
      ((p->cra & CRA_IRQ2) && (!(p->cra & CRA_CA2_OUT)) && (p->cra & 0x08))) {
    irq_a = 1;
  }

  if (irq_a != p->irq_a_state) {
    p->irq_a_state = irq_a;
    pia_call(p->out_irqa, p->irq_a_state);
  }

  uint8_t irq_b = 0;
  if (((p->crb & CRB_IRQ1) && (p->crb & CRB_CB1_EN)) ||
      ((p->crb & CRB_IRQ2) && (!(p->crb & CRB_CB2_OUT)) && (p->crb & 0x08))) {
    irq_b = 1;
  }

  if (irq_b != p->irq_b_state) {
    p->irq_b_state = irq_b;
    pia_call(p->out_irqb, p->irq_b_state);
  }
}

auto pia_6821_reset(Pia6821_t* p) -> void {
  memset(p, 0, sizeof(Pia6821_t));
  // Port A has internal pull-up devices
  p->port_a_in = 0xFF;
  p->port_b_in = 0xFF;
}

auto pia_6821_read(Pia6821_t* p, uint8_t addr) -> uint8_t {
  uint8_t val = 0;
  addr &= 0x03;

  switch (addr) {
    case 0:  // Port A or DDRA
      if (p->cra & CRA_DDR_SEL) {
        // Datasheet Page 8: "When reading Port A, the actual pin is read"
        val = p->port_a_in;
        p->cra &= ~(CRA_IRQ1 | CRA_IRQ2);
        update_interrupts(p);

        if (p->cra & CRA_CA2_OUT) {
          if (!(p->cra & CRA_CA2_SEL)) {  // Read Strobe Mode
            if (p->oca2 == 1) {
              p->oca2 = 0;
              pia_call(p->out_ca2, 0);
            }
            if (p->cra & CRA_CA2_LVL) {  // E-Reset
              p->oca2 = 1;
              pia_call(p->out_ca2, 1);
            }
          }
        }
      } else {
        val = p->ddra;
      }
      break;

    case 1:  // Control A
      val = p->cra;
      // Datasheet Page 10: IRQA2=0 if CA2 is an output
      if (p->cra & CRA_CA2_OUT) val &= ~CRA_IRQ2;
      break;

    case 2:  // Port B or DDRB
      if (p->crb & CRB_DDR_SEL) {
        // Datasheet Page 8: "the B side read comes from an output latch"
        val = (p->orb & p->ddrb) | (p->port_b_in & ~p->ddrb);
        p->crb &= ~(CRB_IRQ1 | CRB_IRQ2);
        update_interrupts(p);
      } else {
        val = p->ddrb;
      }
      break;

    case 3:  // Control B
      val = p->crb;
      // Datasheet Page 10: IRQB2=0 if CB2 is an output
      if (p->crb & CRB_CB2_OUT) val &= ~CRB_IRQ2;
      break;

    default:
      break;
  }
  return val;
}

auto pia_6821_write(Pia6821_t* p, uint8_t addr, uint8_t val) -> void {
  addr &= 0x03;

  switch (addr) {
    case 0:  // Port A or DDRA
      if (p->cra & CRA_DDR_SEL) {
        p->ora = val;
        pia_call(p->out_a, p->ora & p->ddra);
      } else {
        p->ddra = val;
      }
      break;

    case 1:  // Control A
      p->cra = (p->cra & 0xC0) | (val & 0x3F);

      if ((p->cra & CRA_CA2_OUT) && (p->cra & CRA_CA2_SEL)) {
        uint8_t next_ca2 = (p->cra & CRA_CA2_LVL) ? 1 : 0;
        if (next_ca2 != p->oca2) {
          p->oca2 = next_ca2;
          pia_call(p->out_ca2, p->oca2);
        }
      }
      update_interrupts(p);
      break;

    case 2:  // Port B or DDRB
      if (p->crb & CRB_DDR_SEL) {
        p->orb = val;
        pia_call(p->out_b, p->orb & p->ddrb);

        if (p->crb & CRB_CB2_OUT) {
          if (!(p->crb & CRB_CB2_SEL)) {  // Write Strobe Mode
            if (p->ocb2 == 1) {
              p->ocb2 = 0;
              pia_call(p->out_cb2, 0);
            }
            if (p->crb & CRB_CB2_LVL) {  // E-Reset
              p->ocb2 = 1;
              pia_call(p->out_cb2, 1);
            }
          }
        }
      } else {
        p->ddrb = val;
      }
      break;

    case 3:  // Control B
      p->crb = (p->crb & 0xC0) | (val & 0x3F);

      if ((p->crb & CRB_CB2_OUT) && (p->crb & CRB_CB2_SEL)) {
        uint8_t next_cb2 = (p->crb & CRB_CB2_LVL) ? 1 : 0;
        if (next_cb2 != p->ocb2) {
          p->ocb2 = next_cb2;
          pia_call(p->out_cb2, p->ocb2);
        }
      }
      update_interrupts(p);
      break;

    default:
      break;
  }
}

auto pia_6821_set_port_a(Pia6821_t* p, uint8_t val) -> void {
  p->port_a_in = val;
}
auto pia_6821_set_port_b(Pia6821_t* p, uint8_t val) -> void {
  p->port_b_in = val;
}

auto pia_6821_set_ca1(Pia6821_t* p, bool level) -> void {
  bool old = p->ca1_in;
  p->ca1_in = level;
  bool transition = false;
  if (p->cra & CRA_CA1_SEL) {  // High-to-Low
    if (old && !level) transition = true;
  } else {  // Low-to-High
    if (!old && level) transition = true;
  }

  if (transition) {
    p->cra |= CRA_IRQ1;
    if ((p->cra & CRA_CA2_OUT) && (!(p->cra & CRA_CA2_SEL)) &&
        (!(p->cra & CRA_CA2_LVL))) {
      if (p->oca2 == 0) {
        p->oca2 = 1;
        pia_call(p->out_ca2, 1);
      }
    }
    update_interrupts(p);
  }
}

auto pia_6821_set_ca2(Pia6821_t* p, bool level) -> void {
  bool old = p->ca2_in;
  p->ca2_in = level;
  if (!(p->cra & CRA_CA2_OUT)) {  // Input mode
    bool transition = false;
    if (p->cra & CRA_CA2_SEL) {  // High-to-Low
      if (old && !level) transition = true;
    } else {  // Low-to-High
      if (!old && level) transition = true;
    }
    if (transition) {
      p->cra |= CRA_IRQ2;
      update_interrupts(p);
    }
  }
}

auto pia_6821_set_cb1(Pia6821_t* p, bool level) -> void {
  bool old = p->cb1_in;
  p->cb1_in = level;
  bool transition = false;
  if (p->crb & CRB_CB1_SEL) {  // High-to-Low
    if (old && !level) transition = true;
  } else {  // Low-to-High
    if (!old && level) transition = true;
  }

  if (transition) {
    p->crb |= CRB_IRQ1;
    if ((p->crb & CRB_CB2_OUT) && (!(p->crb & CRB_CB2_SEL)) &&
        (!(p->crb & CRB_CB2_LVL))) {
      if (p->ocb2 == 0) {
        p->ocb2 = 1;
        pia_call(p->out_cb2, 1);
      }
    }
    update_interrupts(p);
  }
}

auto pia_6821_set_cb2(Pia6821_t* p, bool level) -> void {
  bool old = p->cb2_in;
  p->cb2_in = level;
  if (!(p->crb & CRB_CB2_OUT)) {  // Input mode
    bool transition = false;
    if (p->crb & CRB_CB2_SEL) {  // High-to-Low
      if (old && !level) transition = true;
    } else {  // Low-to-High
      if (!old && level) transition = true;
    }
    if (transition) {
      p->crb |= CRB_IRQ2;
      update_interrupts(p);
    }
  }
}

auto pia_6821_get_port_a(Pia6821_t* p) -> uint8_t { return p->ora & p->ddra; }
auto pia_6821_get_port_b(Pia6821_t* p) -> uint8_t { return p->orb & p->ddrb; }

auto pia_6821_set_listener_a(Pia6821_t* p, void* obj_to,
                             PiaOutputCallback_t func) -> void {
  p->out_a.obj_to = obj_to;
  p->out_a.func = func;
}
auto pia_6821_set_listener_b(Pia6821_t* p, void* obj_to,
                             PiaOutputCallback_t func) -> void {
  p->out_b.obj_to = obj_to;
  p->out_b.func = func;
}
auto pia_6821_set_listener_ca2(Pia6821_t* p, void* obj_to,
                               PiaOutputCallback_t func) -> void {
  p->out_ca2.obj_to = obj_to;
  p->out_ca2.func = func;
}
auto pia_6821_set_listener_cb2(Pia6821_t* p, void* obj_to,
                               PiaOutputCallback_t func) -> void {
  p->out_cb2.obj_to = obj_to;
  p->out_cb2.func = func;
}
auto pia_6821_set_listener_irqa(Pia6821_t* p, void* obj_to,
                                PiaOutputCallback_t func) -> void {
  p->out_irqa.obj_to = obj_to;
  p->out_irqa.func = func;
}
auto pia_6821_set_listener_irqb(Pia6821_t* p, void* obj_to,
                                PiaOutputCallback_t func) -> void {
  p->out_irqb.obj_to = obj_to;
  p->out_irqb.func = func;
}
// NOLINTEND(bugprone-easily-swappable-parameters)
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
