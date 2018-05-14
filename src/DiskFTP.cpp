/*
////////////////////////////////////////////////////////////////////////////
////////////  Choose disk image for given slot number? ////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
//// Adapted for linapple - apple][ emulator for Linux by beom beotiger Nov 2007   /////
///////////////////////////////////////////////////////////////////////////////////////
// Original source from one of Brain Games (http://www.braingames.getput.com)
//	game Super Transball 2.(http://www.braingames.getput.com/stransball2/default.asp)
//
// Brain Games crew creates brilliant retro-remakes! Please visit their site to find out more.
//
*/

/* March 2012 AD by Krez, Beom Beotiger */

#include "stdafx.h"
# include <string.h>
#include <stddef.h>

#ifndef _WIN32
//#include <sys/types.h>
#include <sys/stat.h>
//#include <dirent.h>
#endif

#include <time.h>

#include "list.h"
#include "DiskFTP.h"
#include "ftpparse.h"

// how many file names we are able to see at once!
#define FILES_IN_SCREEN		21
// delay after key pressed (in milliseconds??)
#define KEY_DELAY		25
// define time when cache ftp dir.listing must be refreshed
#define RENEW_TIME	24*3600

char * md5str (const char *input); // forward declaration of md5str func

TCHAR	  g_sFTPDirListing[512] = TEXT("cache/ftp."); // name for FTP-directory listing
////////////////////////////////////////////////////////////////////////////////////////
int getstatFTP(struct ftpparse *fp, int * size)
{
	// gets file status and returns: 0 - special or error, 1 - file is a directory, 2 - file is a normal file
	// In: fp - ftpparse struct ftom ftpparse.h
	if(!fp->namelen) return 0;
	if(fp->flagtrycwd == 1) return 1;	// can CWD, it is dir then

	if(fp->flagtryretr == 1) { // we're able to RETR, it's a file then?!
		if(size != NULL) *size = (int)(fp->size / 1024);
		return 2;
	}
	return 0;
}
////////////////////////////////////////////////////////////////////////////////////////
bool ChooseAnImageFTP(int sx,int sy, char *ftp_dir, int slot, char **filename, bool *isdir, int *index_file)
{
/*	Parameters:
 sx, sy - window size,
 ftp_dir - what FTP directory to use,
 slot - in what slot should an image go (common: #6 for 5.25' 140Kb floppy disks, and #7 for hard-disks).
 	slot #5 - for 800Kb floppy disks, but we do not use them in Apple][?
	(They are as a rule with .2mg extension)
 index_file	- from which file we should start cursor (should be static and 0 when changing dir)

 Out:	filename	- chosen file name (or dir name)
	isdir		- if chosen name is a directory
*/
		double facx = double(g_ScreenWidth) / double(SCREEN_WIDTH);
		double facy = double(g_ScreenHeight) / double(SCREEN_HEIGHT);

	SDL_Surface *my_screen;	// for background
	struct ftpparse FTP_PARSE; // for parsing ftp directories
	
#ifndef _WIN32
	struct stat info;
#endif

	if(font_sfc == NULL)
		if(!fonts_initialization()) return false;	//if we don't have a fonts, we just can do none
	char tmpstr[512];
	char ftpdirpath [MAX_PATH];
	snprintf(ftpdirpath, MAX_PATH, "%s/%s%s", g_sFTPLocalDir, g_sFTPDirListing, md5str(ftp_dir));	// get path for FTP dir listing
//	printf("Dir: %s, MD5(dir)=%s\n",ftp_dir,ftpdirpath);

	List<char> files;		// our files
	List<char> sizes;		// and their sizes (or 'dir' for directories)

	int act_file;		// current file
	int first_file;		// from which we output files
	char ch = 0;
// prepare screen
	SDL_Surface *tempSurface;

	if(!g_WindowResized) {
		if(g_nAppMode == MODE_LOGO) tempSurface = g_hLogoBitmap;	// use logobitmap
			else tempSurface = g_hDeviceBitmap;
	}
	else tempSurface = g_origscreen;

	my_screen = SDL_CreateRGBSurface(SDL_SWSURFACE, tempSurface->w, tempSurface->h, tempSurface->format->BitsPerPixel, 0, 0, 0, 0);
	if(tempSurface->format->palette && my_screen->format->palette)
		SDL_SetColors(my_screen, tempSurface->format->palette->colors,
			      0, tempSurface->format->palette->ncolors);

	surface_fader(my_screen, 0.2F, 0.2F, 0.2F, -1, 0);	// fade it out to 20% of normal
	SDL_BlitSurface(tempSurface, NULL, my_screen, NULL);
	SDL_BlitSurface(my_screen, NULL, screen, NULL);		// show background
//	ch = 0;
	#define	NORMAL_LENGTH 60
	if(strlen(ftp_dir) > NORMAL_LENGTH) { ch = ftp_dir[NORMAL_LENGTH]; ftp_dir[NORMAL_LENGTH] = 0;} //cut-off too long string
	font_print_centered(sx/2 ,5 * facy , ftp_dir, screen, 1.5 * facx, 1.3 * facy);
	if(ch) ftp_dir[NORMAL_LENGTH] = ch; //restore cut-off char	

	font_print_centered(sx/2,20 * facy, "Connecting to FTP server... Please wait.", screen, 1 * facx, 1 * facy);
	SDL_Flip(screen);	// show the screen

	bool OKI;
#ifndef _WIN32
	if(stat(ftpdirpath,&info) == 0 && info.st_mtime > time(NULL) - RENEW_TIME) {
		OKI = false; // use this file
	}
	else {
		OKI = ftp_get(ftp_dir, ftpdirpath); // get ftp dir listing
	}
#else
// in WIN32 let's use constant caching? -- need to be redone using file.mtime
	if(GetFileAttributes(ftpdirpath) != DWORD(-1)) OKI = false;
		else OKI = ftp_get(ftp_dir,ftpdirpath); // get ftp dir listing
#endif

	if(OKI) {	// error
		printf("Failed getting FTP directory %s to %s\n",ftp_dir,ftpdirpath);	
		font_print_centered(sx/2,30 * facy, "Failure. Press any key!",screen, 1.4 * facx, 1.1 * facy);
		SDL_Flip(screen);	// show the screen
		SDL_Delay(KEY_DELAY);	// wait some time to be not too fast
		//////////////////////////////////
		// Wait for keypress
		//////////////////////////////////
		SDL_Event event;	// event
		Uint8 *keyboard;	// key state

		event.type = SDL_QUIT;
		while(event.type != SDL_KEYDOWN) {	// wait for key pressed
					SDL_Delay(100);
					SDL_PollEvent(&event);
		}
		SDL_FreeSurface(my_screen);		
		return false;	
	 }

		FILE *fdir = fopen(ftpdirpath,"r");
	
		char *tmp;
		int i,j, B, N;	// for cycles, beginning and end of list

// build prev dir
	if(strcmp(ftp_dir, "ftp://")) {
		tmp = new char[3];
		strcpy(tmp, "..");
		files.Add(tmp);
		tmp = new char[5];
		strcpy(tmp, "<UP>");
		sizes.Add(tmp);	// add sign of directory
		B = 1;
	}
	else	B = 0;	// for sorting dirs 

			while (tmp = fgets(tmpstr,512,fdir)) // first looking for directories
			{
				// clear and then try to fill in FTP_PARSE struct
				memset(&FTP_PARSE,0,sizeof(FTP_PARSE));
				ftpparse(&FTP_PARSE, tmp, strlen(tmp));
				
				int what = getstatFTP(&FTP_PARSE, NULL);
				
				if (strlen(FTP_PARSE.name) > 0 &&  what == 1) // is directory!
				{
					tmp = new char[strlen(FTP_PARSE.name)+1];	// add entity to list
					strcpy(tmp, FTP_PARSE.name);
					files.Add(tmp);
					tmp = new char[6];
					strcpy(tmp, "<DIR>");
					sizes.Add(tmp);	// add sign of directory
				} /* if */

			}
// sort directories. Please, don't laugh at my bubble sorting - it the simplest thing I've ever seen --bb
			if(files.Length() > 2)
			{
				N = files.Length() - 1;
//				B = 1; - defined above
				for(i = N; i > B; i--)
					for(j = B; j < i; j++)
						if(strcasecmp(files[j], files[j + 1]) > 0)
						{
							files.Swap(j,j + 1);
							sizes.Swap(j,j + 1);
						}

			}
			B = files.Length();	// start for files

			(void) rewind (fdir);	// to the start
				// now get all regular files
			while (tmp = fgets(tmpstr,512,fdir))
			{
				int fsize;
				// clear and then try to fill in FTP_PARSE struct
				memset(&FTP_PARSE,0,sizeof(FTP_PARSE));
				ftpparse(&FTP_PARSE, tmp, strlen(tmp));
				
				if ((getstatFTP(&FTP_PARSE, &fsize) == 2)) // is normal file!
				{
					tmp = new char[strlen(FTP_PARSE.name)+1];	// add this entity to list
					strcpy(tmp, FTP_PARSE.name);
					files.Add(tmp);
					tmp = new char[10];	// 1400000KB
					snprintf(tmp, 9, "%dKB", fsize);
					sizes.Add(tmp);	// add this size to list
				} /* if */
			}
			(void) fclose (fdir);
// do sorting for files
			if(files.Length() > 2 && B < files.Length())
			{
				N = files.Length() - 1;
//				B = 1;
				for(i = N; i > B; i--)
					for(j = B; j < i; j++)
						if(strcasecmp(files[j], files[j + 1]) > 0)
						{
							files.Swap(j,j + 1);
							sizes.Swap(j,j + 1);
						}
			}
//	Count out cursor position and file number output
	act_file = *index_file;
	if(act_file >= files.Length()) act_file = 0;		// cannot be more than files in list
	first_file = act_file - (FILES_IN_SCREEN / 2);
	if (first_file < 0) first_file = 0;	// cannot be negativ...

// Show all directories (first) and files then
//	char *tmp;
	char *siz;
//	int i;

	while(true)
	{
		SDL_BlitSurface(my_screen, NULL, screen, NULL);		// show background
		font_print_centered(sx/2 ,5 * facy , ftp_dir, screen, 1.5 * facx, 1.3 * facy);
		if (slot == 6) font_print_centered(sx/2,20 * facy,"Choose image for floppy 140KB drive", screen, 1 * facx, 1 * facy);
		else
			if (slot == 7) font_print_centered(sx/2,20 * facy,"Choose image for Hard Disk", screen, 1 * facx, 1 * facy);
		else
			if (slot == 5) font_print_centered(sx/2,20 * facy,"Choose image for floppy 800KB drive", screen, 1 * facx, 1 * facy);
		else
			if (slot == 1) font_print_centered(sx/2,20 * facy,"Select file name for saving snapshot", screen, 1 * facx, 1 * facy);
		else
			if (slot == 0) font_print_centered(sx/2,20 * facy,"Select snapshot file name for loading", screen, 1 * facx, 1 * facy);

		font_print_centered(sx/2,30 * facy, "Press ENTER to choose, or ESC to cancel",screen, 1.4 * facx, 1.1 * facy);

		files.Rewind();	// from start
		sizes.Rewind();
		i = 0;

//		printf("We've printed some messages, go to file list!\n");
// show all fetched dirs and files
// topX of first fiel visible
		int TOPX	= 45 * facy;

		while(files.Iterate(tmp)) {
			sizes.Iterate(siz);	// also fetch size string

			if (i >= first_file && i < first_file + FILES_IN_SCREEN)
			{ // FILES_IN_SCREEN items on screen
//				char tmp2[80],tmp3[256];

				if (i == act_file) { // show item under cursor (in inverse mode)
					SDL_Rect r;
					r.x= 2;
					r.y= TOPX + (i-first_file) * 15 * facy - 1;
					if(strlen(tmp) > 46) r.w = 46 * 6 * 1.7 * facx + 2;
					   else r.w= strlen(tmp) * 6 * 1.7 * facx + 2;	// 6- FONT_SIZE_X
					r.h= 9 * 1.5 * facy;
					SDL_FillRect(screen, &r, SDL_MapRGB(screen->format,255,0,0));// in RED
				} /* if */

				// print file name with enlarged font
				ch = 0;
				if(strlen(tmp) > 46) { ch = tmp[46]; tmp[46] = 0;} //cut-off too long string
				font_print(4, TOPX + (i - first_file) * 15 * facy, tmp, screen, 1.7 * facx, 1.5 * facy); // show name
				font_print(sx - 70*facx, TOPX + (i - first_file) * 15 * facy, siz, screen, 1.7 * facx, 1.5 * facy);// show info (dir or size)
				if(ch) tmp[46] = ch; //restore cut-off char
			} /* if */
			i++;		// next item
		} /* while */

/////////////////////////////////////////////////////////////////////////////////////////////
// draw rectangles
	rectangle(screen, 0, TOPX - 5, g_ScreenWidth, 320 * facy, SDL_MapRGB(screen->format, 255, 255, 255));
	rectangle(screen, 480 * facx, TOPX - 5, 0, 320 * facy, SDL_MapRGB(screen->format, 255, 255, 255));

	SDL_Flip(screen);	// show the screen
	SDL_Delay(KEY_DELAY);	// wait some time to be not too fast

	//////////////////////////////////
	// Wait for keypress
	//////////////////////////////////
	SDL_Event event;	// event
	Uint8 *keyboard;	// key state

	event.type = SDL_QUIT;
	while(event.type != SDL_KEYDOWN) {	// wait for key pressed
				SDL_Delay(10);
				SDL_PollEvent(&event);
	}

// control cursor
		keyboard = SDL_GetKeyState(NULL);	// get current state of pressed (and not pressed) keys
		if (keyboard[SDLK_UP] || keyboard[SDLK_LEFT]) {
			if (act_file>0) act_file--;	// up one position
			if (act_file<first_file) first_file=act_file;
		} /* if */

		if (keyboard[SDLK_DOWN] || keyboard[SDLK_RIGHT]) {
			if (act_file < (files.Length() - 1)) act_file++;
			if (act_file >= (first_file + FILES_IN_SCREEN)) first_file=act_file - FILES_IN_SCREEN + 1;
		} /* if */

		if (keyboard[SDLK_PAGEUP]) {
			act_file-=FILES_IN_SCREEN;
			if (act_file<0) act_file=0;
			if (act_file<first_file) first_file=act_file;
		} /* if */

		if (keyboard[SDLK_PAGEDOWN]) {
			act_file+=FILES_IN_SCREEN;
			if (act_file>=files.Length()) act_file=(files.Length()-1);
			if (act_file>=(first_file+FILES_IN_SCREEN)) first_file=act_file-FILES_IN_SCREEN + 1;
		} /* if */

		// choose an item?
		if (keyboard[SDLK_RETURN]) {
			// dup string from selected file name
			*filename = strdup(php_trim(files[act_file],strlen(files[act_file])));
//			printf("files[act_file]=%s, *filename=%s\n\n", files[act_file], *filename);
			if(!strcmp(sizes[act_file], "<DIR>") || !strcmp(sizes[act_file], "<UP>"))
			   		*isdir = true;
				else *isdir = false;	// this is directory (catalog in Apple][ terminology)
			*index_file = act_file;	// remember current index
			files.Delete();
			sizes.Delete();
			SDL_FreeSurface(my_screen);

			return true;
		} /* if */

		if (keyboard[SDLK_ESCAPE]) {
			files.Delete();
			sizes.Delete();
			SDL_FreeSurface(my_screen);
			return false;		// ESC has been pressed
		} /* if */

		if (keyboard[SDLK_HOME]) {	// HOME?
			act_file=0;
			first_file=0;
		} /* if */
		if (keyboard[SDLK_END]) {	// END?
			act_file=files.Length() - 1;	// go to the last possible file in list
			first_file=act_file - FILES_IN_SCREEN + 1;
			if(first_file < 0) first_file = 0;
		} /* if */

	}
} /* ChooseAnImageFTP */

