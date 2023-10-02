
#include <stdio.h>
#include <cassert>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include "dataring.h"

#define DATARING_DEBUG
//#define DATARING_DEBUG2		// requires libkaty

#ifdef DATARING_DEBUG
	#define debug(...)	do {	\
	printf(__VA_ARGS__);	\
	printf("\n");	\
} while(0)
#else
	#define debug(...)	do { } while(0)
#endif

#define error(...)	do {	\
	printf(__VA_ARGS__);	\
	printf("\n");	\
} while(0)

DataRing::DataRing(int ring_initial_size) {
	fHead = fTail = 0;
	fBufferBase = NULL;
	fBuffer = NULL;
	fPreBuffer = NULL;
	fPostBuffer = NULL;
	fRingSize = 0;

	if (ring_initial_size)
		SetRingSize(ring_initial_size);
}

DataRing::~DataRing() {
	free(fBufferBase);
	fBufferBase = NULL;
	fBuffer = NULL;
	fPreBuffer = fPostBuffer = NULL;
}

void DataRing::SetRingSize(int newSize) {
	// need one extra byte because we must always leave at least one byte
	// unused to tell the difference between an empty and full buffer
	newSize++;

	if (newSize != fRingSize || fBuffer == NULL) {
		fHead = fTail = 0;

		// we'll allocate space before and after the ring for when we need to
		// temporarily reassemble wrapped chunks to make them contiguous.
		// worst case amount we'll need to copy is half the size of the ring
		// buffer. think of it like a literal circular paper tape that you're
		// cutting to extend into a solid line. the most you'll ever need to
		// cut is half the circle, because any longer than that and you'd just
		// cut the other end.
		//
		// note: factored out the +/-1's in "(((newSize - 1) + 1) & ~1) / 2"
		// 		(newSize - 1)		remove the byte we added earlier for full/empty disambiguation
		//		+1 & ~1				round up to nearest even integer before division / 2
		//		>> 1				will at most need half that much
		fReassemblySize = (newSize & ~1) >> 1;
		fRingSize = newSize;
		fBufTotalSize = fReassemblySize + fRingSize + fReassemblySize;

		// ensure that the main ring buffer ends up word-aligned for speed --
		// usually, malloc would probably do this for us, but it doesn't know
		// about our extra reassembly padding, so let's hold it's hand a little
		int wordAlignPad = 0;
		int ringBufferOffset = fReassemblySize;
		if (ringBufferOffset & 3)
			wordAlignPad = 4 - (ringBufferOffset & 3);

		int bufAllocSize = fBufTotalSize + wordAlignPad;

		debug("setting fRingSize = %d; fReassemblySize = %d",
				fRingSize, fReassemblySize);
		debug("fBufTotalSize = %d(%d); wordAlignPad = %d",
				fBufTotalSize, bufAllocSize, wordAlignPad);

		free(fBufferBase);
		fBufferBase = (uint8_t *)malloc(bufAllocSize);

		fPreBuffer = fBufferBase + wordAlignPad;
		fBuffer = fPreBuffer + fReassemblySize;
		fPostBuffer = fBuffer + fRingSize;

		memset(fPostBuffer, 0xff, fReassemblySize);
		memset(fBuffer, 0xee, fRingSize);
		memset(fPreBuffer, 0xff, fReassemblySize);

		// debug markers at boundaries
		//#ifndef CONFIG_RELEASE
		//fPreBuffer[0] = 0x10; fPreBuffer[fReassemblySize - 1] = 0x19;
		//fBuffer[0] = 0x20; fBuffer[fRingSize - 1] = 0x29;
		//fPostBuffer[0] = 0x30; fPostBuffer[fReassemblySize - 1] = 0x39;
		//#endif
	}
}

void DataRing::Clear() {
	fHead = fTail;
}

///////////////////////////////////////////////////////////////////////////////

