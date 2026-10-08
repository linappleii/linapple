// SPDX-License-Identifier: GPL-2.0-only
//
// Hardware reference for "Datasheet Page X" citations in this file:
// Motorola Semiconductor MC6821 NMOS Peripheral Interface Adapter (PIA)
// https://colorcomputerarchive.com/repo/Documents/Datasheets/MC6821%20NMOS%20Peripheral%20Interface%20Adapter%20(Motorola).pdf
#include "apple2/chips/6821.h"

#include <cstdint>

namespace {

namespace cra_mask {
constexpr uint8_t irq1 = 0x80;
constexpr uint8_t irq2 = 0x40;
constexpr uint8_t ca2_out = 0x20;
constexpr uint8_t ca2_sel = 0x10;
constexpr uint8_t ca2_lvl = 0x08;
constexpr uint8_t ddr_sel = 0x04;
constexpr uint8_t ca1_sel = 0x02;
constexpr uint8_t ca1_en = 0x01;
}  // namespace cra_mask

namespace crb_mask {
constexpr uint8_t irq1 = 0x80;
constexpr uint8_t irq2 = 0x40;
constexpr uint8_t cb2_out = 0x20;
constexpr uint8_t cb2_sel = 0x10;
constexpr uint8_t cb2_lvl = 0x08;
constexpr uint8_t ddr_sel = 0x04;
constexpr uint8_t cb1_sel = 0x02;
constexpr uint8_t cb1_en = 0x01;
}  // namespace crb_mask

inline auto pia_call(const PiaWriteHandler& h, uint8_t val) noexcept -> void {
  if (h.func != nullptr) {
    h.func(h.obj_to, val);
  }
}

auto update_interrupts(Pia6821* p) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  uint8_t irq_a = 0;
  if (((p->cra & cra_mask::irq1) != 0 && (p->cra & cra_mask::ca1_en) != 0) ||
      ((p->cra & cra_mask::irq2) != 0 && (p->cra & cra_mask::ca2_out) == 0 &&
       (p->cra & cra_mask::ca2_lvl) != 0)) {
    irq_a = 1;
  }

  if (irq_a != p->irq_a_state) {
    p->irq_a_state = irq_a;
    pia_call(p->out_irqa, p->irq_a_state);
  }

  uint8_t irq_b = 0;
  if (((p->crb & crb_mask::irq1) != 0 && (p->crb & crb_mask::cb1_en) != 0) ||
      ((p->crb & crb_mask::irq2) != 0 && (p->crb & crb_mask::cb2_out) == 0 &&
       (p->crb & crb_mask::cb2_lvl) != 0)) {
    irq_b = 1;
  }

  if (irq_b != p->irq_b_state) {
    p->irq_b_state = irq_b;
    pia_call(p->out_irqb, p->irq_b_state);
  }
}

}  // namespace

auto pia_6821_reset(Pia6821* p) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  *p = Pia6821{};
  // Port A has internal pull-up devices
  p->port_a_in = 0xFF;
  p->port_b_in = 0xFF;
}

auto pia_6821_read(Pia6821* p, uint8_t addr) noexcept -> uint8_t {
  if (p == nullptr) {
    return 0;
  }
  switch (addr & 0x03) {
    case 0:
      if ((p->cra & cra_mask::ddr_sel) == 0) {
        return p->ddra;
      }
      p->cra &= ~(cra_mask::irq1 | cra_mask::irq2);
      update_interrupts(p);

      if ((p->cra & (cra_mask::ca2_out | cra_mask::ca2_sel)) == cra_mask::ca2_out) {
        if (p->oca2 == 1) {
          p->oca2 = 0;
          pia_call(p->out_ca2, 0);
        }
        if ((p->cra & cra_mask::ca2_lvl) != 0) {
          p->oca2 = 1;
          pia_call(p->out_ca2, 1);
        }
      }
      // Datasheet Page 8: When reading Port A, the actual pin is read (not
      // the ORA latch)
      return p->port_a_in;

    case 1:
      // Datasheet Page 10: IRQA2=0 if CA2 is an output
      return ((p->cra & cra_mask::ca2_out) != 0) ? (p->cra & ~cra_mask::irq2) : p->cra;

    case 2:
      if ((p->crb & crb_mask::ddr_sel) == 0) {
        return p->ddrb;
      }
      // Datasheet Page 8: "the B side read comes from an output latch"
      p->crb &= ~(crb_mask::irq1 | crb_mask::irq2);
      update_interrupts(p);
      return (p->orb & p->ddrb) | (p->port_b_in & ~p->ddrb);

    case 3:
      // Datasheet Page 10: IRQB2=0 if CB2 is an output
      return ((p->crb & crb_mask::cb2_out) != 0) ? (p->crb & ~crb_mask::irq2) : p->crb;

    default:
      return 0;
  }
}

