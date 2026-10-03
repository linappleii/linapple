// SPDX-License-Identifier: GPL-2.0-only
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstddef>
#include <cstdint>
#include <vector>

#include "apple2/chips/6551.h"
#include "doctest.h"

namespace {

// The NTSC Apple II clock, 14.318 MHz x 65 / 912, in millihertz.
constexpr uint64_t ntsc_clock_mhz = 1020484450;
constexpr uint64_t char_9600_8n1 = 1063;
constexpr uint64_t rdrf_9600_8n1 = 1016;
constexpr uint64_t char_50_8n1 = 204097;
constexpr uint64_t rdrf_50_8n1 = 195168;
constexpr uint64_t char_9600_5n15 = 797;
constexpr uint64_t char_9600_8s1 = 1169;

constexpr uint8_t control_9600_8n1 = 0x1E;
constexpr uint8_t control_50_8n1 = 0x11;
constexpr uint8_t control_9600_5n15 = 0xFE;
constexpr uint8_t command_rx_irq = 0x09;
constexpr uint8_t command_no_irq = 0x0B;
constexpr uint8_t command_tx_irq = 0x05;
constexpr uint8_t command_dtr_only = 0x01;
constexpr uint8_t command_break = 0x0F;
constexpr uint8_t command_echo = 0x11;
constexpr uint8_t command_space_parity_rx_irq = 0xE9;

struct Out_t {
  uint64_t at;
  uint8_t byte;
};

struct Bench_t {
  Acia6551_t acia;
  std::vector<Out_t> sent;

  Bench_t() {
    acia_set_clock_mhz(&acia, ntsc_clock_mhz);
    acia_reset(&acia, 0);
  }

  auto step(uint64_t now) -> void {
    uint8_t byte = 0;
    while (acia_step(&acia, now, &byte)) {
      sent.push_back({now, byte});
    }
  }

  auto write(uint8_t reg, uint8_t value, uint64_t now) -> void {
    uint8_t byte = 0;
    if (acia_write(&acia, reg, value, now, &byte)) {
      sent.push_back({now, byte});
    }
    step(now);
  }

  auto read(uint8_t reg, uint64_t now) -> uint8_t {
    step(now);
    return acia_read(&acia, reg, now);
  }

  auto program(uint8_t control, uint8_t command, uint64_t now) -> void {
    write(acia_reg::control, control, now);
    write(acia_reg::command, command, now);
  }

  auto tdre(uint64_t now) -> bool {
    return (read(acia_reg::status, now) & acia_status::tdre) != 0;
  }

  auto line() -> AciaLine_t {
    AciaLine_t out;
    acia_line_view(&acia, &out);
    return out;
  }
};

}  // namespace

TEST_CASE(
    "6551: hardware reset clears control and command, sets TDRE, reads the "
    "inputs asserted and keeps the receive data register") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_no_irq, 0);
  acia_rx_start(&bench.acia, 0xC1, 0, 0);
  bench.step(char_9600_8n1);
  CHECK(bench.read(acia_reg::data, char_9600_8n1) == 0xC1);

  acia_reset(&bench.acia, 2000);
  CHECK(bench.read(acia_reg::control, 2000) == 0x00);
  CHECK(bench.read(acia_reg::command, 2000) == 0x00);
  CHECK(bench.read(acia_reg::status, 2000) == 0x10);
  CHECK_FALSE(acia_irq(&bench.acia));
  CHECK(bench.read(acia_reg::data, 2000) == 0xC1);
  CHECK(acia_next_event(&bench.acia) == 0);
  CHECK_FALSE(acia_rx_ready(&bench.acia));
}

TEST_CASE(
    "6551: a received byte sets RDRF and IRQ together, the status read "
    "clears IRQ alone and the data read clears RDRF alone") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_rx_irq, 0);
  REQUIRE(acia_rx_ready(&bench.acia));
  acia_rx_start(&bench.acia, 0xC1, 0, 0);

  CHECK(bench.read(acia_reg::status, rdrf_9600_8n1 - 1) == 0x10);
  CHECK_FALSE(acia_irq(&bench.acia));
  bench.step(rdrf_9600_8n1);
  CHECK(acia_irq(&bench.acia));

  CHECK(bench.read(acia_reg::status, 1100) == 0x98);
  CHECK_FALSE(acia_irq(&bench.acia));
  CHECK(bench.read(acia_reg::status, 1110) == 0x18);
  CHECK(bench.read(acia_reg::status, 1120) == 0x18);
  CHECK(bench.read(acia_reg::data, 1130) == 0xC1);
  CHECK(bench.read(acia_reg::status, 1140) == 0x10);
}

