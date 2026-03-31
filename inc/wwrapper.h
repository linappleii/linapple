#include <cstdint>
#include "wincompat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define FILE_BEGIN   SEEK_SET
#define FILE_CURRENT  SEEK_CUR
#define FILE_END  SEEK_END
#define INVALID_HANDLE_VALUE NULL

#define MEM_COMMIT  0x1000
#define PAGE_READWRITE  0
#define MEM_RELEASE  0


unsigned int SetFilePointer(HANDLE hFile, int lDistanceToMove, int32_t* lpDistanceToMoveHigh, unsigned int dwMoveMethod);

bool ReadFile(HANDLE hFile, void* lpBuffer, unsigned int nNumberOfBytesToRead, uint32_t* lpNumberOfBytesRead,
              OVERLAPPED* lpOverlapped);

bool WriteFile(HANDLE hFile, const void* lpBuffer, unsigned int nNumberOfBytesToWrite, uint32_t* lpNumberOfBytesWritten,
               OVERLAPPED* lpOverlapped);

/* close handle whatever it has been .... hmmmmm. I just love Microsoft! */
bool CloseHandle(HANDLE hObject);

bool DeleteFile(const char* lpFileName);

unsigned int GetFileSize(HANDLE hFile, uint32_t* lpFileSizeHigh);

void* VirtualAlloc(void* lpAddress, size_t dwSize, unsigned int flAllocationType, unsigned int flProtect);

bool VirtualFree(void* lpAddress, size_t dwSize, unsigned int dwFreeType);

static inline bool IsCharLower(char ch) {
  return isascii(ch) && islower(ch);
}

static inline bool IsCharUpper(char ch) {
  return isascii(ch) && isupper(ch);
}

unsigned int CharLowerBuff(char* lpsz, unsigned int cchLength);

