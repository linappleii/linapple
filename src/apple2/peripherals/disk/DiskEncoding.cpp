// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-member-init)
#include "apple2/peripherals/disk/DiskEncoding.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/DiskError.h"

namespace {

constexpr uint8_t prologue_1 = 0xD5;
constexpr uint8_t prologue_2 = 0xAA;
constexpr uint8_t addr_prologue_3 = 0x96;
constexpr uint8_t data_prologue_3 = 0xAD;
constexpr uint8_t epilogue_1 = 0xDE;
constexpr uint8_t epilogue_2 = 0xAA;
constexpr uint8_t epilogue_3 = 0xEB;
constexpr uint8_t sync_byte = 0xFF;

constexpr uint8_t gcr62_offset_init = 0xAC;
constexpr uint8_t gcr62_offset_step_1 = 0x56;
constexpr uint8_t gcr62_offset_step_2 = 0x56;
constexpr uint8_t gcr62_offset_step_3 = 0x53;
constexpr int gcr62_iterations = 86;

constexpr uint8_t bit_0_mask = 0x01;
constexpr uint8_t bit_1_mask = 0x02;
constexpr uint8_t mask_6bit = 0x3F;
constexpr uint8_t mask_lowbits = 0xFC;
constexpr uint8_t decode_offset = 0x80;
constexpr uint8_t decode_high_bit_mask = 0x7F;
constexpr uint8_t high_bit_mask = 0x80;
// No 6-and-2 code decodes to this, every one being a multiple of four.
constexpr uint8_t invalid_nibble = 0xFF;
constexpr uint8_t addr_4and4_mask = 0x55;
constexpr uint8_t gcr_sync_bit_mask = 0xAA;
constexpr uint8_t volume_default = 0xFE;

constexpr int shift_1 = 1;
constexpr int shift_2 = 2;
constexpr int shift_3 = 3;
constexpr int shift_5 = 5;
constexpr int shift_7 = 7;

constexpr int max_gcr_markers_per_track = 48;
constexpr int max_nibblized_sector_size = 384;
constexpr int gap1_size = 48;
constexpr int gap2_size = 6;
constexpr size_t sector_size = 256;
constexpr uint32_t cells_per_nibble = 8;
constexpr uint32_t cells_per_self_sync = 10;

// Beneath Apple DOS: every gap on a formatted track runs up to a prologue.
// So a run of 0xFF that ends at one is the gap that separates two fields,
// and a run that ends anywhere else is data that happens to read as 0xFF.
template <typename Emit>
auto walk_sync_runs(const uint8_t* nibbles, uint32_t count,
                    const uint8_t* sync_mask, Emit emit) -> void {
  uint32_t index = 0;
  while (index < count) {
    if (sync_mask != nullptr) {
      emit(nibbles[index], sync_mask[index] != 0);
      ++index;
      continue;
    }
    if (nibbles[index] != sync_byte) {
      emit(nibbles[index], false);
      ++index;
      continue;
    }
    uint32_t run_end = index;
    while (run_end < count && nibbles[run_end] == sync_byte) {
      ++run_end;
    }
    const bool self_sync = nibbles[run_end % count] == prologue_1;
    for (; index < run_end; ++index) {
      emit(sync_byte, self_sync);
    }
  }
}

const std::array<uint8_t, disk_encoding_encode_table_size> disk_encoding_table =
    {{0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC,
      0xAD, 0xAE, 0xAF, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA,
      0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3, 0xD6,
      0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7,
      0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5,
      0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF}};

const std::array<std::array<uint8_t, sectors_per_track>, interleave_modes_count>
    disk_encoding_sector_interleave_table = {
        {{0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B, 0x04, 0x0C, 0x05,
          0x0D, 0x06, 0x0E, 0x07, 0x0F},
         {0x00, 0x07, 0x0E, 0x06, 0x0D, 0x05, 0x0C, 0x04, 0x0B, 0x03, 0x0A,
          0x02, 0x09, 0x01, 0x08, 0x0F},
         {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00}}};

static const auto decode_table = []() {
  std::array<uint8_t, disk_encoding_decode_table_size> t{};
  t.fill(invalid_nibble);
  for (size_t i = 0; i < disk_encoding_table.size(); ++i) {
    t.at(disk_encoding_table.at(i) - decode_offset) =
        static_cast<uint8_t>(i << shift_2);
  }
  return t;
}();

auto encode_sector_62(uint8_t* work_buffer, int sector_index)
    -> const uint8_t* {
  {
    uint8_t* sector_base = &work_buffer[sector_index * sector_size];
    uint8_t* result_ptr = &work_buffer[disk_encoding_work_buffer_offset];
    int result_index = 0;
    uint8_t offset = gcr62_offset_init;

    for (int i = 0; i < gcr62_iterations; ++i) {
      uint8_t value = 0;
      auto encode_bits = [&](uint8_t b) {
        value = static_cast<uint8_t>((value << shift_2) |
                                     ((b & bit_0_mask) << shift_1) |
                                     ((b & bit_1_mask) >> shift_1));
      };

      encode_bits(sector_base[offset]);
      offset -= gcr62_offset_step_1;
      encode_bits(sector_base[offset]);
      offset -= gcr62_offset_step_2;
      encode_bits(sector_base[offset]);
      offset -= gcr62_offset_step_3;

      result_ptr[result_index++] = static_cast<uint8_t>(value << shift_2);
    }

    if (result_index >= 2) {
      result_ptr[result_index - 2] &= mask_6bit;
      result_ptr[result_index - 1] &= mask_6bit;
    }

    std::copy_n(sector_base, sector_size, &result_ptr[result_index]);
  }

  {
    uint8_t saved_value = 0;
    uint8_t* source_ptr = &work_buffer[disk_encoding_work_buffer_offset];
    uint8_t* result_ptr = &work_buffer[disk_encoding_checksum_buffer_offset];
    for (int i = 0; i < static_cast<int>(disk_encoding_sector_data_size); ++i) {
      result_ptr[i] = saved_value ^ source_ptr[i];
      saved_value = source_ptr[i];
    }
    result_ptr[disk_encoding_sector_data_size] = saved_value;
  }

  {
    uint8_t* source_ptr = &work_buffer[disk_encoding_checksum_buffer_offset];
    uint8_t* result_ptr = &work_buffer[disk_encoding_work_buffer_offset];
    for (int i = 0;
         i < static_cast<int>(disk_encoding_sector_with_checksum_size); ++i) {
      result_ptr[i] = disk_encoding_table.at(source_ptr[i] >> shift_2);
    }
  }

  return &work_buffer[disk_encoding_work_buffer_offset];
}

// Returns false for a data field the drive could not have written: a byte
// outside the 6-and-2 alphabet, or a checksum that does not match the
// XOR chain the encoder laid down. Nothing reaches image_ptr unless both
// hold, so a bad read leaves the caller's sector as it found it.
auto decode_sector_62(uint8_t* work_buffer, uint8_t* image_ptr) -> bool {
  {
    const uint8_t* source_ptr = &work_buffer[disk_encoding_work_buffer_offset];
    uint8_t* result_ptr = &work_buffer[disk_encoding_checksum_buffer_offset];
    for (int i = 0;
         i < static_cast<int>(disk_encoding_sector_with_checksum_size); ++i) {
      const uint8_t nibble = source_ptr[i];
      if ((nibble & high_bit_mask) == 0) {
        return false;
      }
      const uint8_t value = decode_table.at(nibble & decode_high_bit_mask);
      if (value == invalid_nibble) {
        return false;
      }
      result_ptr[i] = value;
    }
  }

  uint8_t saved_value = 0;
  {
    const uint8_t* source_ptr =
        &work_buffer[disk_encoding_checksum_buffer_offset];
    uint8_t* result_ptr = &work_buffer[disk_encoding_work_buffer_offset];
    for (int i = 0; i < static_cast<int>(disk_encoding_sector_data_size); ++i) {
      result_ptr[i] = saved_value ^ source_ptr[i];
      saved_value = result_ptr[i];
    }
  }

  if (work_buffer[disk_encoding_checksum_buffer_offset +
                  disk_encoding_sector_data_size] != saved_value) {
    return false;
  }

  {
    const uint8_t* low_bits_ptr =
        &work_buffer[disk_encoding_work_buffer_offset];
    const uint8_t* sector_base =
        &work_buffer[disk_encoding_work_buffer_offset + gcr62_offset_step_1];
    uint8_t offset = gcr62_offset_init;
    for (int i = 0; i < gcr62_iterations; ++i) {
      if (offset >= gcr62_offset_init) {
        image_ptr[offset] =
            static_cast<uint8_t>((sector_base[offset] & mask_lowbits) |
                                 ((low_bits_ptr[0] & 0x80) >> shift_7) |
                                 ((low_bits_ptr[0] & 0x40) >> shift_5));
      }
      offset -= gcr62_offset_step_1;
      image_ptr[offset] =
          static_cast<uint8_t>((sector_base[offset] & mask_lowbits) |
                               ((low_bits_ptr[0] & 0x20) >> shift_5) |
                               ((low_bits_ptr[0] & 0x10) >> shift_3));
      offset -= gcr62_offset_step_2;
      image_ptr[offset] =
          static_cast<uint8_t>((sector_base[offset] & mask_lowbits) |
                               ((low_bits_ptr[0] & 0x08) >> shift_3) |
                               ((low_bits_ptr[0] & 0x04) >> shift_1));
      offset -= gcr62_offset_step_3;
      low_bits_ptr++;
    }
  }

  return true;
}

}  // namespace

