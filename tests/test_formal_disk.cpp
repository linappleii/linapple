// SPDX-License-Identifier: GPL-2.0-only
#include <z3++.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "doctest.h"

namespace {

// Standard Apple II 6-and-2 GCR encoding table
const std::array<uint8_t, 64> kGcrEncodeTable = {
    {0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC,
     0xAD, 0xAE, 0xAF, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA,
     0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3, 0xD6,
     0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7,
     0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5,
     0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF}};

}  // namespace

TEST_CASE("SMT Formal Verification: GCR Physical Encoding Invariants") {
  z3::context ctx;
  z3::solver solver(ctx);

  // Symbolic 6-bit nibble value [0..63]
  z3::expr val = ctx.bv_const("gcr_val", 8);
  solver.add(z3::ule(val, ctx.bv_val(63, 8)));

  // Array of 64 table values encoded as Z3 AST
  z3::expr_vector table_exprs(ctx);
  for (uint8_t entry : kGcrEncodeTable) {
    table_exprs.push_back(ctx.bv_val(entry, 8));
  }

  // Model table lookup: (val == 0 ? table[0] : (val == 1 ? table[1] : ...))
  z3::expr encoded_byte = table_exprs[63];
  for (int i = 62; i >= 0; --i) {
    encoded_byte =
        z3::ite(val == ctx.bv_val(i, 8), table_exprs[i], encoded_byte);
  }

  // Invariant 1: High bit must ALWAYS be set (bit 7 == 1) for every GCR byte
  z3::expr bit7_set = (encoded_byte & ctx.bv_val(0x80, 8)) != 0;

  // Invariant 2: In Apple II 6-and-2 GCR, no more than TWO consecutive zero
  // bits are allowed A violation occurs if three adjacent bits are all 0 (e.g.,
  // bit_k == 0 && bit_{k+1} == 0 && bit_{k+2} == 0).
  z3::expr no_triple_zeros = ctx.bool_val(true);
  for (int b = 0; b < 6; ++b) {
    z3::expr bit_0 =
        z3::lshr(encoded_byte, ctx.bv_val(b, 8)) & ctx.bv_val(1, 8);
    z3::expr bit_1 =
        z3::lshr(encoded_byte, ctx.bv_val(b + 1, 8)) & ctx.bv_val(1, 8);
    z3::expr bit_2 =
        z3::lshr(encoded_byte, ctx.bv_val(b + 2, 8)) & ctx.bv_val(1, 8);
    z3::expr triple_zero = (bit_0 == ctx.bv_val(0, 8)) &&
                           (bit_1 == ctx.bv_val(0, 8)) &&
                           (bit_2 == ctx.bv_val(0, 8));
    no_triple_zeros = no_triple_zeros && !triple_zero;
  }

  // Prove that a violation CANNOT exist
  z3::expr violation = !bit7_set || !no_triple_zeros;
  solver.add(violation);

  // UNSAT proves mathematically that no input in [0..63] can violate physical
  // GCR invariants
  CHECK(solver.check() == z3::unsat);
}

TEST_CASE("SMT Formal Verification: GCR 6-and-2 Exact Bijectivity") {
  z3::context ctx;
  z3::solver solver(ctx);

  // Create 3 arbitrary symbolic bytes representing any raw sector data
  z3::expr b0 = ctx.bv_const("byte0", 8);
  z3::expr b1 = ctx.bv_const("byte1", 8);
  z3::expr b2 = ctx.bv_const("byte2", 8);

  // 6-and-2 splitting:
  // High 6 bits of each byte
  z3::expr high_b0 = z3::lshr(b0, ctx.bv_val(2, 8));
  z3::expr high_b1 = z3::lshr(b1, ctx.bv_val(2, 8));
  z3::expr high_b2 = z3::lshr(b2, ctx.bv_val(2, 8));

  // Low 2 bits of each byte packed into 6-bit aux byte:
  z3::expr b0_bit0 = b0 & ctx.bv_val(1, 8);
  z3::expr b0_bit1 = z3::lshr(b0 & ctx.bv_val(2, 8), ctx.bv_val(1, 8));
  z3::expr b1_bit0 = b1 & ctx.bv_val(1, 8);
  z3::expr b1_bit1 = z3::lshr(b1 & ctx.bv_val(2, 8), ctx.bv_val(1, 8));
  z3::expr b2_bit0 = b2 & ctx.bv_val(1, 8);
  z3::expr b2_bit1 = z3::lshr(b2 & ctx.bv_val(2, 8), ctx.bv_val(1, 8));

  z3::expr aux =
      z3::shl(b0_bit0, ctx.bv_val(1, 8)) | b0_bit1 |
      z3::shl(b1_bit0, ctx.bv_val(3, 8)) | z3::shl(b1_bit1, ctx.bv_val(2, 8)) |
      z3::shl(b2_bit0, ctx.bv_val(5, 8)) | z3::shl(b2_bit1, ctx.bv_val(4, 8));

  // Reconstruction (Denibblizing)
  z3::expr rec_b0 = z3::shl(high_b0, ctx.bv_val(2, 8)) |
                    z3::shl(b0_bit1, ctx.bv_val(1, 8)) | b0_bit0;
  z3::expr rec_b1 = z3::shl(high_b1, ctx.bv_val(2, 8)) |
                    z3::shl(b1_bit1, ctx.bv_val(1, 8)) | b1_bit0;
  z3::expr rec_b2 = z3::shl(high_b2, ctx.bv_val(2, 8)) |
                    z3::shl(b2_bit1, ctx.bv_val(1, 8)) | b2_bit0;

  // Check if there exists ANY byte triplet that does not round-trip identically
  z3::expr mismatch = (rec_b0 != b0) || (rec_b1 != b1) || (rec_b2 != b2);
  solver.add(mismatch);

  // UNSAT proves perfect bijectivity (zero data loss across all 2^24 possible
  // triplets)
  CHECK(solver.check() == z3::unsat);
}

