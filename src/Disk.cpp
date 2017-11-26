/*
AppleWin : An Apple //e emulator for Windows

Copyright (C) 1994-1996, Michael O'Brien
Copyright (C) 1999-2001, Oliver Schmidt
Copyright (C) 2002-2005, Tom Charlesworth
Copyright (C) 2006-2007, Tom Charlesworth, Michael Pohoreski

AppleWin is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

AppleWin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with AppleWin; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Description: Disk
 *
 * Author: Various
 */

/* Adaptation for SDL and POSIX (l) by beom beotiger, Nov-Dec 2007 */

/* AND March 2012 AD */

#include "stdafx.h"
#include "wwrapper.h"
#include "ftpparse.h"
#include "DiskFTP.h"

#ifndef _WIN32
#include <sys/stat.h>
#endif

// for DiskUnGzip
#include <zlib.h>
// for DiskUnZip
#include <zip.h>

//#pragma  hdrstop
//#include "resource.h"

char Disk2_rom[] =
		"\xA2\x20\xA0\x00\xA2\x03\x86\x3C\x8A\x0A\x24\x3C\xF0\x10\x05\x3C"
		"\x49\xFF\x29\x7E\xB0\x08\x4A\xD0\xFB\x98\x9D\x56\x03\xC8\xE8\x10"
		"\xE5\x20\x58\xFF\xBA\xBD\x00\x01\x0A\x0A\x0A\x0A\x85\x2B\xAA\xBD"
		"\x8E\xC0\xBD\x8C\xC0\xBD\x8A\xC0\xBD\x89\xC0\xA0\x50\xBD\x80\xC0"
		"\x98\x29\x03\x0A\x05\x2B\xAA\xBD\x81\xC0\xA9\x56\x20\xA8\xFC\x88"
		"\x10\xEB\x85\x26\x85\x3D\x85\x41\xA9\x08\x85\x27\x18\x08\xBD\x8C"
		"\xC0\x10\xFB\x49\xD5\xD0\xF7\xBD\x8C\xC0\x10\xFB\xC9\xAA\xD0\xF3"
		"\xEA\xBD\x8C\xC0\x10\xFB\xC9\x96\xF0\x09\x28\x90\xDF\x49\xAD\xF0"
		"\x25\xD0\xD9\xA0\x03\x85\x40\xBD\x8C\xC0\x10\xFB\x2A\x85\x3C\xBD"
		"\x8C\xC0\x10\xFB\x25\x3C\x88\xD0\xEC\x28\xC5\x3D\xD0\xBE\xA5\x40"
		"\xC5\x41\xD0\xB8\xB0\xB7\xA0\x56\x84\x3C\xBC\x8C\xC0\x10\xFB\x59"
		"\xD6\x02\xA4\x3C\x88\x99\x00\x03\xD0\xEE\x84\x3C\xBC\x8C\xC0\x10"
		"\xFB\x59\xD6\x02\xA4\x3C\x91\x26\xC8\xD0\xEF\xBC\x8C\xC0\x10\xFB"
		"\x59\xD6\x02\xD0\x87\xA0\x00\xA2\x56\xCA\x30\xFB\xB1\x26\x5E\x00"
		"\x03\x2A\x5E\x00\x03\x2A\x91\x26\xC8\xD0\xEE\xE6\x27\xE6\x3D\xA5"
		"\x3D\xCD\x00\x08\xA6\x2B\x90\xDB\x4C\x01\x08\x00\x00\x00\x00\x00"
		;


