/*
 * Wrappers for some common Microsoft file and memory functions used in AppleWin
 *  by beom beotiger, Nov 2007AD
*/

#include "stdafx.h"
#include "wwrapper.h"

unsigned int SetFilePointer(HANDLE hFile, int lDistanceToMove, int32_t* lpDistanceToMoveHigh, unsigned int dwMoveMethod)
{
  /* ummm,fseek in Russian */
  fseek((FILE *) hFile, lDistanceToMove, dwMoveMethod);
  return ftell((FILE *) hFile);
}

bool ReadFile(HANDLE hFile, void* lpBuffer, unsigned int nNumberOfBytesToRead, uint32_t* lpNumberOfBytesRead,
              OVERLAPPED* lpOverlapped)
{

  /* read something from file */
  unsigned int bytesread = fread(lpBuffer, 1, nNumberOfBytesToRead, (FILE *) hFile);
  *lpNumberOfBytesRead = bytesread;
  return (nNumberOfBytesToRead == bytesread);
}

bool WriteFile(HANDLE hFile, const void* lpBuffer, unsigned int nNumberOfBytesToWrite, uint32_t* lpNumberOfBytesWritten,
               OVERLAPPED* lpOverlapped)
{
  /* write something to file */
  unsigned int byteswritten = fwrite(lpBuffer, 1, nNumberOfBytesToWrite, (FILE *) hFile);
  *lpNumberOfBytesWritten = byteswritten;
  return (nNumberOfBytesToWrite == byteswritten);
}

/* close handle whatever it has been .... hmmmmm. I just love Microsoft! */
bool CloseHandle(HANDLE hObject)
{
  return (!fclose((FILE *) hObject));
}

bool DeleteFile(const char* lpFileName)
{
  if (remove(lpFileName) == 0)
    return true;
  else
    return false;
}

unsigned int GetFileSize(HANDLE hFile, uint32_t* lpFileSizeHigh)
{
  /* what is the size of the specified file??? Hmmm, really I donna. ^_^ */
  long lcurset = ftell((FILE *) hFile); // remember current file position

  fseek((FILE *) hFile, 0, FILE_END);  // go to the end of file
  unsigned int lfilesize = ftell((FILE *) hFile); // that is the real size of file, isn't it??
  fseek((FILE *) hFile, lcurset, FILE_BEGIN); // let the file position be the same as before
  return lfilesize;
}

void* VirtualAlloc(void* lpAddress, size_t dwSize, unsigned int flAllocationType, unsigned int flProtect)
{
  /* just malloc and alles? 0_0 */
  void *mymemory;
  mymemory = malloc(dwSize);
  if (flAllocationType & 0x1000)
    memset(mymemory, 0, dwSize); // original VirtualAlloc does this (if..)
  return mymemory;
}

bool VirtualFree(void* lpAddress, size_t dwSize, unsigned int dwFreeType)
{
  free(lpAddress);
  return true;
}

// make all chars in buffer lowercase
unsigned int CharLowerBuff(char* lpsz, unsigned int cchLength)
{
  if (lpsz)
    for (; *lpsz; lpsz++)
      *lpsz = tolower(*lpsz);
  return 1;

}