int DataRing::Append(const void *datain, int dataLength)
{
	const uint8_t *data = (const uint8_t *)datain;
	if (dataLength <= 0) {
		error("received invalid dataLength %d", dataLength);
		return -1;
	}

	int bytesFree = BytesFree();
	if (dataLength > bytesFree) {
		error("attempt to append %d bytes into ring "
				"when only %d %s available",
				dataLength, bytesFree,
				(bytesFree == 1) ? "is" : "are");
		error("buffer contents looks like:");
		Dump();
		return -1;
	}

	// copy bytes into the buffer, wrapping if necessary
	// first get how many bytes we can push before we'd reach the end and
	// have to wrap
	int bytesToEnd = (fRingSize - fTail);
	debug("insert %d bytes, bytesToEnd = %d at head %d, tail %d (before insertion)",
			dataLength, bytesToEnd, fHead, fTail);
	
	if (dataLength <= bytesToEnd)
	{	// normal simple insertion
		memcpy(fBuffer + fTail, data, dataLength);

		// even though the data isn't wrapped, the tail can still wrap around
		// in some scenarios (e.g. tail = 1, dataLength = ringSize) so we have
		// to check. this is because the tail points at the byte AFTER the end;
		// which makes sense as (head == tail) means empty/0.
		fTail += dataLength;
		if (fTail >= fRingSize) fTail -= fRingSize;
	}
	else
	{	// the tail is advanced to a point where we need to put some of the
		// inserted data at the tail, and the rest should "wrap around" to
		// the beginning of the ring buffer
		debug("performing wrapped insertion: %d bytes at tail, %d at start",
				bytesToEnd, dataLength - bytesToEnd);

		memcpy(fBuffer + fTail, data, bytesToEnd);
		memcpy(fBuffer, data + bytesToEnd, dataLength - bytesToEnd);
	
		// the formula is actually (tail + datalen) % bufferSize, but we
		// already KNOW it will wrap. so we can do the clamp unconditionally.
		fTail = (fTail + dataLength) - fRingSize;
	}
	
	debug("added %d bytes; head=%d/tail=%d; now holding %d bytes",
			dataLength, fHead, fTail, BytesAvail());

	return 0;
}

int DataRing::Prepend(const void *datain, int dataLength) {
	const uint8_t *data = (const uint8_t *)datain;

	int bytesFree = BytesFree();
	if (dataLength > bytesFree) {
		error("attempt to prepend %d bytes into ring "
				"when only %d %s available",
				dataLength, bytesFree,
				(bytesFree == 1) ? "is" : "are");
		return -1;
	}

	debug("Prepending %d bytes", dataLength);

	fHead -= dataLength;
	if (fHead >= 0)
	{	// easy case, so just copy it in now that we've "made room"
		memcpy(fBuffer + fHead, data, dataLength);
	}
	else
	{
		fHead += fRingSize;		// clamp back in range

		// wrapped case. so we need to copy some bytes to wherever
		// the NEW head is, and then the rest to the beginning. this actually
		// works out just like the Append case, except for using the head
		// instead of tail and the fact that we moved the head first
		int bytesToEnd = (fRingSize - fHead);
		debug("got bytesToEnd %d", bytesToEnd);
		memcpy(fBuffer + fHead, data, bytesToEnd);
		memcpy(fBuffer, data + bytesToEnd, dataLength - bytesToEnd);
	}

	return 0;
}

// moves the head/tail pointers as they should be after a Read().
// normally done for you but broken out so you can do this separately if you
// are wanting to "peek" at the data to see if you want it before definitely
// removing it.
void DataRing::Commit(int readLen)
{
	// remove the requested # of bytes from the buffer
	fHead += readLen;
	if (fHead >= fRingSize) fHead -= fRingSize;

	// if the buffer is now empty, we might as well reset the head/tail;
	// with a big enough buffer this should it make it way less common
	// that we need to wrap under normal conditions
	if (fHead == fTail)
		fHead = fTail = 0;
}

