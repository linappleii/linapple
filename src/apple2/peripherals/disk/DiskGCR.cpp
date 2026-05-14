// NOLINTBEGIN(bugprone-easily-swappable-parameters, modernize-use-trailing-return-type, cppcoreguidelines-owning-memory, cppcoreguidelines-avoid-non-const-global-variables, cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index, bugprone-branch-clone, google-readability-braces-around-statements, cppcoreguidelines-no-malloc, cppcoreguidelines-pro-type-const-cast, google-readability-todo, cppcoreguidelines-pro-type-reinterpret-cast, bugprone-narrowing-conversions, cppcoreguidelines-narrowing-conversions, bugprone-switch-missing-default-case, cppcoreguidelines-use-default-member-init, modernize-use-default-member-init, cppcoreguidelines-use-enum-class, modernize-use-auto, cppcoreguidelines-pro-type-member-init, modernize-loop-convert, cppcoreguidelines-macro-usage)
#include "apple2/peripherals/disk/DiskGCR.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "apple2/Memory.h"
#include "apple2/peripherals/disk/DiskCommands.h"

// GCR constants
static constexpr uint8_t GCR_PROLOGUE_1 = 0xD5;
static constexpr uint8_t GCR_PROLOGUE_2 = 0xAA;
static constexpr uint8_t GCR_ADDR_PROLOGUE_3 = 0x96;
static constexpr uint8_t GCR_DATA_PROLOGUE_3 = 0xAD;
static constexpr uint8_t GCR_EPILOGUE_1 = 0xDE;
static constexpr uint8_t GCR_EPILOGUE_2 = 0xAA;
static constexpr uint8_t GCR_EPILOGUE_3 = 0xEB;
static constexpr uint8_t GCR_SYNC_BYTE = 0xFF;

static constexpr uint8_t GCR_62_OFFSET_INIT = 0xAC;
static constexpr uint8_t GCR_62_OFFSET_STEP_1 = 0x56;
static constexpr uint8_t GCR_62_OFFSET_STEP_2 = 0x56;
static constexpr uint8_t GCR_62_OFFSET_STEP_3 = 0x53;
static constexpr int GCR_62_ITERATIONS = 86;

static constexpr uint8_t GCR_MASK_6BIT = 0x3F;
static constexpr uint8_t GCR_MASK_LOWBITS = 0xFC;

static constexpr uint8_t GCR_DECODE_OFFSET = 0x80;
static constexpr uint8_t GCR_DECODE_HIGH_BIT_MASK = 0x7F;

static constexpr uint8_t GCR_VOLUME_DEFAULT = 0xFE;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
static uint8_t diskbyte[GCR_ENCODE_TABLE_SIZE] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC,
    0xAD, 0xAE, 0xAF, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA,
    0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3, 0xD6,
    0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7,
    0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5,
    0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};

static uint8_t sectornumber[NUM_INTERLEAVE_MODES][SECTORS_PER_TRACK_16] = {
    {0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B, 0x04, 0x0C, 0x05, 0x0D,
     0x06, 0x0E, 0x07, 0x0F},
    {0x00, 0x07, 0x0E, 0x06, 0x0D, 0x05, 0x0C, 0x04, 0x0B, 0x03, 0x0A, 0x02,
     0x09, 0x01, 0x08, 0x0F},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00}};
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

