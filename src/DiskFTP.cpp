/*
////////////////////////////////////////////////////////////////////////////
////////////  Choose disk image for given slot number? ////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
//// Adapted for linapple - apple][ emulator for Linux by beom beotiger Nov 2007   /////
///////////////////////////////////////////////////////////////////////////////////////
// Original source from one of Brain Games (http://www.braingames.getput.com)
//  game Super Transball 2.(http://www.braingames.getput.com/stransball2/default.asp)
//
// Brain Games crew creates brilliant retro-remakes! Please visit their site to find out more.
//
*/

/* March 2012 AD by Krez, Beom Beotiger */

#include "stdafx.h"
# include <string.h>
#include <stddef.h>

#ifndef _WIN32
//#include <sys/types.h>
#include <sys/stat.h>
//#include <dirent.h>
#endif

#include <time.h>
#include <vector>

#include "file_entry.h"
#include "DiskFTP.h"
#include "ftpparse.h"

// how many file names we are able to see at once!
#define FILES_IN_SCREEN    21
// delay after key pressed (in milliseconds??)
#define KEY_DELAY    25
// define time when cache ftp dir.listing must be refreshed
#define RENEW_TIME  24*3600

char *md5str(const char *input); // forward declaration of md5str func

char g_sFTPDirListing[512] = TEXT("cache/ftp."); // name for FTP-directory listing
int getstatFTP(struct ftpparse *fp, uintmax_t *size)
{
  // gets file status and returns: 0 - special or error, 1 - file is a directory, 2 - file is a normal file
  // In: fp - ftpparse struct ftom ftpparse.h
  if (!fp->namelen)
    return 0;
  if (fp->flagtrycwd == 1)
    return 1;  // can CWD, it is dir then

  if (fp->flagtryretr == 1) { // we're able to RETR, it's a file then?!
    if (size != NULL)
      *size = fp->size / 1024;
    return 2;
  }
  return 0;
}


struct FTP_file_list_generator_t : public file_list_generator_t {
  FTP_file_list_generator_t(const std::string& dir) :
    directory(dir)
  {}

  const std::vector<file_entry_t> generate_file_list();

  const std::string get_starting_message() {
    return "Connecting to FTP server... Please wait.";
  }

  const std::string get_failure_message() {
    return failure_message;
  }

private:
  std::string directory;
  std::string failure_message = "(no info)";
};


const std::vector<file_entry_t> FTP_file_list_generator_t::generate_file_list()
{
  char ftpdirpath[MAX_PATH];
  int l;
  l = snprintf(ftpdirpath, MAX_PATH,
                "%s/%s%s", g_sFTPLocalDir, g_sFTPDirListing, md5str(directory.c_str())); // get path for FTP dir listing

  if (!(l>=0 && l<MAX_PATH)) {      // check returned value
    failure_message = "Failed get path for FTP dir listing";
    return {};
  }


  bool OKI;
  #ifndef _WIN32
  struct stat info;
  if (stat(ftpdirpath, &info) == 0 && info.st_mtime > time(NULL) - RENEW_TIME) {
    OKI = false; // use this file
  } else {
    OKI = ftp_get(directory.c_str(), ftpdirpath); // get ftp dir listing
  }
  #else
  // in WIN32 let's use constant caching? -- need to be redone using file.mtime
    if(GetFileAttributes(ftpdirpath) != unsigned int(-1)) {
      OKI = false;
    } else {
      OKI = ftp_get(ftp_dir,ftpdirpath); // get ftp dir listing
    }
  #endif

  if (OKI) {  // error
    failure_message =
      "Failed getting FTP directory " + directory + " to " + ftpdirpath;
    return {};
  }

  std::vector<file_entry_t> file_list;

  // build prev dir
  if (directory != "ftp://") {
    file_list.push_back({ "..", file_entry_t::UP, 0 });
  }

  FILE *fdir = fopen(ftpdirpath, "r");
  char *tmp;
  char tmpstr[512];
  while ((tmp = fgets(tmpstr, 512, fdir))) // first looking for directories
  {
    // clear and then try to fill in FTP_PARSE struct
    struct ftpparse FTP_PARSE; // for parsing ftp directories

    memset(&FTP_PARSE, 0, sizeof(FTP_PARSE));
    ftpparse(&FTP_PARSE, tmp, strlen(tmp));

    uintmax_t fsize = 0;
    const int what = getstatFTP(&FTP_PARSE, &fsize);
    if (strlen(FTP_PARSE.name) < 1) {
      continue;
    }

    char* trimmed_name = php_trim(FTP_PARSE.name, strlen(FTP_PARSE.name));

    switch (what) {
    case 1: // is directory!
      file_list.push_back({ trimmed_name, file_entry_t::DIR, 0 });
      break;

    case 2: // is normal file!
      file_list.push_back({ trimmed_name, file_entry_t::FILE, fsize*1024 });
      break;

    default: // others: simply ignore
      ;
    }

    free(trimmed_name);
  }
  (void) fclose(fdir);

  std::sort(file_list.begin(), file_list.end());

  return file_list;
}