auto pia_6821_write(Pia6821* p, uint8_t addr, uint8_t val) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  addr &= 0x03;

  switch (addr) {
    case 0:
      if ((p->cra & cra_mask::ddr_sel) == 0) {
        p->ddra = val;
        break;
      }
      p->ora = val;
      pia_call(p->out_a, p->ora & p->ddra);
      break;

    case 1:
      p->cra = (p->cra & 0xC0) | (val & 0x3F);

      if ((p->cra & cra_mask::ca2_out) != 0 && (p->cra & cra_mask::ca2_sel) != 0) {
        const uint8_t next_ca2 = ((p->cra & cra_mask::ca2_lvl) != 0) ? 1 : 0;
        if (next_ca2 != p->oca2) {
          p->oca2 = next_ca2;
          pia_call(p->out_ca2, p->oca2);
        }
      }
      update_interrupts(p);
      break;

    case 2:
      if ((p->crb & crb_mask::ddr_sel) == 0) {
        p->ddrb = val;
        break;
      }
      p->orb = val;
      pia_call(p->out_b, p->orb & p->ddrb);

      if ((p->crb & (crb_mask::cb2_out | crb_mask::cb2_sel)) == crb_mask::cb2_out) {
        if (p->ocb2 == 1) {
          p->ocb2 = 0;
          pia_call(p->out_cb2, 0);
        }
        if ((p->crb & crb_mask::cb2_lvl) != 0) {
          p->ocb2 = 1;
          pia_call(p->out_cb2, 1);
        }
      }
      break;

    case 3:
      p->crb = (p->crb & 0xC0) | (val & 0x3F);

      if ((p->crb & crb_mask::cb2_out) != 0 && (p->crb & crb_mask::cb2_sel) != 0) {
        const uint8_t next_cb2 = ((p->crb & crb_mask::cb2_lvl) != 0) ? 1 : 0;
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

auto pia_6821_set_port_a(Pia6821* p, uint8_t val) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  p->port_a_in = val;
}

auto pia_6821_set_port_b(Pia6821* p, uint8_t val) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  p->port_b_in = val;
}

auto pia_6821_set_ca1(Pia6821* p, bool level) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  const bool old = p->ca1_in;
  p->ca1_in = level;
  const bool transition =
      ((p->cra & cra_mask::ca1_sel) != 0) ? (old && !level) : (!old && level);
  if (!transition) {
    return;
  }

  p->cra |= cra_mask::irq1;
  const bool ca2_handshake = ((p->cra & cra_mask::ca2_out) != 0) &&
                             ((p->cra & cra_mask::ca2_sel) == 0) &&
                             ((p->cra & cra_mask::ca2_lvl) == 0);
  if (ca2_handshake && p->oca2 == 0) {
    p->oca2 = 1;
    pia_call(p->out_ca2, 1);
  }
  update_interrupts(p);
}

auto pia_6821_set_ca2(Pia6821* p, bool level) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  const bool old = p->ca2_in;
  p->ca2_in = level;
  if ((p->cra & cra_mask::ca2_out) != 0) {
    return;
  }
  const bool transition =
      ((p->cra & cra_mask::ca2_sel) != 0) ? (old && !level) : (!old && level);
  if (!transition) {
    return;
  }
  p->cra |= cra_mask::irq2;
  update_interrupts(p);
}

auto pia_6821_set_cb1(Pia6821* p, bool level) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  const bool old = p->cb1_in;
  p->cb1_in = level;
  const bool transition =
      ((p->crb & crb_mask::cb1_sel) != 0) ? (old && !level) : (!old && level);
  if (!transition) {
    return;
  }

  p->crb |= crb_mask::irq1;
  const bool cb2_handshake = ((p->crb & crb_mask::cb2_out) != 0) &&
                             ((p->crb & crb_mask::cb2_sel) == 0) &&
                             ((p->crb & crb_mask::cb2_lvl) == 0);
  if (cb2_handshake && p->ocb2 == 0) {
    p->ocb2 = 1;
    pia_call(p->out_cb2, 1);
  }
  update_interrupts(p);
}

auto pia_6821_set_cb2(Pia6821* p, bool level) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  const bool old = p->cb2_in;
  p->cb2_in = level;
  if ((p->crb & crb_mask::cb2_out) != 0) {
    return;
  }
  const bool transition =
      ((p->crb & crb_mask::cb2_sel) != 0) ? (old && !level) : (!old && level);
  if (!transition) {
    return;
  }
  p->crb |= crb_mask::irq2;
  update_interrupts(p);
}

auto pia_6821_get_port_a(const Pia6821* p) noexcept -> uint8_t {
  if (p == nullptr) {
    return 0;
  }
  return p->ora & p->ddra;
}

auto pia_6821_get_port_b(const Pia6821* p) noexcept -> uint8_t {
  if (p == nullptr) {
    return 0;
  }
  return p->orb & p->ddrb;
}

auto pia_6821_set_listener_a(Pia6821* p, void* obj_to,
                             PiaOutputCallback func) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  p->out_a.obj_to = obj_to;
  p->out_a.func = func;
}

auto pia_6821_set_listener_b(Pia6821* p, void* obj_to,
                             PiaOutputCallback func) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  p->out_b.obj_to = obj_to;
  p->out_b.func = func;
}

auto pia_6821_set_listener_ca2(Pia6821* p, void* obj_to,
                               PiaOutputCallback func) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  p->out_ca2.obj_to = obj_to;
  p->out_ca2.func = func;
}

auto pia_6821_set_listener_cb2(Pia6821* p, void* obj_to,
                               PiaOutputCallback func) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  p->out_cb2.obj_to = obj_to;
  p->out_cb2.func = func;
}

auto pia_6821_set_listener_irqa(Pia6821* p, void* obj_to,
                                PiaOutputCallback func) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  p->out_irqa.obj_to = obj_to;
  p->out_irqa.func = func;
}

auto pia_6821_set_listener_irqb(Pia6821* p, void* obj_to,
                                PiaOutputCallback func) noexcept -> void {
  if (p == nullptr) {
    return;
  }
  p->out_irqb.obj_to = obj_to;
  p->out_irqb.func = func;
}
