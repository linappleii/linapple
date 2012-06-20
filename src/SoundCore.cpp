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

/* Description: Core sound related functionality
 *
 * Author: Tom Charlesworth
 */

/*	Adaption for Linux+SDL done by beom beotiger. Peace! LLL */

#include "stdafx.h"

// for Assertion
#include <assert.h>



bool g_bDSAvailable = false;

// forward decls    ------------------------
void SDLSoundDriverUninit();		// for DSInit()
bool SDLSoundDriverInit(unsigned wantedFreq, unsigned wantedSamples);	// for DSUninit?

bool DSInit()
{
	if(g_bDSAvailable) return true;	// do not need to repeat all process?? --bb
//	const DWORD SPKR_SAMPLE_RATE = 44100; - defined in Common.h
	g_bDSAvailable = SDLSoundDriverInit(SPKR_SAMPLE_RATE, 4096);// I just do not know what number of samples use.
	return g_bDSAvailable;	//
}

//-----------------------------------------------------------------------------

void DSUninit()
{
	if(!g_bDSAvailable)
		return;
	SDLSoundDriverUninit();	// using code from OpenMSX
//	SDL_CloseAudio();
}


void SoundCore_SetFade(int how)	//
{
	if(how == FADE_OUT) SDL_PauseAudio(1);	//stop playing sound
	else SDL_PauseAudio(0);	//start playing
}

///////////////////////////////////////////////////////////////////////////////////
/////////////////////////  Code from OpenMSX  (http://openmsx.sourceforge.net) ////
///////////////////////////////////////////////////////////////////////////////////
// Definitions

void mute();
void unmute();

unsigned getFrequency();
unsigned getSamples();

double uploadBuffer(short* buffer, unsigned len);

void reInit();
unsigned getBufferFilled();
unsigned getBufferFree();
static void audioCallbackHelper(void* userdata, BYTE* strm, int len);
void audioCallback(short* stream, unsigned len);

unsigned frequency;

short* mixBuffer;
short* mockBuffer;

unsigned fragmentSize;
unsigned bufferSize;
unsigned readIdx, writeIdx, readIdx2, writeIdx2;
double filledStat; /**< average filled status, 1.0 means filled exactly
		the right amount, less than 1.0 mean under
		filled, more than 1.0 means overfilled. */
bool muted;


///////////////////  Main part //////////////////////
bool SDLSoundDriverInit(unsigned wantedFreq, unsigned wantedSamples)
{
	SDL_AudioSpec desired;
	desired.freq     = wantedFreq;
	desired.samples  = wantedSamples;

	desired.channels = 2; // stereo(2)	or mono(1)
// be courteous with BIG_Endian systems, please! --bb
#if ( SDL_BYTEORDER == SDL_BIG_ENDIAN )
	desired.format = AUDIO_S16MSB;
#else
	desired.format = AUDIO_S16LSB;
#endif

	desired.callback = audioCallbackHelper; // must be a static method
	desired.userdata = NULL;
/*
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		printf("Unable to initialize SDL audio subsystem: %s", SDL_GetError());
		return false;
	}
*/
	SDL_AudioSpec audioSpec;
	if (SDL_OpenAudio(&desired, &audioSpec) != 0) {
//		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		printf("Unable to open SDL audio: %s", SDL_GetError());
		return false;
	}
	//std::cerr << "DEBUG wanted: " << wantedSamples
	//          <<     "  actual: " << audioSpec.size / 4 << std::endl;
	frequency = audioSpec.freq;
	bufferSize = 4 * (audioSpec.size / sizeof(short));
//	bufferSize = SPKR_SAMPLE_RATE * 2 * sizeof(short);	// 1 second of stereo short data

/*
	fragmentSize = 256;
	while ((bufferSize / fragmentSize) >= 32) {
		fragmentSize *= 2;
	}
	while ((bufferSize / fragmentSize) < 8) {
		fragmentSize /= 2;
	}
*/
	mixBuffer  = new short[bufferSize];	// buffer for Apple2 speakers
	mockBuffer = new short[bufferSize];	// buffer for Mockingboard

	reInit();
	printf("SDL_MIX_MAXVOLUME=%d\n",SDL_MIX_MAXVOLUME);
	printf("Freq=%d,format=%d,channels=%d,silence=%d\n",
				audioSpec.freq,audioSpec.format,audioSpec.channels,audioSpec.silence);
	printf("samples=%d,size=%d,bufferSize=%d\n",audioSpec.samples,audioSpec.size,bufferSize);
	
//	SDL_PauseAudio(0);
	return true;
}

