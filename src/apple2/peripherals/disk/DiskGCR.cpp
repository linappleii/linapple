#include "apple2/peripherals/disk/DiskGCR.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/Memory.h"
#include <cstring>
#include <algorithm>

static uint8_t diskbyte[GCR_ENCODE_TABLE_SIZE] = {0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2,
                              0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE,
                              0xCF, 0xD3, 0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9,
                              0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB,
                              0xFC, 0xFD, 0xFE, 0xFF};

static uint8_t sectornumber[NUM_INTERLEAVE_MODES][SECTORS_PER_TRACK_16] = {{0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B, 0x04, 0x0C, 0x05, 0x0D, 0x06, 0x0E, 0x07, 0x0F},
                                     {0x00, 0x07, 0x0E, 0x06, 0x0D, 0x05, 0x0C, 0x04, 0x0B, 0x03, 0x0A, 0x02, 0x09, 0x01, 0x08, 0x0F},
                                     {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

// Nibblization functions
static auto Code62(uint8_t* workbuf, int sector_index) -> uint8_t*
{
  // Convert the 256 8-bit bytes into 342 6-bit bytes, which we store
  // Starting at 4k into the work buffer.
  {
    uint8_t* sectorBase = &workbuf[sector_index * PAGE_SIZE];
    uint8_t* resultptr = &workbuf[GCR_WORK_BUFFER_OFFSET];
    int resIdx = 0;
    uint8_t offset = 0xAC;
    while (offset != 0x02) {
      uint8_t value = 0;
      #define ADDVALUE(a) value = (value << 2) |        \
                            (((a) & 0x01) << 1) | \
                            (((a) & 0x02) >> 1)
      ADDVALUE(sectorBase[offset]);
      offset -= 0x56;
      ADDVALUE(sectorBase[offset]);
      offset -= 0x56;
      ADDVALUE(sectorBase[offset]);
      offset -= 0x53;
      #undef ADDVALUE
      resultptr[resIdx++] = value << 2;
    }
    if (resIdx >= 2) {
      resultptr[resIdx - 2] &= 0x3F;
      resultptr[resIdx - 1] &= 0x3F;
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

static void Decode62(uint8_t* workbuf, uint8_t* imageptr)
{
  // If we haven't already done so, generate a table for converting
  // disk bytes back into 6-bit bytes
  static bool tablegenerated = false;
  static uint8_t sixbitbyte[GCR_DECODE_TABLE_SIZE];
  if (!tablegenerated) {
    memset(sixbitbyte, 0, GCR_DECODE_TABLE_SIZE);
    int loop = 0;
    while (loop < GCR_ENCODE_TABLE_SIZE) {
      sixbitbyte[diskbyte[loop] - 0x80] = loop << 2;
      loop++;
    }
    tablegenerated = true;
  }

  // Using our table, convert the disk bytes back into 6-bit bytes
  {
    uint8_t* sourceptr = &workbuf[GCR_WORK_BUFFER_OFFSET];
    uint8_t* resultptr = &workbuf[GCR_CHECKSUM_BUFFER_OFFSET];
    for (int loop = 0; loop < GCR_SECTOR_WITH_CHECKSUM_SIZE; ++loop) {
      resultptr[loop] = sixbitbyte[sourceptr[loop] & 0x7F];
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
    uint8_t* sectorBase = &workbuf[GCR_WORK_BUFFER_OFFSET + 0x56];
    uint8_t offset = 0xAC;
    while (offset != 0x02) {
      if (offset >= 0xAC) {
        imageptr[offset] =
          (sectorBase[offset] & 0xFC) | ((lowbitsptr[0] & 0x80) >> 7) | ((lowbitsptr[0] & 0x40) >> 5);
      }
      offset -= 0x56;
      imageptr[offset] =
        (sectorBase[offset] & 0xFC) | ((lowbitsptr[0] & 0x20) >> 5) | ((lowbitsptr[0] & 0x10) >> 3);
      offset -= 0x56;
      imageptr[offset] =
        (sectorBase[offset] & 0xFC) | ((lowbitsptr[0] & 0x08) >> 3) | ((lowbitsptr[0] & 0x04) >> 1);
      offset -= 0x53;
      lowbitsptr++;
    }
  }
}

void GCR_DenibblizeTrack(uint8_t* workbuf, uint8_t* trackimage, bool dosorder, int nibbles)
{
  memset(workbuf, 0, GCR_WORK_BUFFER_OFFSET);

  // Search through the track image for each sector. For every sector
  // we find, copy the nibblized data for that sector into the work
  // buffer at offset 4k. Then call decode62() to denibblize the data
  // in the buffer and write it into the first part of the work buffer
  // offset by the sector number.
  {
    int offset = 0;
    int partsleft = 33;
    int sector = 0;
    while (partsleft--) {
      uint8_t byteval[3] = {0, 0, 0};
      int bytenum = 0;
      int loop = nibbles;
      while ((loop--) && (bytenum < 3)) {
        if (bytenum) {
          byteval[bytenum++] = trackimage[offset++];
        } else if (trackimage[offset++] == 0xD5) {
          bytenum = 1;
        }
        if (offset >= nibbles) {
          offset = 0;
        }
      }
      if ((bytenum == 3) && (byteval[1] == 0xAA)) {
        int tempoffset = offset;
        const int SCAN_BUFFER_SIZE = 384;
        for (int i = 0; i < SCAN_BUFFER_SIZE; ++i) {
          workbuf[GCR_WORK_BUFFER_OFFSET + i] = trackimage[tempoffset++];
          if (tempoffset >= nibbles) {
            tempoffset = 0;
          }
        }
        if (byteval[2] == 0x96) {
          sector = ((workbuf[GCR_WORK_BUFFER_OFFSET + 4] & 0x55) << 1) | (workbuf[GCR_WORK_BUFFER_OFFSET + 5] & 0x55);
        } else if (byteval[2] == 0xAD) {
          Decode62(workbuf, &workbuf[sectornumber[dosorder ? 1 : 0][sector] * PAGE_SIZE]);
          sector = 0;
        }
      }
    }
  }
}

uint32_t GCR_NibblizeTrackCustomOrder(uint8_t* workbuf, uint8_t* trackImageBuffer, uint8_t* sector_order, int track)
{
  // Note: we assume workbuf contains the track data in the first 4k (DOS_TRACK_SIZE)
  uint32_t offset = 0;
  uint8_t sector = 0;

  // Write gap one, which contains 48 self-sync bytes
  const int gap1_size = 48;
  for (int loop = 0; loop < gap1_size; loop++) {
    trackImageBuffer[offset++] = 0xFF;
  }

  while (sector < SECTORS_PER_TRACK_16) {

    // Write the address field, which contains:
    //   - PROLOGUE (D5AA96)
    //   - VOLUME NUMBER ("4 AND 4" ENCODED)
    //   - TRACK NUMBER ("4 AND 4" ENCODED)
    //   - SECTOR NUMBER ("4 AND 4" ENCODED)
    //   - CHECKSUM ("4 AND 4" ENCODED)
    //   - EPILOGUE (DEAAEB)
    trackImageBuffer[offset++] = 0xD5;
    trackImageBuffer[offset++] = 0xAA;
    trackImageBuffer[offset++] = 0x96;
    #define VOLUME 0xFE
    #define CODE44A(a) ((((a) >> 1) & 0x55) | 0xAA)
    #define CODE44B(a) (((a) & 0x55) | 0xAA)
    trackImageBuffer[offset++] = CODE44A(VOLUME);
    trackImageBuffer[offset++] = CODE44B(VOLUME);
    trackImageBuffer[offset++] = CODE44A((uint8_t) track);
    trackImageBuffer[offset++] = CODE44B((uint8_t) track);
    trackImageBuffer[offset++] = CODE44A(sector);
    trackImageBuffer[offset++] = CODE44B(sector);
    trackImageBuffer[offset++] = CODE44A(VOLUME ^ ((uint8_t) track) ^ sector);
    trackImageBuffer[offset++] = CODE44B(VOLUME ^ ((uint8_t) track) ^ sector);
    #undef CODE44A
    #undef CODE44B
    trackImageBuffer[offset++] = 0xDE;
    trackImageBuffer[offset++] = 0xAA;
    trackImageBuffer[offset++] = 0xEB;

    // Write gap two, which contains six self-sync bytes
    const int gap2_size = 6;
    for (int loop = 0; loop < gap2_size; loop++) {
      trackImageBuffer[offset++] = 0xFF;
    }

    // Write the data field, which contains:
    //   - PROLOGUE (D5AAAD)
    //   - 343 6-BIT BYTES OF NIBBLIZED DATA, INCLUDING A 6-BIT CHECKSUM
    //   - EPILOGUE (DEAAEB)
    trackImageBuffer[offset++] = 0xD5;
    trackImageBuffer[offset++] = 0xAA;
    trackImageBuffer[offset++] = 0xAD;
    memcpy(trackImageBuffer + offset,  Code62(workbuf, sector_order[sector]),  GCR_SECTOR_WITH_CHECKSUM_SIZE);
    offset += GCR_SECTOR_WITH_CHECKSUM_SIZE;
    trackImageBuffer[offset++] = 0xDE;
    trackImageBuffer[offset++] = 0xAA;
    trackImageBuffer[offset++] = 0xEB;

    // Write gap three, which contains 27 self-sync bytes
    for (int loop = 0; loop < GCR_GAP3_SIZE; loop++) {
      trackImageBuffer[offset++] = 0xFF;
    }

    sector++;
  }

  return offset;
}

uint32_t GCR_NibblizeTrack(uint8_t* workbuf, uint8_t* trackImageBuffer, bool dosorder, int track)
{
  return GCR_NibblizeTrackCustomOrder(workbuf, trackImageBuffer, sectornumber[dosorder ? 1 : 0], track);
}

void GCR_SkewTrack(uint8_t* workbuf, int track, int nibbles, uint8_t* trackImageBuffer)
{
  const int SKEW_FACTOR = 768;
  int skewbytes = (track * SKEW_FACTOR) % nibbles;
  memcpy(workbuf,  trackImageBuffer,  static_cast<size_t>(nibbles));
  memcpy(trackImageBuffer,  &workbuf[skewbytes],  static_cast<size_t>(nibbles - skewbytes));
  memcpy(trackImageBuffer + static_cast<size_t>(nibbles - skewbytes),  workbuf,  static_cast<size_t>(skewbytes));
}