// Nibblization functions
static auto Code62(uint8_t* workbuf, int sector_index) -> uint8_t* {
  // Convert the 256 8-bit bytes into 342 6-bit bytes, which we store
  // Starting at 4k into the work buffer.
  {
    uint8_t* sectorBase = &workbuf[sector_index * PAGE_SIZE];
    uint8_t* resultptr = &workbuf[GCR_WORK_BUFFER_OFFSET];
    int resIdx = 0;
    uint8_t offset = GCR_62_OFFSET_INIT;
    for (int i = 0; i < GCR_62_ITERATIONS; ++i) {
      uint8_t value = 0;
#define ADDVALUE(a) \
  value = (value << 2) | (((a) & 0x01) << 1) | (((a) & 0x02) >> 1)
      ADDVALUE(sectorBase[offset]);
      offset -= GCR_62_OFFSET_STEP_1;
      ADDVALUE(sectorBase[offset]);
      offset -= GCR_62_OFFSET_STEP_2;
      ADDVALUE(sectorBase[offset]);
      offset -= GCR_62_OFFSET_STEP_3;
#undef ADDVALUE
      resultptr[resIdx++] = value << 2;
    }
    if (resIdx >= 2) {
      resultptr[resIdx - 2] &= GCR_MASK_6BIT;
      resultptr[resIdx - 1] &= GCR_MASK_6BIT;
    }
    for (int loop = 0; loop < PAGE_SIZE; ++loop) {
      resultptr[resIdx++] = sectorBase[loop];
    }
  }

  // Exclusive-or the entire data block with itself offset by one byte,
  // Creating a 343rd byte which is used as a checksum. Store the new
  // Block of 343 bytes starting at 5k into the work buffer.
  {
    uint8_t savedval = 0;
    uint8_t* sourceptr = &workbuf[GCR_WORK_BUFFER_OFFSET];
    uint8_t* resultptr = &workbuf[GCR_CHECKSUM_BUFFER_OFFSET];
    for (int loop = 0; loop < GCR_SECTOR_DATA_SIZE; ++loop) {
      resultptr[loop] = savedval ^ sourceptr[loop];
      savedval = sourceptr[loop];
    }
    resultptr[GCR_SECTOR_DATA_SIZE] = savedval;
  }

  // Using a lookup table, convert the 6-bit bytes into disk bytes. A valid
  // disk byte is a byte that has the high bit set, at least two adjacent
  // bits set (excluding the high bit), and at most one pair of consecutive
  // zero bits. The converted block of 343 bytes is stored starting at 4k
  // into the work buffer.
  {
    uint8_t* sourceptr = &workbuf[GCR_CHECKSUM_BUFFER_OFFSET];
    uint8_t* resultptr = &workbuf[GCR_WORK_BUFFER_OFFSET];
    for (int loop = 0; loop < GCR_SECTOR_WITH_CHECKSUM_SIZE; ++loop) {
      resultptr[loop] = diskbyte[sourceptr[loop] >> 2];
    }
  }

  return &workbuf[GCR_WORK_BUFFER_OFFSET];
}

static void Decode62(uint8_t* workbuf, uint8_t* imageptr) {
  // If we haven't already done so, generate a table for converting
  // disk bytes back into 6-bit bytes
  static bool tablegenerated = false;
  static uint8_t sixbitbyte[GCR_DECODE_TABLE_SIZE];
  if (!tablegenerated) {
    memset(sixbitbyte, 0, GCR_DECODE_TABLE_SIZE);
    int loop = 0;
    while (loop < GCR_ENCODE_TABLE_SIZE) {
      sixbitbyte[diskbyte[loop] - GCR_DECODE_OFFSET] = static_cast<uint8_t>(loop << 2);
      loop++;
    }
    tablegenerated = true;
  }

  // Using our table, convert the disk bytes back into 6-bit bytes
  {
    uint8_t* sourceptr = &workbuf[GCR_WORK_BUFFER_OFFSET];
    uint8_t* resultptr = &workbuf[GCR_CHECKSUM_BUFFER_OFFSET];
    for (int loop = 0; loop < GCR_SECTOR_WITH_CHECKSUM_SIZE; ++loop) {
      resultptr[loop] = sixbitbyte[sourceptr[loop] & GCR_DECODE_HIGH_BIT_MASK];
    }
  }

  // Exclusive-or the entire data block with itself offset by one byte
  // to undo the effects of the checksumming process
  {
    uint8_t savedval = 0;
    uint8_t* sourceptr = &workbuf[GCR_CHECKSUM_BUFFER_OFFSET];
    uint8_t* resultptr = &workbuf[GCR_WORK_BUFFER_OFFSET];
    for (int loop = 0; loop < GCR_SECTOR_DATA_SIZE; ++loop) {
      resultptr[loop] = savedval ^ sourceptr[loop];
      savedval = resultptr[loop];
    }
  }

  // Convert the 342 6-bit bytes into 256 8-bit bytes
  {
    uint8_t* lowbitsptr = &workbuf[GCR_WORK_BUFFER_OFFSET];
    uint8_t* sectorBase = &workbuf[GCR_WORK_BUFFER_OFFSET + GCR_62_OFFSET_STEP_1];
    uint8_t offset = GCR_62_OFFSET_INIT;
    for (int i = 0; i < GCR_62_ITERATIONS; ++i) {
      if (offset >= GCR_62_OFFSET_INIT) {
        imageptr[offset] = static_cast<uint8_t>((sectorBase[offset] & GCR_MASK_LOWBITS) |
                           ((lowbitsptr[0] & 0x80) >> 7) |
                           ((lowbitsptr[0] & 0x40) >> 5));
      }
      offset -= GCR_62_OFFSET_STEP_1;
      imageptr[offset] = static_cast<uint8_t>((sectorBase[offset] & GCR_MASK_LOWBITS) |
                         ((lowbitsptr[0] & 0x20) >> 5) |
                         ((lowbitsptr[0] & 0x10) >> 3));
      offset -= GCR_62_OFFSET_STEP_2;
      imageptr[offset] = static_cast<uint8_t>((sectorBase[offset] & GCR_MASK_LOWBITS) |
                         ((lowbitsptr[0] & 0x08) >> 3) |
                         ((lowbitsptr[0] & 0x04) >> 1));
      offset -= GCR_62_OFFSET_STEP_3;
      lowbitsptr++;
    }
  }
}

