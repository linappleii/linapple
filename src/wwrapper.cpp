/*
 * Wrappers for some common Microsoft file and memory functions used in AppleWin
 *  by beom beotiger, Nov 2007AD
*/

#include "wwrapper.h"

DWORD SetFilePointer(HANDLE hFile,
       LONG lDistanceToMove,
       PLONG lpDistanceToMoveHigh,
       DWORD dwMoveMethod)  {
         /* ummm,fseek in Russian */
         fseek((FILE*)hFile, lDistanceToMove, dwMoveMethod);
         return ftell((FILE*)hFile);
}

BOOL ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead,
           LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)  {

  /* read something from file */
  DWORD bytesread = fread(lpBuffer, 1, nNumberOfBytesToRead, (FILE*)hFile);
  *lpNumberOfBytesRead = bytesread;
  return (nNumberOfBytesToRead == bytesread);
}

BOOL WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
    LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped) {
  /* write something to file */
  DWORD byteswritten = fwrite(lpBuffer, 1, nNumberOfBytesToWrite, (FILE*)hFile);
  *lpNumberOfBytesWritten = byteswritten;
  return (nNumberOfBytesToWrite == byteswritten);
}

/* close handle whatever it has been .... hmmmmm. I just love Microsoft! */
BOOL CloseHandle(HANDLE hObject) {
  return(!fclose((FILE*) hObject));
}

BOOL DeleteFile(LPCTSTR lpFileName) {
  if(remove(lpFileName) == 0) return TRUE;
  else return FALSE;
}

DWORD GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh) {
  /* what is the size of the specified file??? Hmmm, really I donna. ^_^ */
  long lcurset = ftell((FILE*)hFile); // remember current file position

  fseek((FILE*)hFile, 0, FILE_END);  // go to the end of file
  DWORD lfilesize = ftell((FILE*)hFile); // that is the real size of file, isn't it??
  fseek((FILE*)hFile, lcurset, FILE_BEGIN); // let the file position be the same as before
  return lfilesize;
}

LPVOID VirtualAlloc(LPVOID lpAddress, size_t dwSize,
      DWORD flAllocationType, DWORD flProtect) {
  /* just malloc and alles? 0_0 */
  void* mymemory;
  mymemory = malloc(dwSize);
  if(flAllocationType & 0x1000) ZeroMemory(mymemory, dwSize); // original VirtualAlloc does this (if..)
  return mymemory;
}

BOOL VirtualFree(LPVOID lpAddress, size_t dwSize,
      DWORD dwFreeType) {
  free(lpAddress);
  return TRUE;
}

// make all chars in buffer lowercase
DWORD CharLowerBuff(LPTSTR lpsz, DWORD cchLength)
{
//    char *s;
  if (lpsz)
    for (; *lpsz; lpsz++)
      *lpsz = tolower(*lpsz);
  return 1;

}
