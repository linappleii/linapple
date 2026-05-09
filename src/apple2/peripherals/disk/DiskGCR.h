#ifndef DISKGCR_H
#define DISKGCR_H

#include <cstdint>

constexpr int GCR_WORKBUF_SIZE = 0x2000;

uint32_t GCR_NibblizeTrack(uint8_t* workbuf, uint8_t* trackImageBuffer, bool dosorder, int track);
uint32_t GCR_NibblizeTrackCustomOrder(uint8_t* workbuf, uint8_t* trackImageBuffer, uint8_t* sector_order, int track);
void GCR_DenibblizeTrack(uint8_t* workbuf, uint8_t* trackimage, bool dosorder, int nibbles);
void GCR_SkewTrack(uint8_t* workbuf, int track, int nibbles, uint8_t* trackImageBuffer);

#endif