/* md5.c - an implementation of the MD5 algorithm and MD5 crypt */
/* See RFC 1321 for a description of the MD5 algorithm.
 */
#define cpu_to_le32(x) (x)
#define le32_to_cpu(x) cpu_to_le32(x)
typedef unsigned int UINT4;

/* F, G, H and I are basic MD5 functions.
 */
#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))

/* ROTATE_LEFT rotates x left n bits.
 */
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x >> (32 - (n)))))

static UINT4 md5_initstate[4] =
{
  0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 
};

static char s1[4] = {  7, 12, 17, 22 };
static char s2[4] = {  5,  9, 14, 20 };
static char s3[4] = {  4, 11, 16, 23 };
static char s4[4] = {  6, 10, 15, 21 };

static UINT4 T[64] =
{
  0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
  0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
  0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
  0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
  0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
  0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
  0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
  0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
  0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
  0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
  0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
  0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
  0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
  0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
  0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
  0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static UINT4 state[4];
static unsigned int length;
static unsigned char buffer[64];

static void
md5_transform (const unsigned char block[64])
{
  int i, j;
  UINT4 a,b,c,d,tmp;
  const UINT4 *x = (UINT4 *) block;

  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];

  /* Round 1 */
  for (i = 0; i < 16; i++)
    {
      tmp = a + F (b, c, d) + le32_to_cpu (x[i]) + T[i];
      tmp = ROTATE_LEFT (tmp, s1[i & 3]);
      tmp += b;
      a = d; d = c; c = b; b = tmp;
    }
  /* Round 2 */
  for (i = 0, j = 1; i < 16; i++, j += 5)
    {
      tmp = a + G (b, c, d) + le32_to_cpu (x[j & 15]) + T[i+16];
      tmp = ROTATE_LEFT (tmp, s2[i & 3]);
      tmp += b;
      a = d; d = c; c = b; b = tmp;
    }
  /* Round 3 */
  for (i = 0, j = 5; i < 16; i++, j += 3)
    {
      tmp = a + H (b, c, d) + le32_to_cpu (x[j & 15]) + T[i+32];
      tmp = ROTATE_LEFT (tmp, s3[i & 3]);
      tmp += b;
      a = d; d = c; c = b; b = tmp;
    }
  /* Round 4 */
  for (i = 0, j = 0; i < 16; i++, j += 7)
    {
      tmp = a + I (b, c, d) + le32_to_cpu (x[j & 15]) + T[i+48];
      tmp = ROTATE_LEFT (tmp, s4[i & 3]);
      tmp += b;
      a = d; d = c; c = b; b = tmp;
    }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}