TEST_CASE(
    "6551: with the data read first the status read returns $90 and only "
    "then releases IRQ; with the receiver interrupt disabled nothing is "
    "raised") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_rx_irq, 0);
  acia_rx_start(&bench.acia, 0xC1, 0, 0);
  CHECK(bench.read(acia_reg::data, 1100) == 0xC1);
  CHECK(acia_irq(&bench.acia));
  CHECK(bench.read(acia_reg::status, 1110) == 0x90);
  CHECK_FALSE(acia_irq(&bench.acia));
  CHECK(bench.read(acia_reg::status, 1120) == 0x10);

  Bench_t quiet;
  quiet.program(control_9600_8n1, command_no_irq, 0);
  acia_rx_start(&quiet.acia, 0xC1, 0, 0);
  CHECK(quiet.read(acia_reg::status, 1100) == 0x18);
  CHECK_FALSE(acia_irq(&quiet.acia));
  CHECK(quiet.read(acia_reg::data, 1110) == 0xC1);
  CHECK(quiet.read(acia_reg::status, 1120) == 0x10);
}

TEST_CASE(
    "6551: a byte written to an idle transmitter leaves at the write and "
    "TDRE is set at the first read; one written behind it waits exactly one "
    "character time at 9600, 50 baud and 5N1.5") {
  struct Row_t {
    uint8_t control;
    uint64_t character;
  };
  const Row_t rows[] = {{control_9600_8n1, char_9600_8n1},
                        {control_50_8n1, char_50_8n1},
                        {control_9600_5n15, char_9600_5n15}};
  for (const Row_t& row : rows) {
    CAPTURE(row.character);
    Bench_t bench;
    bench.program(row.control, command_no_irq, 0);
    constexpr uint64_t t = 100;
    bench.write(acia_reg::data, 0xC8, t);
    REQUIRE(bench.sent.size() == 1);
    CHECK(bench.sent.at(0).at == t);
    CHECK(bench.sent.at(0).byte == 0xC8);
    CHECK(bench.tdre(t));

    bench.write(acia_reg::data, 0xC5, t + 4);
    CHECK_FALSE(bench.tdre(t + 4));
    CHECK_FALSE(bench.tdre(t + row.character - 1));
    CHECK(bench.sent.size() == 1);
    CHECK(bench.tdre(t + row.character));
    REQUIRE(bench.sent.size() == 2);
    CHECK(bench.sent.at(1).byte == 0xC5);
    CHECK(acia_next_event(&bench.acia) == t + 2 * row.character);
  }
}

TEST_CASE(
    "6551: three writes four cycles apart deliver the first and third bytes, "
    "the second having been replaced in the TDR before the shifter freed") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_no_irq, 0);
  bench.write(acia_reg::data, 0x01, 100);
  bench.write(acia_reg::data, 0x02, 104);
  bench.write(acia_reg::data, 0x03, 108);
  bench.step(100 + 2 * char_9600_8n1);
  REQUIRE(bench.sent.size() == 2);
  CHECK(bench.sent.at(0).byte == 0x01);
  CHECK(bench.sent.at(1).byte == 0x03);
  CHECK(bench.tdre(100 + 2 * char_9600_8n1));
}

TEST_CASE(
    "6551: a full TDR moves at the character boundary and not at the poll "
    "that notices it, so ten bytes behind a poll take ten character times") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_no_irq, 0);
  constexpr uint64_t t = 1000;
  constexpr uint64_t poll_period = 27;
  uint64_t now = t;
  bench.write(acia_reg::data, 0x10, now);
  int written = 1;
  while (written < 10) {
    now += poll_period;
    if (bench.tdre(now)) {
      bench.write(acia_reg::data, static_cast<uint8_t>(0x10 + written), now);
      ++written;
    }
  }
  bench.step(t + 11 * char_9600_8n1);
  REQUIRE(bench.sent.size() == 10);
  for (size_t i = 0; i < bench.sent.size(); ++i) {
    CHECK(bench.sent.at(i).byte == 0x10 + i);
  }
  CHECK(bench.acia.tx_busy_until == t + 10 * char_9600_8n1);
}

