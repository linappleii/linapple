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


DWORD SetFilePointer(HANDLE hFile,
       LONG lDistanceToMove,
       PLONG lpDistanceToMoveHigh,
       DWORD dwMoveMethod);

BOOL ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead,
         LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);

BOOL WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
        LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);

 /* close handle whatever it has been .... hmmmmm. I just love Microsoft! */
BOOL CloseHandle(HANDLE hObject);

BOOL DeleteFile(LPCTSTR lpFileName);

DWORD GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);

LPVOID VirtualAlloc(LPVOID lpAddress, size_t dwSize,
    DWORD flAllocationType, DWORD flProtect);

BOOL VirtualFree(LPVOID lpAddress, size_t dwSize, DWORD dwFreeType);


static inline bool IsCharLower(char ch) {
  return isascii(ch) && islower(ch);
}

static inline bool IsCharUpper(char ch) {
  return isascii(ch) && isupper(ch);
}

DWORD CharLowerBuff(LPTSTR lpsz, DWORD cchLength);