static void
md5_init(void)
{
  memcpy ((char *) state, (char *) md5_initstate, sizeof (md5_initstate));
  length = 0;
}

static void
md5_update (const char *input, int inputlen)
{
  int buflen = length & 63;
  length += inputlen;
  if (buflen + inputlen < 64) 
    {
      memcpy (buffer + buflen, input, inputlen);
      buflen += inputlen;
      return;
    }
  
  memcpy (buffer + buflen, input, 64 - buflen);
  md5_transform (buffer);
  input += 64 - buflen;
  inputlen -= 64 - buflen;
  while (inputlen >= 64)
    {
      md5_transform ((unsigned char*)input);
      input += 64;
      inputlen -= 64;
    }
  memcpy (buffer, input, inputlen);
  buflen = inputlen;
}

static unsigned char *
md5_final()
{
  int i, buflen = length & 63;

  buffer[buflen++] = 0x80;
  memset (buffer+buflen, 0, 64 - buflen);
  if (buflen > 56)
    {
      md5_transform (buffer);
      memset (buffer, 0, 64);
      buflen = 0;
    }
  
  *(UINT4 *) (buffer + 56) = cpu_to_le32 (8 * length);
  *(UINT4 *) (buffer + 60) = 0;
  md5_transform (buffer);

  for (i = 0; i < 4; i++)
    state[i] = cpu_to_le32 (state[i]);
  return (unsigned char *) state;
}

static char *
md5 (const char *input) 
{
  md5_init();
//  memcpy ((char *) state, (char *) md5_initstate, sizeof (md5_initstate));
//  length = 0;
  md5_update (input, strlen (input));
  return (char *)md5_final ();
}

char *
md5str (const char *input) 
{
	char result[16 * 3 +1];
	unsigned char* digest = (unsigned char*)md5 (input);
	int i;

	for (i=0; i < 16; i++)
		sprintf (result+2*i, "%02X", digest[i]);
	return result;
}