TEST_CASE(
    "6551: with TIC 01 the interrupt is raised at the write that empties the "
    "TDR and then once per character time while it stays empty, each write "
    "re-anchoring the clock") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_tx_irq, 0);
  constexpr uint64_t t = 500;
  bench.write(acia_reg::data, 0xC1, t);
  CHECK(acia_irq(&bench.acia));
  CHECK((bench.read(acia_reg::status, t) & 0x90) == 0x90);
  CHECK_FALSE(acia_irq(&bench.acia));

  bench.step(t + char_9600_8n1 - 1);
  CHECK_FALSE(acia_irq(&bench.acia));
  bench.step(t + char_9600_8n1);
  CHECK(acia_irq(&bench.acia));
  CHECK(acia_next_event(&bench.acia) == t + 2 * char_9600_8n1);
  bench.read(acia_reg::status, t + char_9600_8n1);
  bench.step(t + 2 * char_9600_8n1 - 1);
  CHECK_FALSE(acia_irq(&bench.acia));
  bench.step(t + 2 * char_9600_8n1);
  CHECK(acia_irq(&bench.acia));
  bench.read(acia_reg::status, t + 2 * char_9600_8n1);

  constexpr uint64_t t2 = t + 2 * char_9600_8n1 + 400;
  bench.write(acia_reg::data, 0xC2, t2);
  CHECK(acia_irq(&bench.acia));
  bench.read(acia_reg::status, t2);
  CHECK(acia_next_event(&bench.acia) == t2 + char_9600_8n1);
  bench.step(t2 + char_9600_8n1 - 1);
  CHECK_FALSE(acia_irq(&bench.acia));
  bench.step(t2 + char_9600_8n1);
  CHECK(acia_irq(&bench.acia));
  bench.read(acia_reg::status, t2 + char_9600_8n1);

  bench.write(acia_reg::command, command_no_irq, t2 + char_9600_8n1 + 10);
  bench.step(t2 + 4 * char_9600_8n1);
  CHECK_FALSE(acia_irq(&bench.acia));
  CHECK(acia_next_event(&bench.acia) == 0);
  CHECK(bench.sent.size() == 2);
}

TEST_CASE(
    "6551: setting TIC 01 with the TDR already empty raises the first "
    "interrupt within one character time") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_no_irq, 0);
  bench.write(acia_reg::command, command_tx_irq, 300);
  const uint64_t next = acia_next_event(&bench.acia);
  CHECK(next > 300);
  CHECK(next <= 300 + char_9600_8n1);
  bench.step(next - 1);
  CHECK_FALSE(acia_irq(&bench.acia));
  bench.step(next);
  CHECK(acia_irq(&bench.acia));
}

TEST_CASE(
    "6551: a programmed reset clears command bits 4-0 and the overrun bit, "
    "keeps control, RDRF and a data interrupt, and releases a DSR/DCD one") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_space_parity_rx_irq, 0);
  acia_rx_start(&bench.acia, 0xC1, 0, 0);
  bench.step(char_9600_8s1);
  acia_rx_start(&bench.acia, 0xC2, 0, char_9600_8s1);
  bench.step(3 * char_9600_8s1);
  CHECK((bench.acia.status_latches & acia_status::overrun) != 0);
  CHECK(acia_irq(&bench.acia));

  bench.write(acia_reg::status, 0x00, 3 * char_9600_8s1 + 10);
  CHECK(bench.read(acia_reg::control, 3 * char_9600_8s1 + 10) ==
        control_9600_8n1);
  CHECK(bench.read(acia_reg::command, 3 * char_9600_8s1 + 10) == 0xE0);
  CHECK(acia_irq(&bench.acia));
  CHECK(bench.read(acia_reg::status, 3 * char_9600_8s1 + 20) == 0x98);
  CHECK(bench.read(acia_reg::data, 3 * char_9600_8s1 + 30) == 0xC1);
  CHECK(bench.line().dtr == 0);

  Bench_t lines;
  lines.program(control_9600_8n1, command_no_irq, 0);
  acia_set_lines(&lines.acia, acia_line::cts | acia_line::dcd, 100);
  CHECK(acia_irq(&lines.acia));
  lines.write(acia_reg::status, 0x00, 110);
  CHECK_FALSE(acia_irq(&lines.acia));
}

TEST_CASE(
    "6551: with DTR deasserted the receiver takes nothing and no interrupt "
    "is raised; with DCD deasserted the receiver stops") {
  Bench_t bench;
  bench.program(control_9600_8n1, 0x08, 0);
  CHECK_FALSE(acia_rx_ready(&bench.acia));
  acia_set_lines(&bench.acia, acia_line::cts, 10);
  CHECK_FALSE(acia_irq(&bench.acia));

  bench.write(acia_reg::command, command_rx_irq, 20);
  CHECK_FALSE(acia_rx_ready(&bench.acia));
  acia_set_lines(&bench.acia, acia_line::all_asserted, 30);
  CHECK(acia_irq(&bench.acia));
  CHECK(acia_rx_ready(&bench.acia));
}

