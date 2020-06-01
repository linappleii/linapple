/*
 * Wrappers for some common Microsoft file and memory functions used in AppleWin
 *  by beom beotiger, Nov 2007AD
*/

#include "wwrapper.h"

unsigned int SetFilePointer(HANDLE hFile, int lDistanceToMove, PLONG lpDistanceToMoveHigh, unsigned int dwMoveMethod)
{
  /* ummm,fseek in Russian */
  fseek((FILE *) hFile, lDistanceToMove, dwMoveMethod);
  return ftell((FILE *) hFile);
}

bool ReadFile(HANDLE hFile, LPVOID lpBuffer, unsigned int nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead,
              LPOVERLAPPED lpOverlapped)
{

  /* read something from file */
  unsigned int bytesread = fread(lpBuffer, 1, nNumberOfBytesToRead, (FILE *) hFile);
  *lpNumberOfBytesRead = bytesread;
  return (nNumberOfBytesToRead == bytesread);
}

bool WriteFile(HANDLE hFile, LPCVOID lpBuffer, unsigned int nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten,
               LPOVERLAPPED lpOverlapped)
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

bool DeleteFile(LPCTSTR lpFileName)
{
  if (remove(lpFileName) == 0)
    return true;
  else
    return false;
}

unsigned int GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh)
{
  /* what is the size of the specified file??? Hmmm, really I donna. ^_^ */
  long lcurset = ftell((FILE *) hFile); // remember current file position

  fseek((FILE *) hFile, 0, FILE_END);  // go to the end of file
  unsigned int lfilesize = ftell((FILE *) hFile); // that is the real size of file, isn't it??
  fseek((FILE *) hFile, lcurset, FILE_BEGIN); // let the file position be the same as before
  return lfilesize;
}

LPVOID VirtualAlloc(LPVOID lpAddress, size_t dwSize, unsigned int flAllocationType, unsigned int flProtect)
{
  /* just malloc and alles? 0_0 */
  void *mymemory;
  mymemory = malloc(dwSize);
  if (flAllocationType & 0x1000)
    ZeroMemory(mymemory, dwSize); // original VirtualAlloc does this (if..)
  return mymemory;
}

bool VirtualFree(LPVOID lpAddress, size_t dwSize, unsigned int dwFreeType)
{
  free(lpAddress);
  return true;
}

// make all chars in buffer lowercase
unsigned int CharLowerBuff(LPTSTR lpsz, unsigned int cchLength)
{
  if (lpsz)
    for (; *lpsz; lpsz++)
      *lpsz = tolower(*lpsz);
  return 1;

}
