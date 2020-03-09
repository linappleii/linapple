/*
	asset.h - LinApple asset management
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

#ifndef __asset_h
#define __asset_h

typedef struct {
  SDL_Surface *icon;
  SDL_Surface *font;
  SDL_Surface *splash;
} assets_t;

extern assets_t *assets;

bool Asset_Init(void);

void Asset_Quit(void);

int Asset_InsertMasterDisk(void);

#endif