auto disk_encoding_denibblize_track(uint8_t* work_buffer, uint8_t* track_image,
                                    bool is_dos_order, int nibbles)
    -> DiskError_e {
  std::fill_n(work_buffer, disk_encoding_work_buffer_offset, 0);

  int current_offset = 0;
  int markers_found = 0;
  int current_sector = -1;
  uint16_t decoded_sectors_mask = 0;
  bool field_refused = false;

  auto fetch_byte = [&]() -> uint8_t {
    uint8_t byte = track_image[current_offset++];
    if (current_offset >= nibbles) {
      current_offset = 0;
    }
    return byte;
  };

  auto find_next_marker = [&]() -> bool {
    for (int i = 0; i < nibbles; ++i) {
      if (fetch_byte() == prologue_1 &&
          track_image[current_offset] == prologue_2) {
        fetch_byte();
        return true;
      }
    }
    return false;
  };

  constexpr uint16_t all_sectors_mask = 0xFFFF;
  while (decoded_sectors_mask != all_sectors_mask &&
         markers_found < max_gcr_markers_per_track && find_next_marker()) {
    const uint8_t marker_type = fetch_byte();
    markers_found++;

    switch (marker_type) {
      case addr_prologue_3: {
        for (int i = 0; i < 4; ++i) {
          fetch_byte();
        }
        const uint8_t sector_high = fetch_byte();
        const uint8_t sector_low = fetch_byte();
        current_sector =
            static_cast<int>(((sector_high & addr_4and4_mask) << shift_1) |
                             (sector_low & addr_4and4_mask));
        break;
      }

      case data_prologue_3:
        for (int i = 0;
             i < static_cast<int>(disk_encoding_sector_with_checksum_size);
             ++i) {
          work_buffer[disk_encoding_work_buffer_offset + i] = fetch_byte();
        }
        if (current_sector >= 0 && current_sector < sectors_per_track) {
          const size_t interleave_idx = is_dos_order ? 1 : 0;
          const uint8_t physical_sector =
              disk_encoding_sector_interleave_table.at(interleave_idx)
                  .at(static_cast<size_t>(current_sector));
          if (!decode_sector_62(work_buffer,
                                &work_buffer[physical_sector * sector_size])) {
            field_refused = true;
          } else {
            decoded_sectors_mask |= static_cast<uint16_t>(1 << physical_sector);
          }
        }
        current_sector = -1;
        break;

      default:
        break;
    }
  }

  return field_refused ? disk_err_corrupt : disk_err_none;
}

