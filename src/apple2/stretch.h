#pragma once
/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2004 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    slouken@libsdl.org
*/


/* This a stretch blit implementation based on ideas given to me by
   Tomasz Cejner - thanks! :)
   April 27, 2000 - Sam Lantinga
*/

#include "apple2/Video.h"

/* Perform a stretch blit between two surfaces of the same format.
   NOTE:  This function is not safe to call from multiple threads!
*/

int VideoSoftStretch(VideoSurface *src, VideoRect *srcrect, VideoSurface *dst, VideoRect *dstrect);

/*  VideoSoftStretchOr  - the same as VideoSoftStretch, but ORed with destination
  NOTE: 24bpp does not support
*/
/* Perform a stretch blit between two surfaces of the same format.
   NOTE:  This function is not safe to call from multiple threads!
*/
int VideoSoftStretchOr(VideoSurface *src, VideoRect *srcrect, VideoSurface *dst, VideoRect *dstrect);

int VideoSoftStretchMono8(VideoSurface *src, VideoRect *srcrect, VideoSurface *dst, VideoRect *dstrect, uint32_t fgbrush, uint32_t bgbrush);

// Font Routines

#define FONT_SIZE_X  6
#define FONT_SIZE_Y  8
// chars in row in font bitmap
#define CHARS_IN_ROW  45
extern VideoSurface *font_sfc;

bool fonts_initialization(void);

void fonts_termination(void);

void font_print(int x, int y, const char *text, VideoSurface *surface, double kx, double ky);

void font_print_right(int x, int y, const char *text, VideoSurface *surface, double kx, double ky);

void font_print_centered(int x, int y, const char *text, VideoSurface *surface, double kx, double ky);

// Some auxiliary functions
void surface_fader(VideoSurface *surface, float r_factor, float g_factor, float b_factor, float a_factor, VideoRect *r);

void putpixel(VideoSurface *surface, int x, int y, uint32_t pixel);

void rectangle(VideoSurface *surface, int x, int y, int w, int h, uint32_t pixel);
