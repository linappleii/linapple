// SPDX-License-Identifier: GPL-2.0-only
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//             cppcoreguidelines-pro-bounds-pointer-arithmetic,
//             cppcoreguidelines-pro-type-member-init)
#include "apple2/peripherals/disk/DiskEncoding.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "apple2/Memory.h"
#include "apple2/peripherals/disk/DiskCommands.h"

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
constexpr uint8_t addr_4and4_mask = 0x55;
constexpr uint8_t gcr_sync_bit_mask = 0xAA;
constexpr uint8_t volume_default = 0xFE;

constexpr int shift_1 = 1;
constexpr int shift_2 = 2;
constexpr int shift_3 = 3;
constexpr int shift_5 = 5;
constexpr int shift_7 = 7;

constexpr int max_gcr_markers_per_track = 33;
constexpr int max_nibblized_sector_size = 384;
constexpr int gap1_size = 48;
constexpr int gap2_size = 6;
constexpr int skew_factor = 768;

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
  t.fill(0);
  for (size_t i = 0; i < disk_encoding_table.size(); ++i) {
    t.at(disk_encoding_table.at(i) - decode_offset) =
        static_cast<uint8_t>(i << shift_2);
  }
  return t;
}();

auto encode_sector_62(uint8_t* work_buffer, int sector_index) -> uint8_t* {
  {
    uint8_t* sector_base = &work_buffer[sector_index * PAGE_SIZE];
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

    std::copy_n(sector_base, PAGE_SIZE, &result_ptr[result_index]);
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

auto decode_sector_62(uint8_t* work_buffer, uint8_t* image_ptr) -> void {
  {
    uint8_t* source_ptr = &work_buffer[disk_encoding_work_buffer_offset];
    uint8_t* result_ptr = &work_buffer[disk_encoding_checksum_buffer_offset];
    for (int i = 0;
         i < static_cast<int>(disk_encoding_sector_with_checksum_size); ++i) {
      result_ptr[i] = decode_table.at(source_ptr[i] & decode_high_bit_mask);
    }
  }

  {
    uint8_t saved_value = 0;
    uint8_t* source_ptr = &work_buffer[disk_encoding_checksum_buffer_offset];
    uint8_t* result_ptr = &work_buffer[disk_encoding_work_buffer_offset];
    for (int i = 0; i < static_cast<int>(disk_encoding_sector_data_size); ++i) {
      result_ptr[i] = saved_value ^ source_ptr[i];
      saved_value = result_ptr[i];
    }
  }

  {
    uint8_t* low_bits_ptr = &work_buffer[disk_encoding_work_buffer_offset];
    uint8_t* sector_base =
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
}

}  // namespace

auto disk_encoding_denibblize_track(uint8_t* work_buffer, uint8_t* track_image,
                                    bool is_dos_order, int nibbles) -> void {
  std::fill_n(work_buffer, disk_encoding_work_buffer_offset, 0);

  int current_offset = 0;
  int markers_found = 0;
  int current_sector = 0;

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

  while (markers_found < max_gcr_markers_per_track && find_next_marker()) {
    const uint8_t marker_type = fetch_byte();
    markers_found++;

    switch (marker_type) {
      case addr_prologue_3:
        for (int i = 0; i < 4; ++i) {
          fetch_byte();
        }
        current_sector =
            static_cast<int>(((fetch_byte() & addr_4and4_mask) << shift_1) |
                             (fetch_byte() & addr_4and4_mask));
        break;

      case data_prologue_3:
        for (int i = 0; i < max_nibblized_sector_size; ++i) {
          work_buffer[disk_encoding_work_buffer_offset + i] = fetch_byte();
        }
        if (current_sector >= 0 && current_sector < sectors_per_track) {
          const size_t interleave_idx = is_dos_order ? 1 : 0;
          const uint8_t physical_sector =
              disk_encoding_sector_interleave_table.at(interleave_idx)
                  .at(static_cast<size_t>(current_sector));
          decode_sector_62(work_buffer,
                           &work_buffer[physical_sector * PAGE_SIZE]);
        }
        current_sector = 0;
        break;

      default:
        break;
    }
  }
}

auto disk_encoding_nibblize_track_custom_order(uint8_t* work_buffer,
                                               uint8_t* track_image_buffer,
                                               const uint8_t* sector_order,
                                               int track) -> uint32_t {
  uint32_t current_offset = 0;

  std::fill_n(track_image_buffer, nibbles_per_track, sync_byte);

  auto encode_4and4_high = [](uint8_t a) -> uint8_t {
    return static_cast<uint8_t>((((a) >> shift_1) & addr_4and4_mask) |
                                gcr_sync_bit_mask);
  };

  auto encode_4and4_low = [](uint8_t a) -> uint8_t {
    return static_cast<uint8_t>(((a)&addr_4and4_mask) | gcr_sync_bit_mask);
  };

  for (int sector_idx = 0; sector_idx < sectors_per_track; ++sector_idx) {
    track_image_buffer[current_offset++] = prologue_1;
    track_image_buffer[current_offset++] = prologue_2;
    track_image_buffer[current_offset++] = addr_prologue_3;

    track_image_buffer[current_offset++] = encode_4and4_high(volume_default);
    track_image_buffer[current_offset++] = encode_4and4_low(volume_default);
    track_image_buffer[current_offset++] =
        encode_4and4_high(static_cast<uint8_t>(track));
    track_image_buffer[current_offset++] =
        encode_4and4_low(static_cast<uint8_t>(track));
    track_image_buffer[current_offset++] =
        encode_4and4_high(static_cast<uint8_t>(sector_idx));
    track_image_buffer[current_offset++] =
        encode_4and4_low(static_cast<uint8_t>(sector_idx));

    const uint8_t checksum =
        static_cast<uint8_t>(volume_default ^ static_cast<uint8_t>(track) ^
                             static_cast<uint8_t>(sector_idx));
    track_image_buffer[current_offset++] = encode_4and4_high(checksum);
    track_image_buffer[current_offset++] = encode_4and4_low(checksum);

    track_image_buffer[current_offset++] = epilogue_1;
    track_image_buffer[current_offset++] = epilogue_2;
    track_image_buffer[current_offset++] = epilogue_3;

    std::fill_n(&track_image_buffer[current_offset], gap2_size, sync_byte);
    current_offset += static_cast<uint32_t>(gap2_size);

    track_image_buffer[current_offset++] = prologue_1;
    track_image_buffer[current_offset++] = prologue_2;
    track_image_buffer[current_offset++] = data_prologue_3;

    std::copy_n(encode_sector_62(work_buffer,
                                 static_cast<int>(sector_order[sector_idx])),
                static_cast<size_t>(disk_encoding_sector_with_checksum_size),
                &track_image_buffer[current_offset]);

    current_offset +=
        static_cast<uint32_t>(disk_encoding_sector_with_checksum_size);
    track_image_buffer[current_offset++] = epilogue_1;
    track_image_buffer[current_offset++] = epilogue_2;
    track_image_buffer[current_offset++] = epilogue_3;

    std::fill_n(&track_image_buffer[current_offset], disk_encoding_gap3_size,
                sync_byte);
    current_offset += static_cast<uint32_t>(disk_encoding_gap3_size);
  }

  return current_offset;
}

auto disk_encoding_nibblize_track(uint8_t* work_buffer,
                                  uint8_t* track_image_buffer,
                                  bool is_dos_order, int track) -> uint32_t {
  return disk_encoding_nibblize_track_custom_order(
      work_buffer, track_image_buffer,
      disk_encoding_sector_interleave_table.at(is_dos_order ? 1 : 0).data(),
      track);
}

auto disk_encoding_skew_track(uint8_t* track_image_buffer, uint8_t* work_buffer,
                              int track, int nibbles) -> void {
  int skew_bytes = (track * skew_factor) % nibbles;
  std::copy_n(track_image_buffer, static_cast<size_t>(nibbles), work_buffer);
  std::copy_n(&work_buffer[skew_bytes],
              static_cast<size_t>(nibbles - skew_bytes), track_image_buffer);
  std::copy_n(work_buffer, static_cast<size_t>(skew_bytes),
              &track_image_buffer[nibbles - skew_bytes]);
}
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//           cppcoreguidelines-pro-bounds-pointer-arithmetic,
//           cppcoreguidelines-pro-type-member-init)
