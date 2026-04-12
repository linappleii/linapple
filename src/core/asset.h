#ifndef __asset_h
#define __asset_h

#include "apple2/Video.h"

typedef struct assets_tag {
  void         *icon; // Platform-specific icon handle
  VideoSurface *font;
  VideoSurface *splash;
} assets_t;

extern assets_t *assets;

bool Asset_Init(void);

void Asset_Quit(void);

int Asset_InsertMasterDisk(void);

#endif
