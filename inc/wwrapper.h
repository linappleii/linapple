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


unsigned int SetFilePointer(HANDLE hFile, int lDistanceToMove, PLONG lpDistanceToMoveHigh, unsigned int dwMoveMethod);

bool ReadFile(HANDLE hFile, LPVOID lpBuffer, unsigned int nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead,
              LPOVERLAPPED lpOverlapped);

bool WriteFile(HANDLE hFile, LPCVOID lpBuffer, unsigned int nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten,
               LPOVERLAPPED lpOverlapped);

/* close handle whatever it has been .... hmmmmm. I just love Microsoft! */
bool CloseHandle(HANDLE hObject);

bool DeleteFile(LPCTSTR lpFileName);

unsigned int GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);

LPVOID VirtualAlloc(LPVOID lpAddress, size_t dwSize, unsigned int flAllocationType, unsigned int flProtect);

bool VirtualFree(LPVOID lpAddress, size_t dwSize, unsigned int dwFreeType);

static inline bool IsCharLower(char ch) {
  return isascii(ch) && islower(ch);
}

static inline bool IsCharUpper(char ch) {
  return isascii(ch) && isupper(ch);
}

unsigned int CharLowerBuff(LPTSTR lpsz, unsigned int cchLength);

