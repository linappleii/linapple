/*
	asset.cpp - LinApple asset management
	<one line to give the program's name and a brief idea of what it does.>
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
#include <SDL.h>
#include <SDL_image.h>

#include "asset.h"
#include "stdafx.h"  // for Disk.h DiskInsert()
#include "shim.h"  // SDL_GetBasePath()

#include "../res/charset40.xpm"
#include "../res/font.xpm"
#include "../res/icon.xpm"
#include "../res/splash.xpm"

#define ASSET_MASTER_DSK     "Master.dsk"

assets_t *assets = NULL;

#ifdef ASSET_DIR
static char system_assets[] = ASSET_DIR "/";
#else
static char system_assets[] = "./"
#endif
static char *system_exedir = NULL;

SDL_Surface *Asset_LoadBMP(const char *filename)
{
  SDL_Surface *surf;
  char *path = (char *)SDL_malloc(sizeof(char[PATH_MAX]));
  if (NULL == path) {
    fprintf(stderr, "Asset_LoadBMP: Allocating path: %s\n", SDL_GetError());
    return NULL;
  }

  snprintf(path, PATH_MAX, "%s%s", system_assets, filename);
  surf = SDL_LoadBMP(path);
  if (NULL == surf) {
    snprintf(path, PATH_MAX, "%s%s", system_exedir, filename);
    surf = SDL_LoadBMP(path);
  }

  if (NULL != surf) {
    fprintf(stderr, "Asset_LoadBMP: Loaded %s from %s\n", filename, path);
  } else {
    fprintf(stderr, "Asset_LoadBMP: Couldn't load %s in either %s or %s!\n",
        filename, system_assets, system_exedir);
  }

  SDL_free(path);
  return surf;
}

bool Asset_Init(void)
{
  system_exedir = SDL_GetBasePath();
  if (NULL == system_exedir) {
    fprintf(stderr, "Asset_Init: Warning: SDL_GetBasePath() returned NULL, using \"./\"\n");
    system_exedir = SDL_strdup("./");
  }

  assets = (assets_t *)SDL_calloc(1, sizeof(assets_t));
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

  assets->charset40 = IMG_ReadXPMFromArray(charset40_xpm);
  if (NULL == assets->charset40) {
    return false;
  }

  assets->splash = IMG_ReadXPMFromArray(splash_xpm);
  if (NULL == assets->splash) {
    return false;
  }

  return true;
}

void Asset_Quit(void)
{
  if (NULL != assets) {
    if (NULL != assets->icon) {
      SDL_FreeSurface(assets->icon);
      assets->icon = NULL;
    }

    if (NULL != assets->font) {
      SDL_FreeSurface(assets->font);
      assets->font = NULL;
    }

    if (NULL != assets->charset40) {
      SDL_FreeSurface(assets->charset40);
      assets->charset40 = NULL;
    }

    if (NULL != assets->splash) {
      SDL_FreeSurface(assets->splash);
      assets->splash = NULL;
    }

    if (NULL != system_exedir) {
      SDL_free(system_exedir);
      system_exedir = NULL;
    }

    SDL_free(assets);
  }
}

// FIXME: How this is done is currently kinda screwed up. Refactor
int Asset_InsertMasterDisk(void)
{
  int rc;
  char *path = (char *)SDL_malloc(sizeof(char[PATH_MAX]));
  snprintf(path, PATH_MAX, "%s%s", system_assets, ASSET_MASTER_DSK);
  rc = DiskInsert(0, path, 0, 0);
  if (IMAGE_ERROR_UNABLE_TO_OPEN == rc) {
    snprintf(path, PATH_MAX, "%s%s", system_exedir, ASSET_MASTER_DSK);
    rc = DiskInsert(0, path, 0, 0);
  }

  SDL_free(path);
  return 0;
}