bool ChooseAnImageFTP(int sx, int sy, const std::string& ftp_dir, int slot,
                      std::string& filename, bool& isdir, size_t& index_file)
{
  /*  Parameters:
   sx, sy - window size,
   ftp_dir - what FTP directory to use,
   slot - in what slot should an image go (common: #6 for 5.25' 140Kb floppy disks, and #7 for hard-disks).
     slot #5 - for 800Kb floppy disks, but we do not use them in Apple][?
    (They are as a rule with .2mg extension)
   index_file  - from which file we should start cursor (should be static and 0 when changing dir)

   Out:  filename  - chosen file name (or dir name)
    isdir    - if chosen name is a directory
  */

  FTP_file_list_generator_t file_list_generator(ftp_dir);
  return ChooseImageDialog(sx, sy, ftp_dir, slot, &file_list_generator,
                           filename, isdir, index_file);
}

/* md5.c - an implementation of the MD5 algorithm and MD5 crypt */
/* See RFC 1321 for a description of the MD5 algorithm.
 */
#define cpu_to_le32(x) (x)
#define le32_to_cpu(x) cpu_to_le32(x)
typedef unsigned int UINT4;

/* F, G, H and I are basic MD5 functions.
 */
#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))

/* ROTATE_LEFT rotates x left n bits.
 */
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x >> (32 - (n)))))

static UINT4 md5_initstate[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};

static char s1[4] = {7, 12, 17, 22};
static char s2[4] = {5, 9, 14, 20};
static char s3[4] = {4, 11, 16, 23};
static char s4[4] = {6, 10, 15, 21};

static UINT4 T[64] = {0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
                      0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
                      0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
                      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
                      0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
                      0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
                      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
                      0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

static UINT4 state[4];
static unsigned int length;
static unsigned char buffer[64];

static void md5_transform(const unsigned char block[64])
{
  int i, j;
  UINT4 a, b, c, d, tmp;
  const UINT4 *x = (UINT4 *) block;

  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];

  /* Round 1 */
  for (i = 0; i < 16; i++) {
    tmp = a + F (b, c, d) + le32_to_cpu (x[i]) + T[i];
    tmp = ROTATE_LEFT (tmp, s1[i & 3]);
    tmp += b;
    a = d;
    d = c;
    c = b;
    b = tmp;
  }
  /* Round 2 */
  for (i = 0, j = 1; i < 16; i++, j += 5) {
    tmp = a + G (b, c, d) + le32_to_cpu (x[j & 15]) + T[i + 16];
    tmp = ROTATE_LEFT (tmp, s2[i & 3]);
    tmp += b;
    a = d;
    d = c;
    c = b;
    b = tmp;
  }
  /* Round 3 */
  for (i = 0, j = 5; i < 16; i++, j += 3) {
    tmp = a + H (b, c, d) + le32_to_cpu (x[j & 15]) + T[i + 32];
    tmp = ROTATE_LEFT (tmp, s3[i & 3]);
    tmp += b;
    a = d;
    d = c;
    c = b;
    b = tmp;
  }
  /* Round 4 */
  for (i = 0, j = 0; i < 16; i++, j += 7) {
    tmp = a + I (b, c, d) + le32_to_cpu (x[j & 15]) + T[i + 48];
    tmp = ROTATE_LEFT (tmp, s4[i & 3]);
    tmp += b;
    a = d;
    d = c;
    c = b;
    b = tmp;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}

static void md5_init(void)
{
  memcpy((char *) state, (char *) md5_initstate, sizeof(md5_initstate));
  length = 0;
}

static void md5_update(const char *input, int inputlen)
{
  int buflen = length & 63;
  length += inputlen;
  if (buflen + inputlen < 64) {
    memcpy(buffer + buflen, input, inputlen);
    buflen += inputlen;
    return;
  }

  memcpy(buffer + buflen, input, 64 - buflen);
  md5_transform(buffer);
  input += 64 - buflen;
  inputlen -= 64 - buflen;
  while (inputlen >= 64) {
    md5_transform((unsigned char *) input);
    input += 64;
    inputlen -= 64;
  }
  memcpy(buffer, input, inputlen);
  buflen = inputlen;
}

static unsigned char *md5_final()
{
  int i, buflen = length & 63;

  buffer[buflen++] = 0x80;
  memset(buffer + buflen, 0, 64 - buflen);
  if (buflen > 56) {
    md5_transform(buffer);
    memset(buffer, 0, 64);
    buflen = 0;
  }

  *(UINT4 *) (buffer + 56) = cpu_to_le32 (8 * length);
  *(UINT4 *) (buffer + 60) = 0;
  md5_transform(buffer);

  for (i = 0; i < 4; i++)
    state[i] = cpu_to_le32 (state[i]);
  return (unsigned char *) state;
}

static char *md5(const char *input)
{
  md5_init();
  md5_update(input, strlen(input));
  return (char *) md5_final();
}

// GPH Warning: Not re-entrant!
char *md5str(const char *input)
{
  static char result[16 * 3 + 1];
  unsigned char *digest = (unsigned char *) md5(input);
  int i;

  for (i = 0; i < 16; i++) {
    sprintf(result + 2 * i, "%02X", digest[i]);
  }
  return result;
}

