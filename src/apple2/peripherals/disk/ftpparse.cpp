// This sourse includes a FTPPARSE:

/* ftpparse.c, ftpparse.h: library for parsing FTP LIST responses
20001223
D. J. Bernstein, djb@cr.yp.to
http://cr.yp.to/ftpparse.html

Commercial use is fine, if you let me know what programs you're using this in.

Currently covered formats:
EPLF.
UNIX ls, with or without gid.
Microsoft FTP Service.
Windows NT FTP Server.
VMS.
WFTPD.
NetPresenz (Mac).
NetWare.
MSDOS.

Definitely not covered:
Long VMS filenames, with information split across two lines.
NCSA Telnet FTP server. Has LIST = NLST (and bad NLST for directories).
*/
#include "apple2/peripherals/disk/ftpparse.h"

#include <curl/curl.h>

#include <cstddef>
#include <cstdio>
#include <ctime>

#include "core/Common.h"
#include "core/Common_Globals.h"

const long SECONDS_PER_DAY = 86400;
const long SECONDS_PER_HOUR = 3600;
const long SECONDS_PER_MINUTE = 60;
const long DAYS_PER_400_YEARS = 146097;
const long DAYS_PER_100_YEARS = 36524;
const long DAYS_PER_4_YEARS = 1461;
const long DAYS_PER_YEAR = 365;
const int BASE_YEAR_TM = 1900;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): libcurl callback
// signature
static auto progress_callback(void* clientp, curl_off_t dltotal,
                              curl_off_t dlnow, curl_off_t ultotal,
                              curl_off_t ulnow) -> int {
  (void)clientp;
  (void)dltotal;
  (void)ultotal;
  (void)ulnow;
  printf("FTP: %lu bytes downloaded\n", dlnow);
  return 0;
}