void SDLSoundDriverUninit()
{
	delete[] mixBuffer;
	delete[] mockBuffer;

	SDL_CloseAudio();
//	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}


void reInit()
{
	memset(mixBuffer, 0, bufferSize * sizeof(short));
	memset(mockBuffer, 0, bufferSize * sizeof(short));
	readIdx  = readIdx2 = 0;
	writeIdx = writeIdx2 = 0;//(5 * bufferSize) / 8;
	filledStat = 1.0;
}

void mute()
{
	if (!muted) {
		muted = true;
		SDL_PauseAudio(1);
	}
}

void unmute()
{
	if (muted) {
		muted = false;
		reInit();
		SDL_PauseAudio(0);
	}
}

unsigned getFrequency()
{
	return frequency;
}

unsigned getSamples()
{
	return fragmentSize;
}

void audioCallbackHelper(void* userdata, BYTE* strm, int len)
{
	assert((len & 3) == 0); // stereo, 16-bit
	audioCallback((short*)strm, len / sizeof(short));
}

unsigned getBufferFilled()
{
	int tmp = writeIdx - readIdx;
	int result = (0 <= tmp) ? tmp : tmp + bufferSize;
	assert((0 <= result) && (unsigned(result) < bufferSize));
	return result;
}

unsigned getBufferFree()
{
	// we can't distinguish completely filled from completely empty
	// (in both cases readIx would be equal to writeIdx), so instead
	// we define full as '(writeIdx + 2) == readIdx' (note that index
	// increases in steps of 2 (stereo)).
	int result = bufferSize - 2 - getBufferFilled();
	assert((0 <= result) && (unsigned(result) < bufferSize));
	return result;
}
//////////////////////////////////////////////////////////////////
//////// for Mockingboard support using another buffer //////////
//////////////////////////////////////////////////////////////////
unsigned getBuffer2Filled()
{
	int tmp = writeIdx2 - readIdx2;
	int result = (0 <= tmp) ? tmp : tmp + bufferSize;
	assert((0 <= result) && (unsigned(result) < bufferSize));
	return result;
}

unsigned getBuffer2Free()
{
	// we can't distinguish completely filled from completely empty
	// (in both cases readIx would be equal to writeIdx), so instead
	// we define full as '(writeIdx + 2) == readIdx' (note that index
	// increases in steps of 2 (stereo)).
	int result = bufferSize - 2 - getBuffer2Filled();
	assert((0 <= result) && (unsigned(result) < bufferSize));
	return result;
}
//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

void audioCallback(short* stream, unsigned len)
{
	int i;
    static short lastvalue = 0;
    
	assert((len & 1) == 0); // stereo
	unsigned len1, len2;
	unsigned available = getBufferFilled();
	
	//std::cerr << "DEBUG callback: " << available << std::endl;
	unsigned num = std::min(len, available);
	if ((readIdx + num) < bufferSize) {
		memcpy(stream, &mixBuffer[readIdx], num * sizeof(short));
		readIdx += num;
	} else {
		len1 = bufferSize - readIdx;
		memcpy(stream, &mixBuffer[readIdx], len1 * sizeof(short));
		len2 = num - len1;
		memcpy(&stream[len1], mixBuffer, len2 * sizeof(short));
		readIdx = len2;
	}

    if (available != 0) {
        if (readIdx != 0)
            lastvalue = mixBuffer[readIdx-1];
        else
            lastvalue = mixBuffer[bufferSize - 1];
	}

    for (i = num; i < len; i++) {
        stream[i] = lastvalue; 
    }

/* Note: cleared at the begininng of the func

	int missing = len - available;
	if (missing > 0) {
		// buffer underrun
		//std::cerr << "DEBUG underrun: " << missing << std::endl;
		memset(&stream[available], 0, missing * sizeof(short));
	}
*/
	unsigned target = (5 * bufferSize) / 8;
	double factor = double(available) / target;
	filledStat = (63 * filledStat + factor) / 64;
	//std::cerr << "DEBUG filledStat: " << filledStat << std::endl;
#ifdef MOCKINGBOARD
// And add Mockingboard sound data to the stream
	available = getBuffer2Filled();
	//std::cerr << "DEBUG callback: " << available << std::endl;
	num = std::min(len, available);
	if ((readIdx2 + num) < bufferSize) {
//		memcpy(stream, &mixBuffer[readIdx], num * sizeof(short));
		for(i = 0; i < num; i++)
			stream[i] |= mockBuffer[readIdx2 + i];
		readIdx2 += num;
	} else {
		len1 = bufferSize - readIdx2;
//		memcpy(stream, &mixBuffer[readIdx], len1 * sizeof(short));
		for(i = 0; i < len1; i++)
			stream[i] |= mockBuffer[readIdx2 + i];
		len2 = num - len1;
//		memcpy(&stream[len1], mixBuffer, len2 * sizeof(short));
		for(i = 0; i < len2; i++)
			stream[len1 + i] |= mockBuffer[i];
		readIdx2 = len2;
	}
#endif
// normalization
/*	const short MY_MAX_VOLUME = (short)SDL_MIX_MAXVOLUME / 2;
	for(unsigned k=0;k<len;k+=2)
		if((short)stream[k] > MY_MAX_VOLUME)
			stream[k] = MY_MAX_VOLUME;
*/
}  // audioCallback

double DSUploadBuffer(short* buffer, unsigned len)
{
	SDL_LockAudio();
//	len *= 2; // stereo
	unsigned free = getBufferFree();
	if (len > free) {
//		std::cerr << "DEBUG overrun: " << len - free << std::endl;
		printf("DEBUG overrun: len=%d,free=%d\n\n", len, free);
	}
	unsigned num = std::min(len, free); // ignore overrun (drop samples)
	if ((writeIdx + num) < bufferSize) {
		memcpy(&mixBuffer[writeIdx], buffer, num * sizeof(short));
		writeIdx += num;
	} else {
//		printf("DEBUG split: writeIdx=%d,num=%d\n\n", writeIdx, num);
		unsigned len1 = bufferSize - writeIdx;
		memcpy(&mixBuffer[writeIdx], buffer, len1 * sizeof(short));
		unsigned len2 = num - len1;
		memcpy(mixBuffer, &buffer[len1], len2 * sizeof(short));
		writeIdx = len2;
	}

	//unsigned available = getBufferFilled();
	//std::cerr << "DEBUG upload: " << available << " (" << len << ")" << std::endl;
	double result = filledStat;
	filledStat = 1.0; // only report difference once
	SDL_UnlockAudio();
	return result;
}

///// Uploading sound data for Mockingboard buffer
void /*double*/ DSUploadMockBuffer(short* buffer, unsigned len)
{
	SDL_LockAudio();
//	len *= 2; // stereo
	unsigned free = getBuffer2Free();
	//if (len > free) {
	//	std::cerr << "DEBUG overrun: " << len - free << std::endl;
	//}
	unsigned num = std::min(len, free); // ignore overrun (drop samples)
	if ((writeIdx2 + num) < bufferSize) {
		memcpy(&mockBuffer[writeIdx2], buffer, num * sizeof(short));
		writeIdx2 += num;
	} else {
		unsigned len1 = bufferSize - writeIdx2;
		memcpy(&mockBuffer[writeIdx2], buffer, len1 * sizeof(short));
		unsigned len2 = num - len1;
		memcpy(mockBuffer, &buffer[len1], len2 * sizeof(short));
		writeIdx2 = len2;
	}

	SDL_UnlockAudio();
//	return 1.0;	// do not use result?
}