// see Read().
// allows to drop the bounds checking when doing ReadChunk(), which will
// have already tested that there's enough bytes waiting.
uint8_t *DataRing::ReadDirect(int readLen, bool commit)
{
	debug("ReadDirect: request for %d bytes", readLen);

	// get the number of bytes from the head to the end of buffer.
	// this would be ((fRingSize - 1) - fHead) + 1, but we can just
	// factor out the ones.
	int bytesToEnd = (fRingSize - fHead);
	uint8_t *headptr = fBuffer + fHead;
	//trace("bytes from head to end of buffer = %d", bytesToEnd);

	// move the head pointer
	if (commit)
		Commit(readLen);

	// simple most-common case: is the requested data already contigous,
	// as it doesn't cross the edges of the "ring"? if so, we can just give
	// them a pointer directly to the correct offset in our buffer
	if (readLen <= bytesToEnd) {
		debug("common case");
		return headptr;
	}

	// TODO: if the previous request was a peek at or a subset of this request
	// and nothing's been added since, we could re-use the already-copied
	// portion. this usage pattern does occur for example in PacketDecoder.

	// else the requested # of bytes are "wrapped" around the end of the buffer;
	// we'll need to reassemble it into a contiguous whole for the return.
	debug("reassembling; piece sizes %d and %d", bytesToEnd, readLen - bytesToEnd);

	// there's two ways we can assemble the requested number of bytes to a
	// contiguous buffer: we can either copy the part at the beginning of the
	// ring to the pad space past the end, or copy the part near the end to
	// before the beginning. determine which one makes more sense.
	#define preWrapCopySize	   bytesToEnd
	int		postWrapCopySize = (readLen - bytesToEnd);

	if (postWrapCopySize < preWrapCopySize)
	{
		#define buffer_end	(fPreBuffer + fBufTotalSize)
		assert(fPostBuffer + (postWrapCopySize - 1) < buffer_end);
		
		// copy bytes from the beginning to the end, then return the head
		memcpy(fPostBuffer, fBuffer, postWrapCopySize);
		return headptr;
	}
	else
	{
		// copy bytes from the head to before the beginning
		uint8_t *preCopyPtr = (fBuffer - preWrapCopySize);
		assert(preCopyPtr >= fPreBuffer);
	
		memcpy(preCopyPtr, headptr, preWrapCopySize);
		return preCopyPtr;
	}

#undef preWrapCopySize
#undef buffer_lastbyte
}

// read [readLen] bytes and remove them from the buffer, returning a pointer
// to them (which will be contiguous even if they were stored wrapped).
// NOTE: you should have already checked that there are enough bytes available.
uint8_t *DataRing::Read(int readLen, bool commit)
{
	assert(readLen >= 0);
	assert(readLen <= BytesAvail());
	return ReadDirect(readLen, commit);
}

// convenience function for reading fixed-sized structures. 
// if there is at least [chunk_size] bytes waiting, read it and return
// a pointer to it. else return NULL and do nothing.
uint8_t *DataRing::ReadChunk(int chunk_size, bool commit) {
	if (BytesAvail() >= chunk_size)
		return ReadDirect(chunk_size, commit);
	else
		return NULL;
}

int DataRing::ReadByte() {
	if (fHead == fTail)
		return -1;

	uint8_t byte = fBuffer[fHead];
	fHead = (fHead + 1) % fRingSize;
	return byte;
}

///////////////////////////////////////////////////////////////////////////////

bool DataRing::IsFull() {
	// when full, the head pointer will be just after the tail (i.e. reversed),
	// as the buffer will necessarily be fully wrapped in order to be full
	//return ((fTail + 1) % fRingSize) == fHead;
	if (fHead == 0)
		return fTail == (fRingSize - 1);
	else
		return fHead == (fTail + 1);
}

///////////////////////////////////////////////////////////////////////////////

int DataRing::isByteInBuffer(int offs) {
	if (offs >= fRingSize) return false;
	if (offs < 0) return false;

	//if (offs >= fHead)
		//return 1;

	if (fTail >= fHead) {
		// "simple" case without wrap
		return (offs >= fHead && offs < fTail) ? 1 : 0;
	}
	else {
		// wrapped
		if (offs >= fHead) return 2;
		if (offs < fTail) return 3;
		return 0;
	}
}

