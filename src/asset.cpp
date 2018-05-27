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

#include "asset.h"
#include "shim.h"  // SDL_GetBasePath()

#define ASSET_ICON_BMP       "icon.bmp"
#define ASSET_SPLASH_BMP     "splash.bmp"
#define ASSET_CHARSET40_BMP  "charset40.bmp"
#define ASSET_FONT_BMP       "font.bmp"

assets_t assets;

bool Asset_Init(void)
{
  char *path = (char *)SDL_malloc(sizeof(char[PATH_MAX]));

  assets.basepath = SDL_GetBasePath();
  if (NULL != assets.basepath) {
    fprintf(stderr, "SDL_GetBasePath() returned NULL, using current directory.\n");
    assets.basepath = SDL_strdup("./");
  }
  assets.icon = NULL;
  assets.font = NULL;
  assets.charset40 = NULL;
  assets.splash = NULL;

  snprintf(path, PATH_MAX, "%s%s", assets.basepath, ASSET_ICON_BMP);
  assets.icon = SDL_LoadBMP(path);
  if (NULL == assets.icon) {
    fprintf(stderr, "Unable to locate required asset " ASSET_ICON_BMP "\n");
    return false;
  }

  snprintf(path, PATH_MAX, "%s%s", assets.basepath, ASSET_FONT_BMP);
  assets.font = SDL_LoadBMP(path);
  if (NULL == assets.font) {
    fprintf(stderr, "Unable to locate required asset " ASSET_FONT_BMP "\n");
    return false;
  }

  snprintf(path, PATH_MAX, "%s%s", assets.basepath, ASSET_CHARSET40_BMP);
  assets.charset40 = SDL_LoadBMP(path);
  if (NULL == assets.charset40) {
    fprintf(stderr, "Unable to locate required asset " ASSET_CHARSET40_BMP "\n");
    return false;
  }

  snprintf(path, PATH_MAX, "%s%s", assets.basepath, ASSET_SPLASH_BMP);
  assets.splash = SDL_LoadBMP(path);
  if (NULL == assets.splash) {
    fprintf(stderr, "Unable to locate required asset " ASSET_SPLASH_BMP "\n");
    return false;
  }

  free(path);
  return true;
}

void Asset_Quit(void)
{
  if (NULL != assets.icon) {
    SDL_FreeSurface(assets.icon);
    assets.icon = NULL;
  }

  if (NULL != assets.font) {
    SDL_FreeSurface(assets.font);
    assets.font = NULL;
  }

  if (NULL != assets.charset40) {
    SDL_FreeSurface(assets.charset40);
    assets.charset40 = NULL;
  }

  if (NULL != assets.splash) {
    SDL_FreeSurface(assets.splash);
    assets.splash = NULL;
  }

  SDL_free(assets.basepath);
}