void GCR_DenibblizeTrack(uint8_t* workbuf, uint8_t* trackimage, bool dosorder,
                         int nibbles) {
  memset(workbuf, 0, GCR_WORK_BUFFER_OFFSET);

  // Search through the track image for each sector. For every sector
  // we find, copy the nibblized data for that sector into the work
  // buffer at offset 4k. Then call decode62() to denibblize the data
  // in the buffer and write it into the first part of the work buffer
  // offset by the sector number.
  {
    int offset = 0;
    static constexpr int GCR_DENIBBLIZE_MAX_PARTS = 33;
    int partsleft = GCR_DENIBBLIZE_MAX_PARTS;
    int sector = 0;
    while (partsleft-- > 0) {
      std::array<uint8_t, 3> byteval = {{0, 0, 0}};
      int bytenum = 0;
      int loop = nibbles;
      while ((loop--) > 0 && (bytenum < 3)) {
        if (bytenum > 0) {
          byteval[static_cast<size_t>(bytenum++)] = trackimage[offset++];
        } else if (trackimage[offset++] == GCR_PROLOGUE_1) {
          bytenum = 1;
        }
        if (offset >= nibbles) {
          offset = 0;
        }
      }
      if ((bytenum == 3) && (byteval[1] == GCR_PROLOGUE_2)) {
        int tempoffset = offset;
        static constexpr int GCR_SCAN_BUFFER_SIZE = 384;
        for (int i = 0; i < GCR_SCAN_BUFFER_SIZE; ++i) {
          workbuf[GCR_WORK_BUFFER_OFFSET + i] = trackimage[tempoffset++];
          if (tempoffset >= nibbles) {
            tempoffset = 0;
          }
        }
        if (byteval[2] == GCR_ADDR_PROLOGUE_3) {
          static constexpr uint8_t GCR_ADDR_4AND4_MASK = 0x55;
          static constexpr int GCR_ADDR_SECTOR_OFFSET = 4;
          sector = ((workbuf[GCR_WORK_BUFFER_OFFSET + GCR_ADDR_SECTOR_OFFSET] & GCR_ADDR_4AND4_MASK) << 1) |
                   (workbuf[GCR_WORK_BUFFER_OFFSET + GCR_ADDR_SECTOR_OFFSET + 1] & GCR_ADDR_4AND4_MASK);
        } else if (byteval[2] == GCR_DATA_PROLOGUE_3) {
          Decode62(
              workbuf,
              &workbuf[static_cast<size_t>(sectornumber[dosorder ? 1 : 0][sector]) * PAGE_SIZE]);
          sector = 0;
        }
      }
    }
  }
}

static constexpr uint8_t Code44A(uint8_t a) {
  return static_cast<uint8_t>((((a) >> 1) & 0x55) | 0xAA);
}

static constexpr uint8_t Code44B(uint8_t a) {
  return static_cast<uint8_t>(((a) & 0x55) | 0xAA);
}

