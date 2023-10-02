
#ifndef __RINPUT_DATARING_H
#define __RINPUT_DATARING_H

#include <stdlib.h>
#include <stdint.h>

// ring buffer implementation, with ability to have a "min chunk size" when
// reading. this can also be used as a normal ring buffer by just always
// asking to read no more than "BytesAvail()".
//
// this object is designed to help out once and for all with a few common
// patterns that often can be awkward with POSIX read() operations and
// breaking any type of "stream" data into blocks such as for packet decoding
// on a TCP link. It implements a ring buffer of a configurable size, into
// which you can dump arbitrary amounts of data (such as returned by a read()
// call with a large buffer. You can then, without need to call into the
// kernel again, perform operations such as:
//
//	- take data out of the buffer in particular size chunks, or only if there
//    enough data to make an entire chunk
//
//	- determine very quickly how much data is in the buffer
//
//	- peek at data and then "put it back" if you don't want it right now
//
constexpr int DATARING_DEFAULT_SIZE		= 2048;

class DataRing
{
public:
	DataRing(int ring_initial_size = DATARING_DEFAULT_SIZE);
	~DataRing();

	void SetRingSize(int new_size);

	// push/copy data into the ring. returns < 0 on error.
	int Append(const void *data, int len);

	// push data at the beginning of the ring. returns < 0 on error.
	int Prepend(const void *data, int len);

	// read a single byte from the buffer in FIFO order.
	// returns -1 if called while the buffer is empty.
	int ReadByte();

	// read the given number of bytes and return a pointer to them,
	// "unwrapping" the bytes if necessary (i.e. if the start and end were on
	// opposite sides of the ring). to avoid copying, the pointer is located
	// within the object's own ring buffer, so the data should be processed or
	// copied before another read or insertion.
	//
	// if "commit" is false, then the head/tail pointers are not updated, so
	// the data is not actually removed from the buffer. this can be used in
	// combination with the Commit() function to implement a "peek".
	uint8_t *Read(int len, bool commit = true);

	// remove the given number of bytes from the start of the buffer,
	// "committing" a prior "peek" (a Read() with commit=false). You must call
	// this before doing anything else between the Read and the Commit, and
	// the "len" parameter must be equal to what was passed to Read, or
	// bad things will happen.
	void Commit(int len);

	// if there is at least [minSize] bytes available in the ring, read exactly
	// that many bytes (and no more), and return a pointer to the start of it.
	// if there is not at least chunk_size bytes yet, returns NULL.
	uint8_t *ReadChunk(int minSize, bool commit = true);

	const char *ReadLine();

	int BytesAvail();		// max number of bytes you can currently read
	int BytesFree();		// max number of bytes you can currently insert

	bool IsFull();
	inline bool IsEmpty() {
		return (fHead == fTail);
	}

	void Dump();
	void Clear();

private:
	uint8_t *ReadDirect(int len, bool commit);
	int isByteInBuffer(int offs);

private:
	int fHead, fTail;
		
	int fRingSize;			// size of the main ring
	int fReassemblySize;	// how much pad we have before and after the ring for doing reassembly
	int fBufTotalSize;		// pre-buffer + ring + post-buffer = (fRingSize + (fReassemblySize * 2))
	
	uint8_t *fBufferBase;	// actual start of the malloc(), including any word-align padding
	uint8_t *fBuffer;		// start of the ring (embedded between the pre and post sections)
	uint8_t *fPreBuffer;	// actual start of the buffer for reassembly
	uint8_t *fPostBuffer;	// pointer past end of buffer for reassembly

	#ifdef CONFIG_DATARING_TESTS
	friend void dataring_test();
	friend bool dataring_stresstest();
	#endif
};

// return the number of bytes currently stored in the ring buffer
inline int DataRing::BytesAvail()
{
	if (fTail >= fHead)
		return fTail - fHead;				// "normal"
	else
		return (fRingSize - fHead) + fTail;	// "wrapped"
}

// return the maximum number of bytes that could currently be added to the
// buffer without overflowing
inline int DataRing::BytesFree() {
	return (fRingSize - BytesAvail()) - 1;
}

// simple wrapper template which saves you from needing to cast
// (use this when storing structs/packets in the ring).
template<class T>
class DataRingOf : public DataRing {
public:
	DataRingOf(int ring_initial_size = DATARING_DEFAULT_SIZE)
		: DataRing(ring_initial_size)
	{ }

	T *Read(int len) { return (T*)DataRing::Read(len); }
	T *ReadChunk(int minSize, bool commit) { return (T *)DataRing::ReadChunk(minSize, commit); }
	//T *PeekChunk(int minSize) { return (T *)DataRing::PeekChunk(minSize); }

	T *ReadChunk(bool commit) {
		return ReadChunk(sizeof(T), commit);
	}
};

#endif

