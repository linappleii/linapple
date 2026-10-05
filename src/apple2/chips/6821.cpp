// SPDX-License-Identifier: GPL-2.0-only
//
// Hardware reference for "Datasheet Page X" citations in this file:
// Motorola Semiconductor MC6821 NMOS Peripheral Interface Adapter (PIA)
// https://colorcomputerarchive.com/repo/Documents/Datasheets/MC6821%20NMOS%20Peripheral%20Interface%20Adapter%20(Motorola).pdf
#include "apple2/chips/6821.h"

#include <cstdint>

namespace {

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

inline auto pia_call(const PiaWriteHandler_t& h, uint8_t val) noexcept -> void {
  if (h.func != nullptr) {
    h.func(h.obj_to, val);
  }
}

auto update_interrupts(Pia6821_t* p) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  uint8_t irq_a = 0;
  if (((p->cra & CRA_IRQ1) && (p->cra & CRA_CA1_EN)) ||
      ((p->cra & CRA_IRQ2) && (!(p->cra & CRA_CA2_OUT)) &&
       (p->cra & CRA_CA2_LVL))) {
    irq_a = 1;
  }

  if (irq_a != p->irq_a_state) {
    p->irq_a_state = irq_a;
    pia_call(p->out_irqa, p->irq_a_state);
  }

  uint8_t irq_b = 0;
  if (((p->crb & CRB_IRQ1) && (p->crb & CRB_CB1_EN)) ||
      ((p->crb & CRB_IRQ2) && (!(p->crb & CRB_CB2_OUT)) &&
       (p->crb & CRB_CB2_LVL))) {
    irq_b = 1;
  }

  if (irq_b != p->irq_b_state) {
    p->irq_b_state = irq_b;
    pia_call(p->out_irqb, p->irq_b_state);
  }
}

}  // namespace

auto pia_6821_reset(Pia6821_t* p) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  *p = Pia6821_t{};
  // Port A has internal pull-up devices
  p->port_a_in = 0xFF;
  p->port_b_in = 0xFF;
}

auto pia_6821_read(Pia6821_t* p, uint8_t addr) noexcept -> uint8_t {
  if (p == nullptr) {
    return 0;
  }
  switch (addr & 0x03) {
    case 0:
      if (!(p->cra & CRA_DDR_SEL)) {
        return p->ddra;
      }
      p->cra &= ~(CRA_IRQ1 | CRA_IRQ2);
      update_interrupts(p);

      if ((p->cra & (CRA_CA2_OUT | CRA_CA2_SEL)) == CRA_CA2_OUT) {
        if (p->oca2 == 1) {
          p->oca2 = 0;
          pia_call(p->out_ca2, 0);
        }
        if (p->cra & CRA_CA2_LVL) {
          p->oca2 = 1;
          pia_call(p->out_ca2, 1);
        }
      }
      // Datasheet Page 8: When reading Port A, the actual pin is read (not
      // the ORA latch)
      return p->port_a_in;

    case 1:
      // Datasheet Page 10: IRQA2=0 if CA2 is an output
      return ((p->cra & CRA_CA2_OUT) != 0) ? (p->cra & ~CRA_IRQ2) : p->cra;

    case 2:
      if (!(p->crb & CRB_DDR_SEL)) {
        return p->ddrb;
      }
      // Datasheet Page 8: "the B side read comes from an output latch"
      p->crb &= ~(CRB_IRQ1 | CRB_IRQ2);
      update_interrupts(p);
      return (p->orb & p->ddrb) | (p->port_b_in & ~p->ddrb);

    case 3:
      // Datasheet Page 10: IRQB2=0 if CB2 is an output
      return ((p->crb & CRB_CB2_OUT) != 0) ? (p->crb & ~CRB_IRQ2) : p->crb;

    default:
      return 0;
  }
}

auto pia_6821_write(Pia6821_t* p, uint8_t addr, uint8_t val) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  addr &= 0x03;

  switch (addr) {
    case 0:
      if (!(p->cra & CRA_DDR_SEL)) {
        p->ddra = val;
        break;
      }
      p->ora = val;
      pia_call(p->out_a, p->ora & p->ddra);
      break;

    case 1:
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

    case 2:
      if (!(p->crb & CRB_DDR_SEL)) {
        p->ddrb = val;
        break;
      }
      p->orb = val;
      pia_call(p->out_b, p->orb & p->ddrb);

      if ((p->crb & (CRB_CB2_OUT | CRB_CB2_SEL)) == CRB_CB2_OUT) {
        if (p->ocb2 == 1) {
          p->ocb2 = 0;
          pia_call(p->out_cb2, 0);
        }
        if (p->crb & CRB_CB2_LVL) {
          p->ocb2 = 1;
          pia_call(p->out_cb2, 1);
        }
      }
      break;

    case 3:
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

auto pia_6821_set_port_a(Pia6821_t* p, uint8_t val) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  p->port_a_in = val;
}