TEST_CASE(
    "6551: TIC 00 parks a written byte until the transmitter is turned on, "
    "and a deasserted CTS parks it until CTS is asserted again") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_dtr_only, 0);
  bench.write(acia_reg::data, 0xC8, 100);
  CHECK(bench.sent.empty());
  CHECK_FALSE(bench.tdre(100 + 2 * char_9600_8n1));
  bench.write(acia_reg::command, command_no_irq, 3000);
  REQUIRE(bench.sent.size() == 1);
  CHECK(bench.sent.at(0).at == 3000);
  CHECK(bench.tdre(3000));

  Bench_t cts;
  cts.program(control_9600_8n1, command_no_irq, 0);
  acia_set_lines(&cts.acia, acia_line::dsr | acia_line::dcd, 50);
  cts.write(acia_reg::data, 0xC8, 100);
  CHECK(cts.sent.empty());
  CHECK_FALSE(cts.tdre(100 + 2 * char_9600_8n1));
  acia_set_lines(&cts.acia, acia_line::all_asserted, 3000);
  cts.step(3000);
  REQUIRE(cts.sent.size() == 1);
  CHECK(cts.tdre(3000));
}

TEST_CASE(
    "6551: TIC 11 sends nothing and reports the break level from the next "
    "character boundary until the mode is left") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_no_irq, 0);
  bench.write(acia_reg::data, 0xC1, 100);
  bench.write(acia_reg::command, command_break, 200);
  CHECK(bench.line().brk == 0);
  CHECK(acia_next_event(&bench.acia) == 100 + char_9600_8n1);
  bench.step(100 + char_9600_8n1 - 1);
  CHECK(bench.line().brk == 0);
  bench.step(100 + char_9600_8n1);
  CHECK(bench.line().brk == 1);
  bench.write(acia_reg::data, 0xC2, 2000);
  bench.step(6000);
  CHECK(bench.sent.size() == 1);
  CHECK(bench.line().brk == 1);
  bench.write(acia_reg::command, command_no_irq, 6000);
  CHECK(bench.line().brk == 0);
  REQUIRE(bench.sent.size() == 2);
  CHECK(bench.sent.at(1).byte == 0xC2);
}

TEST_CASE("6551: echo mode sends each received byte out again") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_echo, 0);
  acia_rx_start(&bench.acia, 0xC1, 0, 0);
  bench.step(rdrf_9600_8n1 - 1);
  CHECK(bench.sent.empty());
  bench.step(rdrf_9600_8n1);
  REQUIRE(bench.sent.size() == 1);
  CHECK(bench.sent.at(0).byte == 0xC1);
}

TEST_CASE(
    "6551: RDRF is set 9/16 into the stop bit and the receiver is free at the "
    "frame's end, the two wake points coming in turn, at 9600 and 50 baud") {
  struct Row_t {
    uint8_t control;
    uint64_t rdrf;
    uint64_t free;
  };
  const Row_t rows[] = {{control_9600_8n1, rdrf_9600_8n1, char_9600_8n1},
                        {control_50_8n1, rdrf_50_8n1, char_50_8n1}};
  for (const Row_t& row : rows) {
    CAPTURE(row.free);
    Bench_t bench;
    bench.program(row.control, command_no_irq, 0);
    acia_rx_start(&bench.acia, 0xC1, 0, 0);
    CHECK(acia_next_event(&bench.acia) == row.rdrf);
    CHECK_FALSE(acia_rx_ready(&bench.acia));
    bench.step(row.rdrf - 1);
    CHECK((bench.read(acia_reg::status, row.rdrf - 1) & acia_status::rdrf) ==
          0);
    bench.step(row.rdrf);
    CHECK((bench.read(acia_reg::status, row.rdrf) & acia_status::rdrf) != 0);
    CHECK(acia_next_event(&bench.acia) == row.free);
    CHECK(bench.read(acia_reg::data, row.rdrf + 1) == 0xC1);
    CHECK_FALSE(acia_rx_ready(&bench.acia));
    bench.step(row.free);
    CHECK(acia_rx_ready(&bench.acia));
    CHECK(acia_next_event(&bench.acia) == 0);
  }
}