auto disk_encoding_nibblize_track_custom_order(uint8_t* work_buffer,
                                               uint8_t* track_image_buffer,
                                               uint8_t* sync_mask_buffer,
                                               const uint8_t* sector_order,
                                               int track) -> uint32_t {
  uint32_t current_offset = 0;

  std::fill_n(track_image_buffer, nibbles_per_track, sync_byte);
  if (sync_mask_buffer != nullptr) {
    std::fill_n(sync_mask_buffer, nibbles_per_track, static_cast<uint8_t>(1));
  }

  // Every byte a field is made of is data; what stays sync is whatever the
  // gaps left behind.
  auto put_data = [&](uint8_t value) {
    track_image_buffer[current_offset] = value;
    if (sync_mask_buffer != nullptr) {
      sync_mask_buffer[current_offset] = 0;
    }
    ++current_offset;
  };

  auto encode_4and4_high = [](uint8_t a) -> uint8_t {
    return static_cast<uint8_t>((((a) >> shift_1) & addr_4and4_mask) |
                                gcr_sync_bit_mask);
  };

  auto encode_4and4_low = [](uint8_t a) -> uint8_t {
    return static_cast<uint8_t>(((a)&addr_4and4_mask) | gcr_sync_bit_mask);
  };

  for (int sector_idx = 0; sector_idx < sectors_per_track; ++sector_idx) {
    put_data(prologue_1);
    put_data(prologue_2);
    put_data(addr_prologue_3);

    put_data(encode_4and4_high(volume_default));
    put_data(encode_4and4_low(volume_default));
    put_data(encode_4and4_high(static_cast<uint8_t>(track)));
    put_data(encode_4and4_low(static_cast<uint8_t>(track)));
    put_data(encode_4and4_high(static_cast<uint8_t>(sector_idx)));
    put_data(encode_4and4_low(static_cast<uint8_t>(sector_idx)));

    const uint8_t checksum =
        static_cast<uint8_t>(volume_default ^ static_cast<uint8_t>(track) ^
                             static_cast<uint8_t>(sector_idx));
    put_data(encode_4and4_high(checksum));
    put_data(encode_4and4_low(checksum));

    put_data(epilogue_1);
    put_data(epilogue_2);
    put_data(epilogue_3);

    std::fill_n(&track_image_buffer[current_offset], gap2_size, sync_byte);
    current_offset += static_cast<uint32_t>(gap2_size);

    put_data(prologue_1);
    put_data(prologue_2);
    put_data(data_prologue_3);

    const uint8_t* const encoded = encode_sector_62(
        work_buffer, static_cast<int>(sector_order[sector_idx]));
    for (int i = 0;
         i < static_cast<int>(disk_encoding_sector_with_checksum_size); ++i) {
      put_data(encoded[i]);
    }

    put_data(epilogue_1);
    put_data(epilogue_2);
    put_data(epilogue_3);

    std::fill_n(&track_image_buffer[current_offset], disk_encoding_gap3_size,
                sync_byte);
    current_offset += static_cast<uint32_t>(disk_encoding_gap3_size);
  }

  return current_offset;
}