static BYTE DiskControlMotor (WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
static BYTE DiskControlStepper (WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
static BYTE DiskEnable (WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
static BYTE DiskReadWrite (WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
static BYTE DiskSetLatchValue (WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
static BYTE DiskSetReadMode (WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
static BYTE DiskSetWriteMode (WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);

#define LOG_DISK_ENABLED 1

// __VA_ARGS__ not supported on MSVC++ .NET 7.x
#if (LOG_DISK_ENABLED) && !defined(_VC71)
	#define LOG_DISK(format, ...) LOG(format, __VA_ARGS__)
#else
	#define LOG_DISK(...)
#endif

// Public _________________________________________________________________________________________

	BOOL      enhancedisk     = 1;

// Private ________________________________________________________________________________________

	const int MAX_DISK_IMAGE_NAME = 15;
	const int MAX_DISK_FULL_NAME  = MAX_PATH;

	struct Disk_t
	{
		TCHAR  imagename[ MAX_DISK_IMAGE_NAME + 1 ];
		TCHAR  fullname [ MAX_DISK_FULL_NAME  + 1 ];
		HIMAGE imagehandle;
		int    track;
		LPBYTE trackimage;
		int    phase;
		int    byte;
		BOOL   writeprotected;
		BOOL   trackimagedata;
		BOOL   trackimagedirty;
		DWORD  spinning;
		DWORD  writelight;
		int    nibbles;
	};

static WORD      currdrive       = 0;
static BOOL      diskaccessed    = 0;
static Disk_t    g_aFloppyDisk[DRIVES];
static BYTE      floppylatch     = 0;
static BOOL      floppymotoron   = 0;
static BOOL      floppywritemode = 0;
static WORD      phases; // state bits for stepper magnet phases 0 - 3

static void CheckSpinning();
static Disk_Status_e GetDriveLightStatus( const int iDrive );
static bool IsDriveValid( const int iDrive );
static void ReadTrack (int drive);
static void RemoveDisk (int drive);
static void WriteTrack (int drive);

//===========================================================================
void CheckSpinning () {
  DWORD modechange = (floppymotoron && !g_aFloppyDisk[currdrive].spinning);
  if (floppymotoron)
    g_aFloppyDisk[currdrive].spinning = 20000;
  if (modechange)
    FrameRefreshStatus(DRAW_LEDS);
}

//===========================================================================
Disk_Status_e GetDriveLightStatus( const int iDrive )
{
	if (IsDriveValid( iDrive ))
	{
		Disk_t *pFloppy = & g_aFloppyDisk[ iDrive ];

		if (pFloppy->spinning)
		{
			/*if (pFloppy->writeprotected)
				return DISK_STATUS_PROT;*/

			if (pFloppy->writelight)
				return DISK_STATUS_WRITE;
			else
				return DISK_STATUS_READ;
		}
		else
			return DISK_STATUS_OFF;
	}

	return DISK_STATUS_OFF;
}

//===========================================================================
char *GetImageTitle (LPCTSTR imagefilename, Disk_t * fptr)
{// returns image title
	TCHAR   imagetitle[ MAX_DISK_FULL_NAME+1 ];
	LPCTSTR startpos = imagefilename;

  // imagetitle = <FILENAME.EXT>
  if (_tcsrchr(startpos,FILE_SEPARATOR))
    startpos = _tcsrchr(startpos,FILE_SEPARATOR)+1;
  _tcsncpy(imagetitle,startpos,MAX_DISK_FULL_NAME);
  imagetitle[MAX_DISK_FULL_NAME] = 0;

  // if imagetitle contains a lowercase char, then found=1 (why?)
  BOOL found = 0;
  int  loop  = 0;
  while (imagetitle[loop] && !found)
  {
    if (IsCharLower(imagetitle[loop]))
      found = 1;
    else
      loop++;
  }

  if ((!found) && (loop > 2))
    CharLowerBuff(imagetitle+1,_tcslen(imagetitle+1));	// to lower case?

  // fptr->fullname = <FILENAME.EXT>
  _tcsncpy( fptr->fullname, /*imagetitle*/imagefilename, MAX_DISK_FULL_NAME );
  fptr->fullname[ MAX_DISK_FULL_NAME ] = 0;

  if (imagetitle[0])
  {
    LPTSTR dot = imagetitle;
    if (_tcsrchr(dot,TEXT('.')))
      dot = _tcsrchr(dot,TEXT('.'));
    if (dot > imagetitle)
      *dot = 0;
  }

	// fptr->imagename = <FILENAME> (ie. no extension)
	_tcsncpy( fptr->imagename, imagetitle, MAX_DISK_IMAGE_NAME );
	fptr->imagename[ MAX_DISK_IMAGE_NAME ] = 0;
	return fptr->imagename;	// return it
}


//===========================================================================
bool IsDriveValid( const int iDrive )
{
	if (iDrive < 0)
		return false;

	if (iDrive >= DRIVES)
		return false;

	return true;
}


//===========================================================================

static void AllocTrack(int drive)
{
  Disk_t * fptr = &g_aFloppyDisk[drive];
  fptr->trackimage = (LPBYTE)VirtualAlloc(NULL,NIBBLES_PER_TRACK,MEM_COMMIT,PAGE_READWRITE);
}

//===========================================================================
static void ReadTrack (int iDrive)
{
	if (! IsDriveValid( iDrive ))
		return;

	Disk_t *pFloppy = &g_aFloppyDisk[ iDrive ];

	if (pFloppy->track >= TRACKS)
	{
		pFloppy->trackimagedata = 0;
		return;
	}

	if (! pFloppy->trackimage)
		AllocTrack( iDrive );

	if (pFloppy->trackimage && pFloppy->imagehandle)
	{
        LOG_DISK("read track %2X%s\r", pFloppy->track, (pFloppy->phase & 1) ? ".5" : "");

		ImageReadTrack(
			pFloppy->imagehandle,
			pFloppy->track,
			pFloppy->phase,
			pFloppy->trackimage,
			&pFloppy->nibbles);

		pFloppy->byte           = 0;
		pFloppy->trackimagedata = (pFloppy->nibbles != 0);
	}
}

//===========================================================================
static void RemoveDisk (int iDrive)
{
	Disk_t *pFloppy = &g_aFloppyDisk[iDrive];

	if (pFloppy->imagehandle)
	{
		if (pFloppy->trackimage && pFloppy->trackimagedirty)
			WriteTrack( iDrive);

		ImageClose(pFloppy->imagehandle);
		pFloppy->imagehandle = (HIMAGE)0;
	}

	if (pFloppy->trackimage)
	{
		VirtualFree(pFloppy->trackimage,0,MEM_RELEASE);
		pFloppy->trackimage     = NULL;
		pFloppy->trackimagedata = 0;
	}

	memset( pFloppy->imagename, 0, MAX_DISK_IMAGE_NAME+1 );
	memset( pFloppy->fullname , 0, MAX_DISK_FULL_NAME +1 );
}

//===========================================================================
static void WriteTrack (int iDrive)
{
	Disk_t *pFloppy = &g_aFloppyDisk[ iDrive ];

	if (pFloppy->track >= TRACKS)
		return;

	if (pFloppy->writeprotected)
		return;

	if (pFloppy->trackimage && pFloppy->imagehandle)
		ImageWriteTrack(
			pFloppy->imagehandle,
			pFloppy->track,
			pFloppy->phase,
			pFloppy->trackimage,
			pFloppy->nibbles );

	pFloppy->trackimagedirty = 0;
}

//
// ----- ALL GLOBALLY ACCESSIBLE FUNCTIONS ARE BELOW THIS LINE -----
//

//===========================================================================
void DiskBoot () {

  // THIS FUNCTION RELOADS A PROGRAM IMAGE IF ONE IS LOADED IN DRIVE ONE.
  // IF A DISK IMAGE OR NO IMAGE IS LOADED IN DRIVE ONE, IT DOES NOTHING.
  if (g_aFloppyDisk[0].imagehandle && ImageBoot(g_aFloppyDisk[0].imagehandle))
    floppymotoron = 0;

}

//===========================================================================
static BYTE DiskControlMotor (WORD, WORD address, BYTE, BYTE, ULONG) {
  floppymotoron = address & 1;
  CheckSpinning();
  return MemReturnRandomData(1);
}

//===========================================================================
static BYTE DiskControlStepper (WORD, WORD address, BYTE, BYTE, ULONG)
{
  Disk_t * fptr = &g_aFloppyDisk[currdrive];
  int phase     = (address >> 1) & 3;
  int phase_bit = (1 << phase);

  // update the magnet states
  if (address & 1)
  {
    // phase on
    phases |= phase_bit;
    LOG_DISK("track %02X phases %X phase %d on  address $C0E%X\r", fptr->phase, phases, phase, address & 0xF);
  }
  else
  {
    // phase off
    phases &= ~phase_bit;
    LOG_DISK("track %02X phases %X phase %d off address $C0E%X\r", fptr->phase, phases, phase, address & 0xF);
  }

  // check for any stepping effect from a magnet
  // - move only when the magnet opposite the cog is off
  // - move in the direction of an adjacent magnet if one is on
  // - do not move if both adjacent magnets are on
  // momentum and timing are not accounted for ... maybe one day!
  int direction = 0;
  if (phases & (1 << ((fptr->phase + 1) & 3)))
    direction += 1;
  if (phases & (1 << ((fptr->phase + 3) & 3)))
    direction -= 1;

  // apply magnet step, if any
  if (direction)
  {
    fptr->phase = MAX(0, MIN(79, fptr->phase + direction));
    int newtrack = MIN(TRACKS-1, fptr->phase >> 1); // (round half tracks down)
    LOG_DISK("newtrack %2X%s\r", newtrack, (fptr->phase & 1) ? ".5" : "");
    if (newtrack != fptr->track)
    {
      if (fptr->trackimage && fptr->trackimagedirty)
      {
        WriteTrack(currdrive);
      }
      fptr->track          = newtrack;
      fptr->trackimagedata = 0;
    }
  }
  return (address == 0xE0) ? 0xFF : MemReturnRandomData(1);
}

//===========================================================================
void DiskDestroy ()
{
	RemoveDisk(0);
	RemoveDisk(1);
	unlink("drive0.dsk");	// delete temporary ungzipped (unzipped)images
	unlink("drive1.dsk");
}

//===========================================================================
static BYTE DiskEnable (WORD, WORD address, BYTE, BYTE, ULONG) {
  currdrive = address & 1;
  g_aFloppyDisk[!currdrive].spinning   = 0;
  g_aFloppyDisk[!currdrive].writelight = 0;
  CheckSpinning();
  return 0;
}


//===========================================================================
void DiskEject( const int iDrive )
{
	if (IsDriveValid( iDrive ))
	{
		RemoveDisk( iDrive );
	}
}


//===========================================================================
LPCTSTR DiskGetFullName (int drive) {
  return g_aFloppyDisk[drive].fullname;
}


//===========================================================================
void DiskGetLightStatus (int *pDisk1Status_, int *pDisk2Status_)
{
//	*drive1 = g_aFloppyDisk[0].spinning ? g_aFloppyDisk[0].writelight ? 2 : 1 : 0;
//	*drive2 = g_aFloppyDisk[1].spinning ? g_aFloppyDisk[1].writelight ? 2 : 1 : 0;

	if (pDisk1Status_)
		*pDisk1Status_ = GetDriveLightStatus( 0 );
	if (pDisk2Status_)
		*pDisk2Status_ = GetDriveLightStatus( 1 );
}

//===========================================================================
LPCTSTR DiskGetName (int drive) {
  return g_aFloppyDisk[drive].imagename;
}

//===========================================================================

BYTE Disk_IORead(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
BYTE Disk_IOWrite(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);

void DiskInitialize ()
{
	int loop = DRIVES;
	while (loop--)
		ZeroMemory(&g_aFloppyDisk[loop],sizeof(Disk_t ));

/*	TCHAR imagefilename[MAX_PATH];
	_tcscpy(imagefilename,g_sProgramDir);
	_tcscat(imagefilename,TEXT("MASTER.DSK")); // TODO: Should remember last disk by user*/
/*#define MASTER_DISK	"Master.dsk"
	DiskInsert(0, MASTER_DISK, 0, 0);*/
}

//===========================================================================
////////////////////////// Unzip .gz file to drive0.dsk or drive1.dsk (set in fname) into current directory
bool DiskUnGzip(char * gzname, char * fname)
{
#define GZBUF	4096
	gzFile gzF;
	FILE *dskF;
	int len;
	char gzbuf[GZBUF];	// buffer for copied data
	if((gzF = gzopen(gzname, "rb")) == NULL) return false;
	dskF = fopen(fname, "wb");
	if(dskF == NULL) { gzclose(gzF); return false; }

	while(!gzeof(gzF)) {
		len = gzread(gzF, gzbuf, GZBUF);
		fwrite(gzbuf, 1, len, dskF);
	}
	fclose(dskF);
	gzclose(gzF);
	return true;
}
//===========================================================================
////////////////////////// Unzip .zip file to drive0.dsk or drive1.dsk (set in fname) into current directory
bool DiskUnZip(char * gzname, char * fname)
{
#define ZIPBUF	4096
	struct zip *arch;
	struct zip_file *zip_f;
	FILE *dskF;
	int len;
	char zipbuf[ZIPBUF];	// buffer for copied data
	// open zip archive
	if((arch = zip_open(gzname, 0, NULL)) == NULL) return false;
	dskF = fopen(fname, "wb");
	if(dskF == NULL) { zip_close(arch); return false; }
	// try to open first file in zip archive
	if((zip_f = zip_fopen_index(arch,0,0)) == NULL) { zip_close(arch); return false; }
	// read entire file into another file
	while((len = zip_fread(zip_f, zipbuf, ZIPBUF))>0)
		fwrite(zipbuf, 1, len, dskF);
	zip_fclose(zip_f);
	fclose(dskF);
	zip_close(arch);
	return true;
}
//===========================================================================
int DiskInsert (int drive, LPCTSTR imagefilename, BOOL writeprotected, BOOL createifnecessary) {
  Disk_t * fptr = &g_aFloppyDisk[drive];
  char s_title[MAX_DISK_IMAGE_NAME + 32];
  char *tmp = (char*)imagefilename;
  char tempdsk[12];

  if (fptr->imagehandle)
    RemoveDisk(drive);
  ZeroMemory(fptr,sizeof(Disk_t ));

//	let us deal with .gz files
  int lf = strlen(imagefilename);
  if(lf > 3 && imagefilename[lf-1] == 'z' && imagefilename[lf-2] == 'g' && imagefilename[lf-3] == '.')
  {// .gz?
	snprintf(tempdsk, 12, "drive%d.dsk", drive);
        if (DiskUnGzip((char*)imagefilename, tempdsk)) {
		writeprotected = 1;
		createifnecessary = 0;
		tmp = tempdsk;
	}
  }
  else
  if(lf > 4 && imagefilename[lf-1] == 'p' && imagefilename[lf-2] == 'i' && imagefilename[lf-3] == 'z'
  						&& imagefilename[lf-4] == '.') // .zip?
  {
	snprintf(tempdsk, 12, "drive%d.dsk", drive);
        if (DiskUnZip((char*)imagefilename, tempdsk)) {
		writeprotected = 1;
		createifnecessary = 0;
		tmp = tempdsk;
	}
  }

  fptr->writeprotected = writeprotected;
  int error = ImageOpen(/*imagefilename*/tmp,
                        &fptr->imagehandle,
                        &fptr->writeprotected,
                        createifnecessary);
  if (error == IMAGE_ERROR_NONE)
  {// if no errors
//	  _tcsncpy(fptr->(imageinfoptr)imagehandle->filename, imagefilename, MAX_PATH);
    tmp = GetImageTitle(imagefilename, fptr);	// get image title
    snprintf(s_title, MAX_DISK_IMAGE_NAME + 32, "%s - %s", g_pAppTitle, tmp); //
    if(drive == 0) SDL_WM_SetCaption(s_title, g_pAppTitle);// change caption just for drive 0 (leading)
    printf("Disk is inserted. Full name = %s\n", /*g_aFloppyDisk[drive].fullname*/imagefilename);
  }
  return error;
}

//===========================================================================
BOOL DiskIsSpinning ()
{
	return floppymotoron;
}

//===========================================================================
void DiskNotifyInvalidImage (LPCTSTR imagefilename,int error)
{
	TCHAR buffer[MAX_PATH+128];

	switch (error)
	{

	case 1:
		sprintf(
			buffer,
			TEXT("Unable to open the file %s."),
			(LPCTSTR)imagefilename);
		break;

	case 2:
		sprintf(
			buffer,
			TEXT("Unable to use the file %s\nbecause the ")
			TEXT("disk image format is not recognized."),
			(LPCTSTR)imagefilename);
		break;

	default:
		// IGNORE OTHER ERRORS SILENTLY
		return;
	}

/*	MessageBox(
		g_hFrameWindow,
		buffer,
		g_pAppTitle,
		MB_ICONEXCLAMATION | MB_SETFOREGROUND);*/
	fprintf(stderr, "Inserting disk failure: %s\n", buffer);
}


//===========================================================================
bool DiskGetProtect( const int iDrive )
{
	if (IsDriveValid( iDrive ))
	{
		Disk_t *pFloppy = &g_aFloppyDisk[ iDrive ];
		if (pFloppy->writeprotected)
			return true;
	}
	return false;
}


//===========================================================================
void DiskSetProtect( const int iDrive, const bool bWriteProtect )
{
	if (IsDriveValid( iDrive ))
	{
		Disk_t *pFloppy = &g_aFloppyDisk[ iDrive ];
		pFloppy->writeprotected = bWriteProtect;
	}
}


//===========================================================================
static BYTE DiskReadWrite (WORD programcounter, WORD, BYTE, BYTE, ULONG) {
  Disk_t * fptr = &g_aFloppyDisk[currdrive];
  diskaccessed = 1;
  if ((!fptr->trackimagedata) && fptr->imagehandle)
    ReadTrack(currdrive);
  if (!fptr->trackimagedata)
    return 0xFF;
  BYTE result = 0;
  if ((!floppywritemode) || (!fptr->writeprotected)) {
    if (floppywritemode) {
      if (floppylatch & 0x80) {
        *(fptr->trackimage+fptr->byte) = floppylatch;
        fptr->trackimagedirty = 1;
      } else {
        return 0;
			}
    } else {
      result = *(fptr->trackimage+fptr->byte);
		}
	}
  if (0)
  {
    LOG_DISK("nib %4X = %2X\r", fptr->byte, result);
  }
  if (++fptr->byte >= fptr->nibbles)
    fptr->byte = 0;
  return result;
}

//===========================================================================

void DiskReset()
{
	floppymotoron = 0;
	phases = 0;
}


//===========================================================================
void DiskSelectImage (int drive, LPSTR pszFilename)
{
	//  omit pszFilename??? for some reason or not!
	static int findex = 0;		// file index will be remembered for current dir
	static int backdx = 0;	//reserve
	static int dirdx  = 0;  // reserve for dirs

	char * filename = NULL;			// given filename
	char fullpath[MAX_PATH];	// full path for it
	char tmppath [MAX_PATH];
	bool isdir;			// if given filename is a directory?

//   TCHAR directory[MAX_PATH] = TEXT("");
//   TCHAR filename[MAX_PATH];
//	TCHAR title[40];

	findex = backdx;
	isdir = true;
	strcpy(fullpath, g_sCurrentDir);	// global var for disk selecting directory
//	printf("fullpath=%s\n", fullpath);

	while(isdir)
	{
		if(!ChooseAnImage(g_ScreenWidth, g_ScreenHeight, fullpath, 6, &filename, &isdir, &findex)) {
			DrawFrameWindow();
			return;	// if ESC was pressed, just leave
		}
//   strcpy(filename, pszFilename);
//		printf("We got next:\n");
//		printf("isdir=%d, findex=%d, filename=%s\n", isdir, findex, filename);
		if(isdir)
		{
			if(!strcmp(filename, ".."))	// go to the upper directory
			{
				filename = strrchr(fullpath, FILE_SEPARATOR); // look for last '/'
				if(filename) *filename = '\0';	// cut it off
				if(strlen(fullpath) == 0) strcpy(fullpath,"/");	//we don't want fullpath to be empty
				findex = dirdx;	// restore

			}
			else
			{
				if(strcmp(fullpath, "/")) snprintf(tmppath, MAX_PATH, "%s/%s", fullpath, filename); // next dir
					else snprintf(tmppath, MAX_PATH, "/%s", filename);
				strcpy(fullpath, tmppath);	// got ot anew
//				printf("We build %s\n", tmppath);
				dirdx = findex; // store it
				findex = 0;	// start with beginning of dir
			}
		}/* if isdir */
	} /* while isdir */
	// we chose some file
	strcpy(g_sCurrentDir, fullpath);
	RegSaveString(TEXT("Preferences"),REGVALUE_PREF_START_DIR, 1, g_sCurrentDir);// save it

	snprintf(tmppath, MAX_PATH, "%s/%s", fullpath, filename); // next dir
	strcpy(fullpath, tmppath);	// got ot anew


     int error = DiskInsert(drive, fullpath, 0, 1);
     if (!error)
     {
/*       filename[ofn.nFileOffset] = 0;
       if (_tcsicmp(directory,filename))
         RegSaveString(TEXT("Preferences"),REGVALUE_PREF_START_DIR,1,filename);*/

// in future: save file name in registry for future fetching
// for one drive will be one reg parameter
//	RegSaveString(TEXT("Preferences"),REGVALUE_PREF_START_DIR, 1,filename);
	if(drive == 0)
		RegSaveString(TEXT("Preferences"),REGVALUE_DISK_IMAGE1, 1, fullpath);
	else
		RegSaveString(TEXT("Preferences"),REGVALUE_DISK_IMAGE2, 1, fullpath);
     }
     else
     {
	  DiskNotifyInvalidImage(filename,error); // show error on the screen (or in console for our case)
     }
     backdx = findex;	//store cursor position
     DrawFrameWindow();
}

//===========================================================================
void DiskSelect (int drive)
{
	char szSelect[] = "";
	DiskSelectImage(drive, szSelect);	// drive is 0 for D1, 1 - for D2
}


//===========================================================================
void Disk_FTP_SelectImage (int drive)	// select a disk image using FTP
{
	//  omit pszFilename??? for some reason or not!
	static int findex = 0;	// file index will be remembered for current dir
	static int backdx = 0;	//reserve
	static int dirdx  = 0;  // reserve for dirs

	char * filename = NULL;			// given filename
	char fullpath[MAX_PATH];	// full path for it
	char tmppath [MAX_PATH];
	bool isdir;			// if given filename is a directory?
	
#ifndef _WIN32
	struct stat info;
#endif

	findex = backdx;
	isdir = true;
	strcpy(fullpath, g_sFTPServer);	// global var for FTP path

	while(isdir)
	{
//		printf("Disk_FTP_SelectImage: fullpath=%s\n", fullpath);

		if(!ChooseAnImageFTP(g_ScreenWidth, g_ScreenHeight, fullpath, 6, &filename, &isdir, &findex)) {
			DrawFrameWindow();
			return;	// if ESC was pressed, just leave
		}
// Debug output
//		printf("Disk_FTP_SelectImage: we got next:\n");
//		printf("isdir=%d, findex=%d, filename=%s\n", isdir, findex, filename);
// --
		if(isdir)
		{
			if(!strcmp(filename, ".."))
			// go to the upper directory
			{
				filename = strrchr(fullpath, FTP_SEPARATOR); // look for last '/'
				if(filename) {
					*filename = '\0';	// cut it off
					filename = strrchr(fullpath, FTP_SEPARATOR); // look for last '/'
					if(filename) *(++filename) = '\0';	// leave it on the place
				}
				if(strlen(fullpath) == 0) strcpy(fullpath,"/");	//we don't want fullpath to be empty
				findex = dirdx;	// restore
			}
			else
			{
				if(strcmp(fullpath, "/")) snprintf(tmppath, MAX_PATH, "%s%s/", fullpath, filename); // next dir
					else snprintf(tmppath, MAX_PATH, "/%s/", filename);
				strcpy(fullpath, tmppath);	// got ot anew
//		printf("Disk_FTP_SelectImage: we build %s\n", tmppath);
				dirdx = findex; // store it
				findex = 0;	// start with beginning of dir
			}
		}/* if isdir */
	} /* while isdir */
	// we chose some file
	strcpy(g_sFTPServer, fullpath);
	RegSaveString(TEXT("Preferences"),REGVALUE_FTP_DIR, 1, g_sFTPServer);// save it

	snprintf(tmppath, MAX_PATH, "%s/%s", fullpath, filename);
	strcpy(fullpath, tmppath); // fullpath - full path to file on FTP server

	snprintf(tmppath, MAX_PATH, "%s/%s", g_sFTPLocalDir, filename); // local path for file

	int error;

// One moment - if we have some file with the same name
// we do not want to download it again
#ifndef _WIN32
	if(stat(tmppath,&info) == 0) {
		error = 0; // use this file
	}
	else {
		error = ftp_get(fullpath, tmppath); // 0 on success
	}
#else
// using WIN32 method
	if(GetFileAttributes(tmppath) != DWORD(-1)) error = 0;
		else error = ftp_get(fullpath, tmppath); // 0 on success
#endif
	
	if(!error) {
	     error = DiskInsert(drive, tmppath, 0, 1);// try to insert downloaded file as a disk image
	     if (!error)
	     { 
	/*
		if(drive == 0)
			RegSaveString(TEXT("Preferences"),REGVALUE_DISK_IMAGE1, 1, fullpath);
		else
			RegSaveString(TEXT("Preferences"),REGVALUE_DISK_IMAGE2, 1, fullpath);
	*/
	     }
	     else	     
	     {
		  	DiskNotifyInvalidImage(filename,error); // show error on the screen (or in console for our case)
	     }
	}
	else printf("Error downloading file %s\n",tmppath);
	
     backdx = findex;	//store cursor position
     DrawFrameWindow();
}
//===========================================================================

static BYTE DiskSetLatchValue (WORD, WORD, BYTE write, BYTE value, ULONG) {
  if (write)
    floppylatch = value;
  return floppylatch;
}

//===========================================================================
static BYTE DiskSetReadMode (WORD, WORD, BYTE, BYTE, ULONG) {
  floppywritemode = 0;
  return MemReturnRandomData(g_aFloppyDisk[currdrive].writeprotected);
}

//===========================================================================
static BYTE DiskSetWriteMode (WORD, WORD, BYTE, BYTE, ULONG) {
  floppywritemode = 1;
  BOOL modechange = !g_aFloppyDisk[currdrive].writelight;
  g_aFloppyDisk[currdrive].writelight = 20000;
  if (modechange)
    FrameRefreshStatus(DRAW_LEDS);
  return MemReturnRandomData(1);
}

//===========================================================================
void DiskUpdatePosition (DWORD cycles) {
  int loop = 2;
  while (loop--) {
    Disk_t * fptr = &g_aFloppyDisk[loop];
    if (fptr->spinning && !floppymotoron) {
      if (!(fptr->spinning -= MIN(fptr->spinning,(cycles >> 6))))
        FrameRefreshStatus(DRAW_LEDS);
    }
    if (floppywritemode && (currdrive == loop) && fptr->spinning)
      fptr->writelight = 20000;
    else if (fptr->writelight) {
      if (!(fptr->writelight -= MIN(fptr->writelight,(cycles >> 6))))
        FrameRefreshStatus(DRAW_LEDS);
    }
    if ((!enhancedisk) && (!diskaccessed) && fptr->spinning) {
      needsprecision = cumulativecycles;
      fptr->byte += (cycles >> 5);
      if (fptr->byte >= fptr->nibbles)
        fptr->byte -= fptr->nibbles;
    }
  }
  diskaccessed = 0;
}

//===========================================================================

bool DiskDriveSwap()
{
	char s_title[MAX_DISK_IMAGE_NAME + 32];	// for title changing
	// Refuse to swap if either Disk][ is active
	if(g_aFloppyDisk[0].spinning || g_aFloppyDisk[1].spinning)
		return false;

	// Swap disks between drives
	Disk_t  temp;

	// Swap trackimage ptrs (so don't need to swap the buffers' data)
	// TODO: Array of Pointers: Disk_t* g_aDrive[]
	memcpy(&temp            , &g_aFloppyDisk[0], sizeof(Disk_t ));
	memcpy(&g_aFloppyDisk[0], &g_aFloppyDisk[1], sizeof(Disk_t ));
	memcpy(&g_aFloppyDisk[1], &temp            , sizeof(Disk_t ));
// change title
	snprintf(s_title, MAX_DISK_IMAGE_NAME + 32, "%s - %s", g_pAppTitle, g_aFloppyDisk[0].imagename);
	SDL_WM_SetCaption(s_title, g_pAppTitle);// change caption just for drive 0 (leading)

	FrameRefreshStatus(DRAW_LEDS | DRAW_BUTTON_DRIVES);

	return true;
}

//===========================================================================

//static BYTE Disk_IORead(WORD pc, BYTE addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);
//static BYTE Disk_IOWrite(WORD pc, BYTE addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft);

void DiskLoadRom(LPBYTE pCxRomPeripheral, UINT uSlot)
{
	const UINT DISK2_FW_SIZE = 256;

// 	HRSRC hResInfo = FindResource(NULL, MAKEINTRESOURCE(IDR_DISK2_FW), "FIRMWARE");
// 	if(hResInfo == NULL)
// 		return;
//
// 	DWORD dwResSize = SizeofResource(NULL, hResInfo);
// 	if(dwResSize != DISK2_FW_SIZE)
// 		return;
//
// 	HGLOBAL hResData = LoadResource(NULL, hResInfo);
// 	if(hResData == NULL)
// 		return;

// #define IDR_DISK2_FW	"Disk2.rom"
// 	char BUFFER[DISK2_FW_SIZE];
// 	FILE * hdfile = NULL;
// 	hdfile = fopen(IDR_DISK2_FW, "rb");
// 	if(hdfile == NULL) return; // no file?
// 	UINT nbytes = fread(BUFFER, 1, DISK2_FW_SIZE, hdfile);
// 	fclose(hdfile);
// 	if(nbytes != DISK2_FW_SIZE) return; // have not read enough?

	BYTE* pData = (BYTE*) Disk2_rom;	// NB. Don't need to unlock resource
// 	if(pData == NULL)
// 		return;	//

	memcpy(pCxRomPeripheral + uSlot*256, pData, DISK2_FW_SIZE);

	// TODO/FIXME: HACK! REMOVE A WAIT ROUTINE FROM THE DISK CONTROLLER'S FIRMWARE
	*(pCxRomPeripheral+0x064C) = 0xA9;
	*(pCxRomPeripheral+0x064D) = 0x00;
	*(pCxRomPeripheral+0x064E) = 0xEA;

	//

	RegisterIoHandler(uSlot, Disk_IORead, Disk_IOWrite, NULL, NULL, NULL, NULL);
}

//===========================================================================

/*static*/ BYTE Disk_IORead(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft)
{
	addr &= 0xFF;

	switch (addr & 0xf)
	{
	case 0x0:	return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
	case 0x1:	return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
	case 0x2:	return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
	case 0x3:	return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
	case 0x4:	return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
	case 0x5:	return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
	case 0x6:	return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
	case 0x7:	return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
	case 0x8:	return DiskControlMotor(pc, addr, bWrite, d, nCyclesLeft);
	case 0x9:	return DiskControlMotor(pc, addr, bWrite, d, nCyclesLeft);
	case 0xA:	return DiskEnable(pc, addr, bWrite, d, nCyclesLeft);
	case 0xB:	return DiskEnable(pc, addr, bWrite, d, nCyclesLeft);
	case 0xC:	return DiskReadWrite(pc, addr, bWrite, d, nCyclesLeft);
	case 0xD:	return DiskSetLatchValue(pc, addr, bWrite, d, nCyclesLeft);
	case 0xE:	return DiskSetReadMode(pc, addr, bWrite, d, nCyclesLeft);
	case 0xF:	return DiskSetWriteMode(pc, addr, bWrite, d, nCyclesLeft);
	}

	return 0;
}

/*static*/ BYTE Disk_IOWrite(WORD pc, WORD addr, BYTE bWrite, BYTE d, ULONG nCyclesLeft)
{
	addr &= 0xFF;

	switch (addr & 0xf)
	{
	case 0x0:	return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
	case 0x1:	return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
	case 0x2:	return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
	case 0x3:	return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
	case 0x4:	return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
	case 0x5:	return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
	case 0x6:	return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
	case 0x7:	return DiskControlStepper(pc, addr, bWrite, d, nCyclesLeft);
	case 0x8:	return DiskControlMotor(pc, addr, bWrite, d, nCyclesLeft);
	case 0x9:	return DiskControlMotor(pc, addr, bWrite, d, nCyclesLeft);
	case 0xA:	return DiskEnable(pc, addr, bWrite, d, nCyclesLeft);
	case 0xB:	return DiskEnable(pc, addr, bWrite, d, nCyclesLeft);
	case 0xC:	return DiskReadWrite(pc, addr, bWrite, d, nCyclesLeft);
	case 0xD:	return DiskSetLatchValue(pc, addr, bWrite, d, nCyclesLeft);
	case 0xE:	return DiskSetReadMode(pc, addr, bWrite, d, nCyclesLeft);
	case 0xF:	return DiskSetWriteMode(pc, addr, bWrite, d, nCyclesLeft);
	}

	return 0;
}

//===========================================================================

DWORD DiskGetSnapshot(SS_CARD_DISK2* pSS, DWORD dwSlot)
{
	pSS->Hdr.UnitHdr.dwLength = sizeof(SS_CARD_DISK2);
	pSS->Hdr.UnitHdr.dwVersion = MAKE_VERSION(1,0,0,2);

	pSS->Hdr.dwSlot = dwSlot;
	pSS->Hdr.dwType = CT_Disk2;

	pSS->phases			    = phases; // new in 1.0.0.2 disk snapshots
	pSS->currdrive			= currdrive; // this was an int in 1.0.0.1 disk snapshots
	pSS->diskaccessed		= diskaccessed;
	pSS->enhancedisk		= enhancedisk;
	pSS->floppylatch		= floppylatch;
	pSS->floppymotoron		= floppymotoron;
	pSS->floppywritemode	= floppywritemode;

	for(UINT i=0; i<2; i++)
	{
		strcpy(pSS->Unit[i].szFileName, g_aFloppyDisk[i].fullname);
//		printf("Snapshot disk name = %s\n", pSS->Unit[i].szFileName);
		pSS->Unit[i].track				= g_aFloppyDisk[i].track;
		pSS->Unit[i].phase				= g_aFloppyDisk[i].phase;
		pSS->Unit[i].byte				= g_aFloppyDisk[i].byte;
		pSS->Unit[i].writeprotected		= g_aFloppyDisk[i].writeprotected;
		pSS->Unit[i].trackimagedata		= g_aFloppyDisk[i].trackimagedata;
		pSS->Unit[i].trackimagedirty	= g_aFloppyDisk[i].trackimagedirty;
		pSS->Unit[i].spinning			= g_aFloppyDisk[i].spinning;
		pSS->Unit[i].writelight			= g_aFloppyDisk[i].writelight;
		pSS->Unit[i].nibbles			= g_aFloppyDisk[i].nibbles;

		if(g_aFloppyDisk[i].trackimage)
			memcpy(pSS->Unit[i].nTrack, g_aFloppyDisk[i].trackimage, NIBBLES_PER_TRACK);
		else
			memset(pSS->Unit[i].nTrack, 0, NIBBLES_PER_TRACK);
	}

//	strcpy(pss->fdd_name1, g_aFloppyDisk[0].fullname);
//	strcpy(pss->fdd_name2, g_aFloppyDisk[1].fullname);
	return 0;
}

DWORD DiskSetSnapshot(SS_CARD_DISK2* pSS, DWORD /*dwSlot*/)
{
	if(pSS->Hdr.UnitHdr.dwVersion > MAKE_VERSION(1,0,0,2))
    {
	    return -1;
    }
	phases  		= pSS->phases; // new in 1.0.0.2 disk snapshots
	currdrive		= pSS->currdrive; // this was an int in 1.0.0.1 disk snapshots
	diskaccessed	= pSS->diskaccessed;
	enhancedisk		= pSS->enhancedisk;
	floppylatch		= pSS->floppylatch;
	floppymotoron	= pSS->floppymotoron;
	floppywritemode	= pSS->floppywritemode;

	for(UINT i=0; i<2; i++)
	{
		bool bImageError = false;

		ZeroMemory(&g_aFloppyDisk[i], sizeof(Disk_t ));
		if(pSS->Unit[i].szFileName[0] == 0x00)
			continue;

		//
// Let us use POSIX stat of file
//		struct stat buf;
//		int result = stat(pSS->Unit[i].szFileName, &buf);

		//DWORD dwAttributes = GetFileAttributes(pSS->Unit[i].szFileName);
// 		if(/*dwAttributes == INVALID_FILE_ATTRIBUTES*/ result == -1)
// 		{
// 			// Get user to browse for file
// 			DiskSelectImage(i, pSS->Unit[i].szFileName);
// 			result = stat(pSS->Unit[i].szFileName, &buf);
// 			//dwAttributes = GetFileAttributes(pSS->Unit[i].szFileName);
// 		}
//
// 		if(/*dwAttributes != INVALID_FILE_ATTRIBUTES*/ result == 0)
// 		{
// 			BOOL bWriteProtected = (/*dwAttributes & FILE_ATTRIBUTE_READONLY*/ buf.st_mode & S_IWUSR) ? /*TRUE : FALSE*/FALSE : TRUE;
//
// 			if(DiskInsert(i, pSS->Unit[i].szFileName, bWriteProtected, 0))
// 				bImageError = true;
//
// 			// DiskInsert() sets up:
// 			// . fullname
// 			// . imagename
// 			// . writeprotected
// 		}

		if(DiskInsert(i, pSS->Unit[i].szFileName, 0, 0)) {
			bImageError = true;
		}
		//

//		strcpy(g_aFloppyDisk[i].fullname, pSS->Unit[i].szFileName);
		g_aFloppyDisk[i].track				= pSS->Unit[i].track;
		g_aFloppyDisk[i].phase				= pSS->Unit[i].phase;
		g_aFloppyDisk[i].byte				= pSS->Unit[i].byte;
//		g_aFloppyDisk[i].writeprotected	= pSS->Unit[i].writeprotected;
		g_aFloppyDisk[i].trackimagedata	= pSS->Unit[i].trackimagedata;
		g_aFloppyDisk[i].trackimagedirty	= pSS->Unit[i].trackimagedirty;
		g_aFloppyDisk[i].spinning			= pSS->Unit[i].spinning;
		g_aFloppyDisk[i].writelight		= pSS->Unit[i].writelight;
		g_aFloppyDisk[i].nibbles			= pSS->Unit[i].nibbles;

		//

		if(!bImageError)
		{
			if((g_aFloppyDisk[i].trackimage == NULL) && g_aFloppyDisk[i].nibbles)
				AllocTrack(i);

			if(g_aFloppyDisk[i].trackimage == NULL)
				bImageError = true;
			else
				memcpy(g_aFloppyDisk[i].trackimage, pSS->Unit[i].nTrack, NIBBLES_PER_TRACK);
		}

		if(bImageError)
		{
			g_aFloppyDisk[i].trackimagedata	= 0;
			g_aFloppyDisk[i].trackimagedirty	= 0;
			g_aFloppyDisk[i].nibbles			= 0;
		}
	}

	FrameRefreshStatus(DRAW_LEDS | DRAW_BUTTON_DRIVES);

	return 0;
}
