/*
	asset.cpp - LinApple asset management
	Handles loading of ROMs, bitmaps, and disk images.
	Copyright (C) 2018  T. Joseph Carter

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <sys/param.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "asset.h"
#include "stdafx.h"
#include "Util_Path.h"

#include "../res/font.xpm"
#include "../res/icon.xpm"
#include "../build/obj/splash.xpm"

#define ASSET_MASTER_DSK     "Master.dsk"

assets_t *assets = NULL;

SDL_Surface *Asset_LoadBMP(const char *filename)
{
  std::string fullPath = Path::FindDataFile(filename);
  if (fullPath.empty()) {
    fprintf(stderr, "Asset_LoadBMP: Couldn't find %s in any search path!\n", filename);
    return NULL;
  }

  SDL_Surface *surf = SDL_LoadBMP(fullPath.c_str());
  if (NULL != surf) {
    fprintf(stderr, "Asset_LoadBMP: Loaded %s from %s\n", filename, fullPath.c_str());
  }

  return surf;
}

bool Asset_Init(void)
{
  assets = (assets_t *) SDL_calloc(1, sizeof(assets_t));
  if (NULL == assets) {
    fprintf(stderr, "Asset_Init: Allocating assets: %s\n", SDL_GetError());
    return false;
  }

  assets->icon = IMG_ReadXPMFromArray(icon_xpm);
  if (NULL == assets->icon) {
    return false;
  }

  assets->font = IMG_ReadXPMFromArray(font_xpm);
  if (NULL == assets->font) {
    return false;
  }

  assets->splash = IMG_ReadXPMFromArray(splash_xpm);
  return NULL != assets->splash;
}

void Asset_Quit(void)
{
  if (NULL != assets) {
    if (NULL != assets->icon) {
      SDL_DestroySurface(assets->icon);
      assets->icon = NULL;
    }

    if (NULL != assets->font) {
      SDL_DestroySurface(assets->font);
      assets->font = NULL;
    }

    if (NULL != assets->splash) {
      SDL_DestroySurface(assets->splash);
      assets->splash = NULL;
    }

    SDL_free(assets);
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
  char *path = (char *) SDL_malloc(sizeof(char[PATH_MAX+1]));

  int err = Asset_FindMasterDisk(path);
  if (err) {
    SDL_free(path);
    return 255;
  }

  int rc = DiskInsert(0, path, false, false);

  SDL_free(path);
  return rc;
}