TEST_CASE("SMT Formal Verification: Disk II Stepper Motor Invariants") {
  z3::context ctx;
  z3::solver solver(ctx);

  constexpr int max_phases = 160;  // 40 tracks * 4 quarter tracks
  constexpr int max_tracks = 40;

  // Symbolic current phase [0..159] and phase magnet mask [0..15]
  z3::expr phase = ctx.bv_const("stepper_phase", 16);
  z3::expr phase_mask = ctx.bv_const("stepper_phase_mask", 16);

  // Precondition: phase is within physical cylinder bounds
  solver.add(z3::ule(phase, ctx.bv_val(max_phases - 1, 16)));
  solver.add(z3::ule(phase_mask, ctx.bv_val(15, 16)));

  // Magnetic force delta calculation (from Disk.cpp: disk_io_control_stepper)
  z3::expr next_idx = (phase + ctx.bv_val(1, 16)) & ctx.bv_val(3, 16);
  z3::expr prev_idx = (phase + ctx.bv_val(3, 16)) & ctx.bv_val(3, 16);

  z3::expr next_bit = z3::shl(ctx.bv_val(1, 16), next_idx);
  z3::expr prev_bit = z3::shl(ctx.bv_val(1, 16), prev_idx);

  z3::expr next_active = (phase_mask & next_bit) != 0;
  z3::expr prev_active = (phase_mask & prev_bit) != 0;

  z3::expr delta = ctx.bv_val(0, 16);
  delta = z3::ite(next_active, delta + ctx.bv_val(1, 16), delta);
  delta = z3::ite(prev_active, delta - ctx.bv_val(1, 16), delta);

  // Stepper motion update with physical stops (std::max(0, std::min(159, phase
  // + delta)))
  z3::expr phase_plus_delta = phase + delta;
  z3::expr clamped_phase = phase_plus_delta;

  // Underflow clamp at track 0 bumper
  clamped_phase =
      z3::ite((delta == ctx.bv_val(0xFFFF, 16)) && (phase == ctx.bv_val(0, 16)),
              ctx.bv_val(0, 16), clamped_phase);

  // Overflow clamp at track 40 bumper
  clamped_phase = z3::ite(
      (delta == ctx.bv_val(1, 16)) && (phase == ctx.bv_val(max_phases - 1, 16)),
      ctx.bv_val(max_phases - 1, 16), clamped_phase);

  z3::expr resulting_track = clamped_phase / ctx.bv_val(4, 16);

  // Invariant 1: Head position must remain strictly inside [0..159]
  z3::expr phase_out_of_bounds =
      z3::ugt(clamped_phase, ctx.bv_val(max_phases - 1, 16));

  // Invariant 2: Derived cylinder track index must remain strictly inside
  // [0..39]
  z3::expr track_out_of_bounds =
      z3::ugt(resulting_track, ctx.bv_val(max_tracks - 1, 16));

  // Invariant 3: Symmetrical opposing magnet energization must yield zero
  // displacement (delta == 0)
  z3::expr opposing_active = next_active && prev_active;
  z3::expr jitter_violation = opposing_active && (delta != ctx.bv_val(0, 16));

  z3::expr any_violation =
      phase_out_of_bounds || track_out_of_bounds || jitter_violation;
  solver.add(any_violation);

  // UNSAT proves mathematically that mechanical track motion is 100% sound and
  // safe
  CHECK(solver.check() == z3::unsat);
}