auto pia_6821_set_port_b(Pia6821_t* p, uint8_t val) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  p->port_b_in = val;
}

auto pia_6821_set_ca1(Pia6821_t* p, bool level) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  const bool old = p->ca1_in;
  p->ca1_in = level;
  const bool transition =
      ((p->cra & CRA_CA1_SEL) != 0) ? (old && !level) : (!old && level);
  if (!transition) {
    return;
  }

  p->cra |= CRA_IRQ1;
  const bool ca2_handshake = ((p->cra & CRA_CA2_OUT) != 0) &&
                             ((p->cra & CRA_CA2_SEL) == 0) &&
                             ((p->cra & CRA_CA2_LVL) == 0);
  if (ca2_handshake && p->oca2 == 0) {
    p->oca2 = 1;
    pia_call(p->out_ca2, 1);
  }
  update_interrupts(p);
}

auto pia_6821_set_ca2(Pia6821_t* p, bool level) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  const bool old = p->ca2_in;
  p->ca2_in = level;
  if ((p->cra & CRA_CA2_OUT) != 0) {
    return;
  }
  const bool transition =
      ((p->cra & CRA_CA2_SEL) != 0) ? (old && !level) : (!old && level);
  if (!transition) {
    return;
  }
  p->cra |= CRA_IRQ2;
  update_interrupts(p);
}

auto pia_6821_set_cb1(Pia6821_t* p, bool level) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  const bool old = p->cb1_in;
  p->cb1_in = level;
  const bool transition =
      ((p->crb & CRB_CB1_SEL) != 0) ? (old && !level) : (!old && level);
  if (!transition) {
    return;
  }

  p->crb |= CRB_IRQ1;
  const bool cb2_handshake = ((p->crb & CRB_CB2_OUT) != 0) &&
                             ((p->crb & CRB_CB2_SEL) == 0) &&
                             ((p->crb & CRB_CB2_LVL) == 0);
  if (cb2_handshake && p->ocb2 == 0) {
    p->ocb2 = 1;
    pia_call(p->out_cb2, 1);
  }
  update_interrupts(p);
}

auto pia_6821_set_cb2(Pia6821_t* p, bool level) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  const bool old = p->cb2_in;
  p->cb2_in = level;
  if ((p->crb & CRB_CB2_OUT) != 0) {
    return;
  }
  const bool transition =
      ((p->crb & CRB_CB2_SEL) != 0) ? (old && !level) : (!old && level);
  if (!transition) {
    return;
  }
  p->crb |= CRB_IRQ2;
  update_interrupts(p);
}

auto pia_6821_get_port_a(const Pia6821_t* p) noexcept -> uint8_t {
  if (p == nullptr) {
    return 0;
  }
  return p->ora & p->ddra;
}

auto pia_6821_get_port_b(const Pia6821_t* p) noexcept -> uint8_t {
  if (p == nullptr) {
    return 0;
  }
  return p->orb & p->ddrb;
}

auto pia_6821_set_listener_a(Pia6821_t* p, void* obj_to,
                             PiaOutputCallback_t func) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  p->out_a.obj_to = obj_to;
  p->out_a.func = func;
}

auto pia_6821_set_listener_b(Pia6821_t* p, void* obj_to,
                             PiaOutputCallback_t func) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  p->out_b.obj_to = obj_to;
  p->out_b.func = func;
}

auto pia_6821_set_listener_ca2(Pia6821_t* p, void* obj_to,
                               PiaOutputCallback_t func) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  p->out_ca2.obj_to = obj_to;
  p->out_ca2.func = func;
}

auto pia_6821_set_listener_cb2(Pia6821_t* p, void* obj_to,
                               PiaOutputCallback_t func) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  p->out_cb2.obj_to = obj_to;
  p->out_cb2.func = func;
}

auto pia_6821_set_listener_irqa(Pia6821_t* p, void* obj_to,
                                PiaOutputCallback_t func) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  p->out_irqa.obj_to = obj_to;
  p->out_irqa.func = func;
}

auto pia_6821_set_listener_irqb(Pia6821_t* p, void* obj_to,
                                PiaOutputCallback_t func) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  p->out_irqb.obj_to = obj_to;
  p->out_irqb.func = func;
}