TEST_CASE(
    "6551: a byte pulled at the first opportunity starts at the free point or "
    "the data read, and one that waited starts when it is noticed") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_no_irq, 0);
  acia_rx_start(&bench.acia, 0xC1, 0, 0);
  bench.read(acia_reg::data, rdrf_9600_8n1 + 20);
  bench.step(char_9600_8n1 + 5);
  REQUIRE(acia_rx_ready(&bench.acia));
  acia_rx_start(&bench.acia, 0xC2, 0, char_9600_8n1 + 5);
  CHECK(acia_next_event(&bench.acia) == char_9600_8n1 + rdrf_9600_8n1);

  bench.step(2 * char_9600_8n1 + 5);
  bench.read(acia_reg::data, 2 * char_9600_8n1 + 300);
  REQUIRE(acia_rx_ready(&bench.acia));
  acia_rx_start(&bench.acia, 0xC3, 0, 2 * char_9600_8n1 + 300);
  CHECK(acia_next_event(&bench.acia) ==
        2 * char_9600_8n1 + 300 + rdrf_9600_8n1);

  bench.read(acia_reg::data, 4 * char_9600_8n1);
  bench.step(8 * char_9600_8n1);
  bench.step(9 * char_9600_8n1);
  REQUIRE(acia_rx_ready(&bench.acia));
  acia_rx_start(&bench.acia, 0xC4, 0, 9 * char_9600_8n1);
  CHECK(acia_next_event(&bench.acia) == 9 * char_9600_8n1 + rdrf_9600_8n1);
}

TEST_CASE(
    "6551: a byte completing into a full RDR sets overrun, leaves the RDR "
    "unchanged and is lost") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_no_irq, 0);
  acia_rx_start(&bench.acia, 0xC1, 0, 0);
  bench.step(char_9600_8n1);
  acia_rx_start(&bench.acia, 0xC2, 0, char_9600_8n1);
  bench.step(2 * char_9600_8n1);
  CHECK(bench.read(acia_reg::status, 2 * char_9600_8n1) == 0x1C);
  CHECK(bench.read(acia_reg::data, 2 * char_9600_8n1 + 1) == 0xC1);
  CHECK(bench.read(acia_reg::status, 2 * char_9600_8n1 + 2) == 0x14);
  CHECK(bench.read(acia_reg::data, 2 * char_9600_8n1 + 3) == 0xC1);
}

TEST_CASE(
    "6551: framing and parity errors come with the byte and clear after a "
    "data read and the next clean byte; an empty read returns the last byte") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_no_irq, 0);
  acia_rx_start(&bench.acia, 0xC1,
                acia_status::framing_error | acia_status::parity_error, 0);
  bench.step(char_9600_8n1);
  CHECK(bench.read(acia_reg::status, char_9600_8n1) == 0x1B);
  CHECK(bench.read(acia_reg::data, char_9600_8n1 + 1) == 0xC1);
  CHECK(bench.read(acia_reg::status, char_9600_8n1 + 2) == 0x13);
  CHECK(bench.read(acia_reg::data, char_9600_8n1 + 3) == 0xC1);

  acia_rx_start(&bench.acia, 0xC2, 0, char_9600_8n1 + 3);
  bench.step(2 * char_9600_8n1 + 3);
  CHECK(bench.read(acia_reg::status, 2 * char_9600_8n1 + 3) == 0x18);
  CHECK(bench.read(acia_reg::data, 2 * char_9600_8n1 + 4) == 0xC2);
}

TEST_CASE(
    "6551: two stop bits except with 8 data bits and parity, 1.5 with 5 data "
    "bits and no parity, and the line view says so") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_no_irq, 0);
  CHECK(acia_frame_sixteenths(&bench.acia) == 160);
  CHECK(acia_rdrf_sixteenths(&bench.acia) == 153);
  CHECK(bench.line().stop_half_bits == 2);
  CHECK(bench.line().data_bits == 8);
  CHECK(bench.line().parity == 0);
  CHECK(bench.line().baud == 9600);
  CHECK(bench.line().dtr == 1);
  CHECK(bench.line().rts == 1);

  bench.program(0x9E, command_no_irq, 0);
  CHECK(acia_frame_sixteenths(&bench.acia) == 176);
  CHECK(bench.line().stop_half_bits == 4);

  bench.program(0x9E, 0x6B, 0);
  CHECK(acia_frame_sixteenths(&bench.acia) == 176);
  CHECK(bench.line().stop_half_bits == 2);
  CHECK(bench.line().parity == 2);

  bench.program(control_9600_5n15, command_no_irq, 0);
  CHECK(acia_frame_sixteenths(&bench.acia) == 120);
  CHECK(bench.line().stop_half_bits == 3);
  CHECK(bench.line().data_bits == 5);

  bench.program(0xBE, 0x2B, 0);
  CHECK(acia_frame_sixteenths(&bench.acia) == 176);
  CHECK(bench.line().stop_half_bits == 4);
  CHECK(bench.line().data_bits == 7);
  CHECK(bench.line().parity == 1);

  bench.program(0x00, 0x00, 0);
  CHECK(bench.line().dtr == 0);
  CHECK(bench.line().rts == 0);
  CHECK(bench.line().baud == 0);
}

