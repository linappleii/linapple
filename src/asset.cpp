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

#include "../res/font.xpm"
#include "../res/icon.xpm"
#include "../build/obj/splash.xpm"

#define ASSET_MASTER_DSK     "Master.dsk"

assets_t *assets = NULL;

#ifdef ASSET_DIR
static char system_assets[] = ASSET_DIR "/";
#else
static char system_assets[] = "./";
#endif
static char *system_exedir = NULL;

SDL_Surface *Asset_LoadBMP(const char *filename)
{
  SDL_Surface *surf;
  char *path = (char *) SDL_malloc(sizeof(char[PATH_MAX]));
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
    fprintf(stderr, "Asset_LoadBMP: Couldn't load %s in either %s or %s!\n", filename, system_assets, system_exedir);
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

int Asset_FindMasterDisk(char *path_out)
{
  // {path_out} gets the path found.
  // Returns non-zero if no path was found.
  //
  // TODO use XDG lookups eg XDG_CONFIG_HOME, XDG_CONFIG_PATHS.
  // TODO the last ditch paths are bunk -- look for better conventions.

  int err = 255;
  const int count = 5;
  char *paths[count];
  char path[MAX_PATH+1];
  
  // Allocate.
  for (int i=0; i<count; i++)
    paths[i] = (char *)SDL_malloc(sizeof(char[PATH_MAX+1]));

  // Define search paths in precedence order.
  strcpy(paths[0], ".");
  strcpy(paths[1], "share/linapple"); // testing convenience
  strcpy(paths[2], SDL_getenv("HOME"));
  strcat(paths[2], "/.local/share/linapple");
  strcpy(paths[3], "/usr/local/share/linapple");
  strcpy(paths[4], "/usr/share/linapple");

  for (auto p: paths) {
    sprintf(path, "%s/%s", p, ASSET_MASTER_DSK);
    printf("[debug] Searching: %s for %s\n", p, ASSET_MASTER_DSK);
    FILE *fp = fopen(path, "r");
    if (fp) {
      fclose(fp);
      strcpy(path_out, path);
      err = 0;
      break;
    }
  }

  if (err)
  {
    printf("[warn ] could not find %s at any of:\n", ASSET_MASTER_DSK);
    for (auto i=0; i<count; i++)
      printf("[warn ] %s\n", paths[i]);
  }
  else
  {
    printf("[info ] Master disk: %s\n", path);
  }

  // Deallocate.
  for (auto i=0; i<count; i++)
    SDL_free(paths[i]);

  return err;
}

int Asset_InsertMasterDisk(void)
{
  char *path = (char *) SDL_malloc(sizeof(char[PATH_MAX+1]));

  int err = Asset_FindMasterDisk(path);
  if (err) {
    SDL_free(path);
    return 255;
  }

  int rc = DiskInsert(0, path, 0, 0);

  SDL_free(path);
  return rc;
}