auto ftp_get(const char* ftp_path, const char* local_path) -> CURLcode {
  // Download file from ftp_path to local_path
  CURLcode res;

  FilePtr stream(fopen(local_path, "w"), fclose);
  if (!stream) {
    return CURLE_WRITE_ERROR;
  }

  curl_easy_reset(g_curl);
  curl_easy_setopt(g_curl, CURLOPT_URL, ftp_path);
  curl_easy_setopt(g_curl, CURLOPT_WRITEDATA, stream.get());
  curl_easy_setopt(g_curl, CURLOPT_USERPWD, g_state.sFTPUserPass);

  curl_easy_setopt(g_curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
  curl_easy_setopt(g_curl, CURLOPT_NOPROGRESS, 0);

  res = curl_easy_perform(g_curl);

  if (res != CURLE_OK) {
    /* we failed */
    printf("Curl error with errorcode = %d\n", res);
  } else {
    printf("FTP: download completed\n");
  }

  return res;
}

// FTP Parse
static auto totai(long year, long month, long mday) -> long {
  long result = 0;
  if (month >= 2) {
    month -= 2;
  } else {
    month += 10;
    --year;
  }
  result = (mday - 1) * 10 + 5 + 306 * month;
  result /= 10;
  if (result == DAYS_PER_YEAR) {
    year -= 3;
    result = 1460;
  } else {
    result += DAYS_PER_YEAR * (year % 4);
  }
  year /= 4;
  result += DAYS_PER_4_YEARS * (year % 25);
  year /= 25;
  if (result == DAYS_PER_100_YEARS) {
    year -= 3;
    result = 146096;
  } else {
    result += DAYS_PER_100_YEARS * (year % 4);
  }
  year /= 4;
  result += DAYS_PER_400_YEARS * (year - 5);
  result += 11017;
  return result * SECONDS_PER_DAY;
}

static int flagneedbase = 1;
static time_t base; /* time() value on this OS at the beginning of 1970 TAI */
static long now;    /* current time */
static int flagneedcurrentyear = 1;
static long currentyear; /* approximation to current year */

static void initbase() {
  struct tm* t = nullptr;
  if (!flagneedbase) {
    return;
  }

  base = 0;
  t = gmtime(&base);
  base = -(totai(t->tm_year + BASE_YEAR_TM, t->tm_mon, t->tm_mday) +
           static_cast<long>(t->tm_hour * SECONDS_PER_HOUR) +
           static_cast<long>(t->tm_min * SECONDS_PER_MINUTE) + t->tm_sec);
  /* assumes the right time_t, counting seconds. */
  /* base may be slightly off if time_t counts non-leap seconds. */
  flagneedbase = 0;
}

static void initnow() {
  long day = 0;
  long year = 0;

  initbase();
  now = time((time_t*)nullptr) - base;

  if (flagneedcurrentyear) {
    day = now / SECONDS_PER_DAY;
    if ((now % SECONDS_PER_DAY) < 0) {
      --day;
    }
    day -= 11017;
    year = 5 + day / DAYS_PER_400_YEARS;
    day = day % DAYS_PER_400_YEARS;
    if (day < 0) {
      day += DAYS_PER_400_YEARS;
      --year;
    }
    year *= 4;
    if (day == 146096) {
      year += 3;
      day = DAYS_PER_100_YEARS;
    } else {
      year += day / DAYS_PER_100_YEARS;
      day %= DAYS_PER_100_YEARS;
    }
    year *= 25;
    year += day / DAYS_PER_4_YEARS;
    day %= DAYS_PER_4_YEARS;
    year *= 4;
    if (day == 1460) {
      year += 3;
      day = DAYS_PER_YEAR;
    } else {
      year += day / DAYS_PER_YEAR;
      day %= DAYS_PER_YEAR;
    }
    currentyear = year;
    flagneedcurrentyear = 0;
  }
}

static auto ftpparse_offsets(const char* month) -> int {
  switch (*month) {
    case 'A':
      if (month[1] == 'p') {
        return 3;
      }
      if (month[1] == 'u') {
        return 7;
      }
      break;
    case 'D':
      return 11;
    case 'F':
      return 1;
    case 'J':
      if (month[1] == 'a') {
        return 0;
      }
      if (month[2] == 'n') {
        return 5;
      }
      return 6;
    case 'M':
      if (month[2] == 'r') {
        return 2;
      }
      return 4;
    case 'N':
      return 10;
    case 'O':
      return 9;
    case 'S':
      return 8;
  }
  return -1;
}

static auto ftpparse_c(const char* buf, int len, struct ftpparse* fp) -> int {
  int i = 0;
  int j = 0;
  int state = 0;
  long size = 0;
  long year = 0;
  long month = 0;
  long mday = 0;

  if (!len) {
    return 0;
  }
  if (*buf == '+') {
    i = 1;
    for (j = 1; j < len; ++j) {
      if (buf[j] == ',') {
        if (state == 0) {
          fp->id = const_cast<char*>(buf + i);
          fp->idlen = j - i;
          state = 1;
          i = j + 1;
        } else if (state == 1) {
          i = j + 1;
          state = 2;
        } else if (state == 2) {
          size = 0;
          for (int k = i; k < j; ++k) {
            size = size * 10 + (buf[k] - '0');
          }
          fp->size = size;
          fp->sizetype = FTPPARSE_SIZE_BINARY;
          state = 3;
          i = j + 1;
        } else if (state == 3) {
          fp->mtime = 0;
          if (buf[i] == 'm') {
            for (int k = i + 1; k < j; ++k) {
              fp->mtime = fp->mtime * 10 + (buf[k] - '0');
            }
            fp->mtimetype = FTPPARSE_MTIME_LOCAL;
          }
          state = 4;
          i = j + 1;
        } else if (state == 4) {
          if (buf[i] == '/') {
            fp->flagtrycwd = 1;
          }
          state = 5;
          i = j + 1;
        } else if (state == 5) {
          return 1;
        }
      }
    }
    return 0;
  }

  while (i < len) {
    switch (*buf) {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        state = 0;
        month = 0;
        while (i < len && buf[i] >= '0' && buf[i] <= '9') {
          month = month * 10 + (buf[i] - '0');
          ++i;
        }
        if (i < len && buf[i] == '-') {
          mday = 0;
          ++i;
          while (i < len && buf[i] >= '0' && buf[i] <= '9') {
            mday = mday * 10 + (buf[i] - '0');
            ++i;
          }
          if (i < len && buf[i] == '-') {
            year = 0;
            ++i;
            while (i < len && buf[i] >= '0' && buf[i] <= '9') {
              year = year * 10 + (buf[i] - '0');
              ++i;
            }
            const int YEAR_70_THRESHOLD = 70;
            const int YEAR_100_THRESHOLD = 100;
            const int YEAR_2000 = 2000;
            if (year < YEAR_70_THRESHOLD) {
              year += YEAR_2000;
            } else if (year < YEAR_100_THRESHOLD) {
              year += BASE_YEAR_TM;
            }
          }
        }
        while (i < len && buf[i] == ' ') {
          ++i;
        }
        if (i < len && buf[i] == '<') {
          ++i;
          if (i < len && buf[i] == 'D') {
            fp->flagtrycwd = 1;
          }
          while (i < len && buf[i] != '>') {
            ++i;
          }
          if (i < len) {
            ++i;
          }
        }
        while (i < len && buf[i] == ' ') {
          ++i;
        }
        size = 0;
        while (i < len && buf[i] >= '0' && buf[i] <= '9') {
          size = size * 10 + (buf[i] - '0');
          ++i;
        }
        fp->size = size;
        fp->sizetype = FTPPARSE_SIZE_BINARY;
        while (i < len && buf[i] == ' ') {
          ++i;
        }
        fp->id = const_cast<char*>(buf + i);
        fp->idlen = len - i;
        fp->mtime = totai(year, month - 1, mday);
        fp->mtimetype = FTPPARSE_MTIME_REMOTEDAY;
        return 1;
      case 'd':
        fp->flagtrycwd = 1;
        break;
      case '-':
        fp->flagtrycwd = 0;
        break;
      case 'l':
        fp->flagtrycwd = 0;  // TODO: handle links
        break;
      default:
        return 0;
    }

    i = 0;
    for (j = 0; j < len; ++j) {
      if (buf[j] == ' ') {
        if (state == 0) {
          state = 1;
          i = j + 1;
        } else {
          while (j < len && buf[j] == ' ') {
            ++j;
          }
          i = j;
          break;
        }
      }
    }
    return 0;
  }
  return 0;
}

auto ftpparse(struct ftpparse* fp, char* buf, int len) -> int {
  fp->name = nullptr;
  fp->namelen = 0;
  fp->flagtrycwd = 0;
  fp->flagtryretr = 0;
  fp->size = 0;
  fp->sizetype = FTPPARSE_SIZE_UNKNOWN;
  fp->mtime = 0;
  fp->mtimetype = FTPPARSE_MTIME_UNKNOWN;
  fp->id = nullptr;
  fp->idlen = 0;
  fp->idtype = FTPPARSE_ID_UNKNOWN;

  if (len < 3) {
    return 0;
  }

  if (ftpparse_c(buf, len, fp)) {
    return 1;
  }

  // Handle UNIX ls -l and other formats
  // This is a simplified version of the original ftpparse
  // specifically for our needs.

  int i = 0;
  if (buf[0] == 'd') {
    fp->flagtrycwd = 1;
  }

  // Skip permissions, links, owner, group
  int spaces = 0;
  while (i < len && spaces < 4) {
    if (buf[i] == ' ') {
      spaces++;
      while (i < len && buf[i] == ' ') {
        i++;
      }
    } else {
      i++;
    }
  }

  // Read size
  long size = 0;
  while (i < len && buf[i] >= '0' && buf[i] <= '9') {
    size = size * 10 + (buf[i] - '0');
    i++;
  }
  fp->size = size;
  fp->sizetype = FTPPARSE_SIZE_BINARY;

  // Skip space
  while (i < len && buf[i] == ' ') {
    i++;
  }

  // Read month
  if (i + 3 < len) {
    char month_str[4];
    month_str[0] = buf[i++];
    month_str[1] = buf[i++];
    month_str[2] = buf[i++];
    month_str[3] = 0;
    int month = ftpparse_offsets(month_str);
    if (month != -1) {
      // Skip space
      while (i < len && buf[i] == ' ') {
        i++;
      }
      // Read day
      int mday = 0;
      while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        mday = mday * 10 + (buf[i] - '0');
        i++;
      }
      // Skip space
      while (i < len && buf[i] == ' ') {
        i++;
      }
      // Read year or time
      int year = 0;
      int hour = 0;
      int minute = 0;
      if (i + 4 < len && buf[i + 2] == ':') {
        hour = (buf[i] - '0') * 10 + (buf[i + 1] - '0');
        minute = (buf[i + 3] - '0') * 10 + (buf[i + 4] - '0');
        i += 5;
        initnow();
        year = static_cast<int>(currentyear);
        fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
      } else {
        while (i < len && buf[i] >= '0' && buf[i] <= '9') {
          year = year * 10 + (buf[i] - '0');
          i++;
        }
        fp->mtimetype = FTPPARSE_MTIME_REMOTEDAY;
      }

      fp->mtime = totai(year, month, mday) + hour * SECONDS_PER_HOUR +
                  minute * SECONDS_PER_MINUTE;
    }
  }

  // Skip space to get name
  while (i < len && buf[i] == ' ') {
    i++;
  }
  fp->name = buf + i;
  fp->namelen = len - i;

  return 1;
}