auto disk_encoding_nibblize_track(uint8_t* work_buffer,
                                  uint8_t* track_image_buffer,
                                  uint8_t* sync_mask_buffer, bool is_dos_order,
                                  int track) -> uint32_t {
  return disk_encoding_nibblize_track_custom_order(
      work_buffer, track_image_buffer, sync_mask_buffer,
      disk_encoding_sector_interleave_table.at(is_dos_order ? 1 : 0).data(),
      track);
}

auto disk_encoding_nibbles_to_bits(const uint8_t* nibbles, uint32_t count,
                                   const uint8_t* sync_mask, uint8_t* bits,
                                   uint32_t max_bits, uint32_t* out_bit_count)
    -> DiskError_e {
  if (nibbles == nullptr || bits == nullptr || out_bit_count == nullptr) {
    return disk_err_invalid_argument;
  }
  *out_bit_count = 0;
  if (count > max_bits / cells_per_nibble) {
    return disk_err_unsupported;
  }

  uint32_t total_bits = 0;
  walk_sync_runs(nibbles, count, sync_mask, [&](uint8_t, bool self_sync) {
    total_bits += self_sync ? cells_per_self_sync : cells_per_nibble;
  });
  if (total_bits > max_bits) {
    return disk_err_unsupported;
  }

  std::fill_n(bits, (total_bits + 7U) / 8U, static_cast<uint8_t>(0));

  uint32_t cell = 0;
  walk_sync_runs(nibbles, count, sync_mask, [&](uint8_t value, bool self_sync) {
    for (uint32_t mask = 0x80U; mask != 0U; mask >>= 1U) {
      if ((value & mask) != 0U) {
        bits[cell >> 3U] |= static_cast<uint8_t>(0x80U >> (cell & 7U));
      }
      ++cell;
    }
    // A self-sync byte is its eight cells followed by two blank ones, which
    // is what lets the shift register fall back into step behind it.
    if (self_sync) {
      cell += cells_per_self_sync - cells_per_nibble;
    }
  });

  *out_bit_count = total_bits;
  return disk_err_none;
}

auto disk_encoding_bits_to_nibbles(const uint8_t* bits, uint32_t bit_count,
                                   uint8_t* nibbles, uint32_t max_nibbles,
                                   uint32_t* out_count) -> DiskError_e {
  if (bits == nullptr || nibbles == nullptr || out_count == nullptr) {
    return disk_err_invalid_argument;
  }
  *out_count = 0;

  auto cell_at = [bits](uint32_t index) -> uint32_t {
    return (bits[index >> 3U] >> (7U - (index & 7U))) & 1U;
  };

  uint32_t cell = 0;
  uint32_t written = 0;
  while (written < max_nibbles) {
    while (cell < bit_count && cell_at(cell) == 0) {
      ++cell;
    }
    if (bit_count < cells_per_nibble || cell > bit_count - cells_per_nibble) {
      break;
    }
    uint8_t value = 0;
    for (uint32_t i = 0; i < cells_per_nibble; ++i) {
      value = static_cast<uint8_t>((value << 1U) | cell_at(cell++));
    }
    nibbles[written++] = value;
  }

  *out_count = written;
  return disk_err_none;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-member-init)