auto GCR_NibblizeTrackCustomOrder(uint8_t* workbuf,
                                      uint8_t* trackImageBuffer,
                                      uint8_t* sector_order, int track) -> uint32_t {
  // Note: we assume workbuf contains the track data in the first 4k
  // (DOS_TRACK_SIZE)
  uint32_t offset = 0;
  uint8_t sector = 0;

  // Write gap one, which contains 48 self-sync bytes
  const int gap1_size = 48;
  for (int loop = 0; loop < gap1_size; loop++) {
    trackImageBuffer[offset++] = GCR_SYNC_BYTE;
  }

  while (sector < SECTORS_PER_TRACK_16) {
    // Write the address field, which contains:
    //   - PROLOGUE (D5AA96)
    //   - VOLUME NUMBER ("4 AND 4" ENCODED)
    //   - TRACK NUMBER ("4 AND 4" ENCODED)
    //   - SECTOR NUMBER ("4 AND 4" ENCODED)
    //   - CHECKSUM ("4 AND 4" ENCODED)
    //   - EPILOGUE (DEAAEB)
    trackImageBuffer[offset++] = GCR_PROLOGUE_1;
    trackImageBuffer[offset++] = GCR_PROLOGUE_2;
    trackImageBuffer[offset++] = GCR_ADDR_PROLOGUE_3;

    trackImageBuffer[offset++] = Code44A(GCR_VOLUME_DEFAULT);
    trackImageBuffer[offset++] = Code44B(GCR_VOLUME_DEFAULT);
    trackImageBuffer[offset++] = Code44A(static_cast<uint8_t>(track));
    trackImageBuffer[offset++] = Code44B(static_cast<uint8_t>(track));
    trackImageBuffer[offset++] = Code44A(sector);
    trackImageBuffer[offset++] = Code44B(sector);
    trackImageBuffer[offset++] = Code44A(static_cast<uint8_t>(GCR_VOLUME_DEFAULT ^ static_cast<uint8_t>(track) ^ sector));
    trackImageBuffer[offset++] = Code44B(static_cast<uint8_t>(GCR_VOLUME_DEFAULT ^ static_cast<uint8_t>(track) ^ sector));

    trackImageBuffer[offset++] = GCR_EPILOGUE_1;
    trackImageBuffer[offset++] = GCR_EPILOGUE_2;
    trackImageBuffer[offset++] = GCR_EPILOGUE_3;

    // Write gap two, which contains six self-sync bytes
    const int gap2_size = 6;
    for (int loop = 0; loop < gap2_size; loop++) {
      trackImageBuffer[offset++] = GCR_SYNC_BYTE;
    }

    // Write the data field, which contains:
    //   - PROLOGUE (D5AAAD)
    //   - 343 6-BIT BYTES OF NIBBLIZED DATA, INCLUDING A 6-BIT CHECKSUM
    //   - EPILOGUE (DEAAEB)
    trackImageBuffer[offset++] = GCR_PROLOGUE_1;
    trackImageBuffer[offset++] = GCR_PROLOGUE_2;
    trackImageBuffer[offset++] = GCR_DATA_PROLOGUE_3;
    memcpy(trackImageBuffer + offset, Code62(workbuf, sector_order[sector]),
           GCR_SECTOR_WITH_CHECKSUM_SIZE);
    offset += GCR_SECTOR_WITH_CHECKSUM_SIZE;
    trackImageBuffer[offset++] = GCR_EPILOGUE_1;
    trackImageBuffer[offset++] = GCR_EPILOGUE_2;
    trackImageBuffer[offset++] = GCR_EPILOGUE_3;

    // Write gap three, which contains gap bytes
    for (int loop = 0; loop < GCR_GAP3_SIZE; loop++) {
      trackImageBuffer[offset++] = GCR_SYNC_BYTE;
    }

    sector++;
  }

  return offset;
}

auto GCR_NibblizeTrack(uint8_t* workbuf, uint8_t* trackImageBuffer,
                           bool dosorder, int track) -> uint32_t {
  return GCR_NibblizeTrackCustomOrder(workbuf, trackImageBuffer,
                                      sectornumber[dosorder ? 1 : 0], track);
}

void GCR_SkewTrack(uint8_t* workbuf, int track, int nibbles,
                   uint8_t* trackImageBuffer) {
  static constexpr int GCR_SKEW_FACTOR = 768;
  int skewbytes = (track * GCR_SKEW_FACTOR) % nibbles;
  memcpy(workbuf, trackImageBuffer, static_cast<size_t>(nibbles));
  memcpy(trackImageBuffer, &workbuf[skewbytes],
         static_cast<size_t>(nibbles - skewbytes));
  memcpy(trackImageBuffer + static_cast<size_t>(nibbles - skewbytes), workbuf,
         static_cast<size_t>(skewbytes));
}

// NOLINTEND(bugprone-easily-swappable-parameters, modernize-use-trailing-return-type, cppcoreguidelines-owning-memory, cppcoreguidelines-avoid-non-const-global-variables, cppcoreguidelines-avoid-magic-numbers, cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays, cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index, bugprone-branch-clone, google-readability-braces-around-statements, cppcoreguidelines-no-malloc, cppcoreguidelines-pro-type-const-cast, google-readability-todo, cppcoreguidelines-pro-type-reinterpret-cast, bugprone-narrowing-conversions, cppcoreguidelines-narrowing-conversions, bugprone-switch-missing-default-case, cppcoreguidelines-use-default-member-init, modernize-use-default-member-init, cppcoreguidelines-use-enum-class, modernize-use-auto, cppcoreguidelines-pro-type-member-init, modernize-loop-convert, cppcoreguidelines-macro-usage)
