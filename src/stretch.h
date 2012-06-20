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


/*	Auxiallary routines */


/* This a stretch blit implementation based on ideas given to me by
   Tomasz Cejner - thanks! :)
   April 27, 2000 - Sam Lantinga
*/

/* This isn't ready for general consumption yet - it should be folded
   into the general blitting mechanism.
*/
//static unsigned char copy_row[4096];

/* Perform a stretch blit between two surfaces of the same format.
   NOTE:  This function is not safe to call from multiple threads!
*/

int SDL_SoftStretchMy(SDL_Surface *src, SDL_Rect *srcrect,
                    SDL_Surface *dst, SDL_Rect *dstrect);


/*	SDL_SoftStretchOr	- the same as SDL_SoftStretch, but ORed with destination
	NOTE: 24bpp does not support	-- beom beotiger 2007 November
*/
/* Perform a stretch blit between two surfaces of the same format.
   NOTE:  This function is not safe to call from multiple threads!
*/
int SDL_SoftStretchOr(SDL_Surface *src, SDL_Rect *srcrect,
		    SDL_Surface *dst, SDL_Rect *dstrect);

int SDL_SoftStretchMono8(SDL_Surface *src, SDL_Rect *srcrect,
			 SDL_Surface *dst, SDL_Rect *dstrect, Uint8 brush);



#if SDL_BYTEORDER == SDL_BIG_ENDIAN
// PPC values:
#define AMASK  0xff000000
#define BMASK  0x000000ff
#define GMASK  0x0000ff00
#define RMASK  0x00ff0000
#define AOFFSET 0
#define BOFFSET 3
#define GOFFSET 2
#define ROFFSET 1

#else
// Intel values:
#define AMASK  0xff000000
#define BMASK  0x000000ff
#define GMASK  0x0000ff00
#define RMASK  0x00ff0000
#define AOFFSET 3
#define BOFFSET 0
#define GOFFSET 1
#define ROFFSET 2

#endif


  /* ----------------------------------------------------------------*/
 /* ---------------------- FONT routines ---------------------------*/
/* ----------------------------------------------------------------*/

#define FONT_SIZE_X	6
#define FONT_SIZE_Y	8
// chars in row in font bitmap
#define CHARS_IN_ROW	45
extern SDL_Surface *font_sfc;

bool fonts_initialization(void);
void fonts_termination(void);
void font_print(int x,int y,const char *text,SDL_Surface *surface, double kx, double ky);
void font_print_right(int x,int y,const char *text,SDL_Surface *surface, double kx, double ky);
void font_print_centered(int x, int y, const char *text, SDL_Surface *surface, double kx, double ky);
///////////////////////////////////////////////////////////////////////////
////// Some auxiliary functions //////////////
///////////////////////////////////////////////////////////////////////////
void surface_fader(SDL_Surface *surface,float r_factor,float g_factor,float b_factor,float a_factor,SDL_Rect *r);
void putpixel(SDL_Surface *surface, int x, int y, Uint32 pixel);
void rectangle(SDL_Surface *surface, int x, int y, int w, int h, Uint32 pixel);