TEST_CASE(
    "6551: baud code 0 or an external receiver clock moves nothing and leaves "
    "TDRE as it was") {
  Bench_t bench;
  bench.program(0x10, command_no_irq, 0);
  CHECK(bench.tdre(0));
  bench.write(acia_reg::data, 0xC8, 10);
  CHECK(bench.sent.empty());
  CHECK_FALSE(bench.tdre(500000));
  CHECK_FALSE(acia_rx_ready(&bench.acia));
  CHECK(acia_next_event(&bench.acia) == 0);

  bench.write(acia_reg::control, 0x0E, 500000);
  CHECK(bench.sent.empty());
  CHECK_FALSE(bench.tdre(600000));
  CHECK(bench.line().baud == 0);

  bench.write(acia_reg::control, control_9600_8n1, 700000);
  REQUIRE(bench.sent.size() == 1);
  CHECK(bench.sent.at(0).at == 700000);
  CHECK(bench.tdre(700000));
}

TEST_CASE(
    "6551: a change on DSR or DCD raises IRQ only with DTR asserted, and a "
    "command write that disables a source leaves a set latch alone") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_rx_irq, 0);
  acia_set_lines(&bench.acia, acia_line::cts | acia_line::dcd, 10);
  CHECK(acia_irq(&bench.acia));
  CHECK(bench.read(acia_reg::status, 20) == 0xD0);
  acia_set_lines(&bench.acia, acia_line::all_asserted, 30);
  CHECK(acia_irq(&bench.acia));
  bench.read(acia_reg::status, 40);

  acia_rx_start(&bench.acia, 0xC1, 0, 40);
  bench.step(40 + rdrf_9600_8n1);
  CHECK(acia_irq(&bench.acia));
  bench.write(acia_reg::command, command_no_irq, 40 + rdrf_9600_8n1 + 1);
  CHECK(acia_irq(&bench.acia));
  CHECK(bench.read(acia_reg::status, 40 + rdrf_9600_8n1 + 2) == 0x98);
  CHECK_FALSE(acia_irq(&bench.acia));
}

TEST_CASE(
    "6551: a now below synced advances nothing, and a character in flight "
    "keeps the time it had left") {
  Bench_t bench;
  bench.program(control_9600_8n1, command_no_irq, 0);
  bench.write(acia_reg::data, 0xC1, 5000);
  bench.write(acia_reg::data, 0xC2, 5004);
  CHECK_FALSE(bench.tdre(5004));

  CHECK_FALSE(bench.tdre(100));
  CHECK(bench.acia.synced == 100);
  CHECK(bench.sent.size() == 1);
  const uint64_t remaining = char_9600_8n1 - 4;
  CHECK_FALSE(bench.tdre(100 + remaining - 1));
  CHECK(bench.tdre(100 + remaining));
  REQUIRE(bench.sent.size() == 2);
  CHECK(bench.sent.at(1).byte == 0xC2);
}

TEST_CASE("6551: a null chip is inert") {
  uint8_t byte = 0;
  acia_reset(nullptr, 0);
  acia_programmed_reset(nullptr, 0);
  CHECK_FALSE(acia_step(nullptr, 0, &byte));
  CHECK(acia_read(nullptr, acia_reg::status, 0) == 0);
  CHECK_FALSE(acia_write(nullptr, acia_reg::data, 0, 0, &byte));
  CHECK_FALSE(acia_irq(nullptr));
  acia_set_lines(nullptr, 0, 0);
  CHECK_FALSE(acia_rx_ready(nullptr));
  acia_rx_start(nullptr, 0, 0, 0);
  acia_line_view(nullptr, nullptr);
  CHECK(acia_next_event(nullptr) == 0);
  CHECK(acia_cycles_for(nullptr, 160) == 0);
}