#ifdef DATARING_DEBUG2
void DataRing::Dump() {
	//#define HEADCOL		"\e[1;48;2;238;36;49m"
	//#define TAILCOL		"\e[1;48;2;80;80;238m"
	#define HEADCOL		"\e[1;48;2;138;16;39m"
	#define TAILCOL		"\e[1;48;2;70;70;198m"
	#define ACTIVECOLFG	"\e[3;4m"
	#define ACTIVECOLBG_NOWRAP	"\e[1;48;2;23;77;123m"
	#define ACTIVECOLBG_WRAP1	"\e[1;48;2;132;58;179m"
	#define ACTIVECOLBG_WRAP2	"\e[1;48;2;23;123;77m"
	#define NORM		"\e[0m"

	static const char *bib_to_col_map[] = {
		NULL, ACTIVECOLBG_NOWRAP, ACTIVECOLBG_WRAP1, ACTIVECOLBG_WRAP2
	};

	DString top(stprintf("── DataRing %p ────────────────────────────", this));
	stat("%s", top.String());

	DString line;
	line.Append(stprintf(" HheadN %d", fHead));
	while(line.Length() < 12) line.Append(' ');
	line.Append("TtailN ");
	int l1len = line.Length() - 4;
	line.Append(stprintf("%d", fTail));

	#define C	22
	while(line.Length() - 4 < C) line.Append(' ');

	line.Append(stprintf("bytesInBuffer %d", BytesAvail()));
	if (IsFull()) {
		#define FEC			42
		while(line.Length() - 4 < FEC) line.Append(' ');
		line.Append("\e[1;91mFULL\e[0m");
	}
	
	line.ReplaceString("H", HEADCOL);
	line.ReplaceString("T", TAILCOL);
	line.ReplaceString("N", NORM);

	stat("%s", line.String());

	line.Clear();
	line.Append(" bufferSize");
	int M = l1len - 1;
	while(line.Length() < M) line.Append(' ');
	line.Append(stprintf(" %d", fRingSize));
	while(line.Length() < C) line.Append(' ');
	line.Append(stprintf("bytesFree     %d", BytesFree()));
	if (IsEmpty()) {
		while(line.Length() < FEC) line.Append(' ');
		line.Append("\e[1;92mEMPTY\e[0m");
	}
	stat("%s", line.String());

	//htstack.Dump();

	// ----------------
	DString dump;
	int linelen = 0;

	#define PRINT_ADDR(A)	do { dump.Append(stprintf("%s%04x ", (A<0)?"-":" ", abs(A))); } while(0)
	#define DUMP_BUFFER()	do {	\
		if (linelen) {	\
			dump.Append(line);	\
			dump.Append('\n');	\
		}				\
		line.Clear();	\
		linelen = 0;	\
	} while(0)

	line.Clear();

	#define SHOW_REASSEMBLY
	#ifdef SHOW_REASSEMBLY
		//error("%d", fReassemblySize);exit(1);
		uint8_t *ptr = fPreBuffer;
		uint8_t *lastbyte = fPreBuffer + (fBufTotalSize - 1);
	#else
		uint8_t *ptr = fBuffer;
		uint8_t *lastbyte = fBuffer + (fRingSize - 1);
	#endif

	int lineCounter = 0;
	const char *color = NULL;
	const char *nextSpace = " ";
	while(ptr <= lastbyte)
	{
		bool inNormalRing = (ptr >= fBuffer && ptr < fPostBuffer);
		int offs = (ptr - fBuffer);

		if (lineCounter == 0) {
			DUMP_BUFFER();
			PRINT_ADDR(offs);
		}
		if (++lineCounter >= 16)
			lineCounter = 0;
	

		char num[16];
		//sprintf(num, "%02x", abs(ptr - fBuffer));
		sprintf(num, "%02x", *ptr);
	
		#define GREY		"\e[1;38;2;120;40;40m"
		if (offs == fHead) nextSpace = GREY "{" NORM;
		line.Append((fHead == fTail) ? " " : nextSpace); nextSpace = " ";
		if (offs == fHead) line.Append(color = HEADCOL);
		else if (offs == fTail) line.Append(color = TAILCOL);

		int inbuftype;
		if ((inbuftype = isByteInBuffer(offs))) {
			line.Append(ACTIVECOLFG);
			if (!color) {
				color = bib_to_col_map[inbuftype];
				if (color) line.Append(color);
			}
		}

		if (!inNormalRing)
			line.Append(color = GREY);

		line.Append(num[0]);
		if (offs == fTail) line.Append(color = TAILCOL);

		if (inNormalRing) {
			if ((offs + 1) % fRingSize == fTail)
				nextSpace = GREY "}" NORM;
		}

		line.Append(num[1]);
		if (color) { color = NULL; line.Append(NORM); }
		linelen += 3;

		ptr++;
		offs++;
	}

	if (linelen)
		DUMP_BUFFER();

	DString btm("───────────────────────────────────────────────────────");
	stat("%s%s", dump.String(), btm.String());
}
#else	// !DATARING_DEBUG

void DataRing::Dump() {
	printf("DataRing: head=%d tail=%d free=%d avail=%d\n",
			fHead, fTail, BytesFree(), BytesAvail());
}

#endif

