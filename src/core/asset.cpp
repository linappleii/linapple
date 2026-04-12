/*
	asset.cpp - LinApple asset management (Core)
*/

#include "core/Common.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>

#include "core/asset.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "apple2/Disk.h"
#include "apple2/Video.h"
#include "apple2/Structs.h"

#include "../res/font.xpm"
#include "../build/obj/splash.xpm"

#define ASSET_MASTER_DSK     "Master.dsk"

assets_t *assets = NULL;

bool Asset_Init(void)
{
  assets = (assets_t *) calloc(1, sizeof(assets_t));
  if (NULL == assets) {
    return false;
  }

  // Icon is loaded by the frontend and assigned to assets->icon

  assets->font = VideoLoadXPM(font_xpm);
  if (NULL == assets->font) {
    return false;
  }

  assets->splash = VideoLoadXPM(splash_xpm);
  return NULL != assets->splash;
}

void Asset_Quit(void)
{
  if (NULL != assets) {
    // Icon is freed by the frontend

    if (NULL != assets->font) {
      VideoDestroySurface(assets->font);
      assets->font = NULL;
    }

    if (NULL != assets->splash) {
      VideoDestroySurface(assets->splash);
      assets->splash = NULL;
    }

    free(assets);
    assets = NULL;
  }
}

int Asset_FindMasterDisk(char *path_out)
{
  std::string fullPath = Path::FindDataFile(ASSET_MASTER_DSK);
  if (fullPath.empty()) {
    printf("[warn ] could not find %s in any search path\n", ASSET_MASTER_DSK);
    return 255;
  }

  Util_SafeStrCpy(path_out, fullPath.c_str(), MAX_PATH);
  printf("[info ] Master disk: %s\n", path_out);
  return 0;
}

int Asset_InsertMasterDisk(void)
{
  char *path = (char *) malloc(MAX_PATH);

  int err = Asset_FindMasterDisk(path);
  if (err) {
    free(path);
    return 255;
  }

  int rc = DiskInsert(0, path, false, false);

  free(path);
  return rc;
}
