// SPDX-License-Identifier: GPL-2.0-only
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

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>

#include "core/Common.h"
#include "core/Common_Globals.h"
#include "core/Log.h"

const int64_t SECONDS_PER_DAY = 86400;
const int64_t SECONDS_PER_HOUR = 3600;
const int64_t SECONDS_PER_MINUTE = 60;
const int64_t DAYS_PER_400_YEARS = 146097;
const int64_t DAYS_PER_100_YEARS = 36524;
const int64_t DAYS_PER_4_YEARS = 1461;
const int64_t DAYS_PER_YEAR = 365;
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
  Logger::Info("FTP: %lu bytes downloaded\n", dlnow);
  return 0;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): public API
auto ftp_get(const char* ftp_path, const char* local_path) -> CURLcode {
  // Download file from ftp_path to local_path
  CURLcode res = CURLE_OK;

  FilePtr stream(fopen(local_path, "w"), fclose);
  if (!stream) {
    return CURLE_WRITE_ERROR;
  }

  curl_easy_reset(g_curl);
  curl_easy_setopt(g_curl, CURLOPT_URL, ftp_path);
  curl_easy_setopt(g_curl, CURLOPT_WRITEDATA, stream.get());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay): ABI
  curl_easy_setopt(g_curl, CURLOPT_USERPWD, g_state.sFTPUserPass.data());

  curl_easy_setopt(g_curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
  curl_easy_setopt(g_curl, CURLOPT_NOPROGRESS, 0);

  res = curl_easy_perform(g_curl);

  if (res != CURLE_OK) {
    /* we failed */
    Logger::Error("Curl error with errorcode = %d\n", res);
  } else {
    Logger::Info("FTP: download completed\n");
  }

  return res;
}

// FTP Parse
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): algorithm parameters
static auto totai(int64_t year, int64_t month, int64_t mday) -> int64_t {
  int64_t result = 0;
  const int64_t MONTH_OFFSET = 2;
  const int64_t MONTH_ADJUST = 10;
  const int64_t DAY_MULTIPLIER = 10;
  const int64_t DAY_ADJUST = 5;
  const int64_t MONTH_MULTIPLIER = 306;
  const int64_t LEAP_YEAR_ADJUST = 3;
  const int64_t DAYS_PER_4_YEARS_MINUS_1 = 1460;
  const int64_t FOUR_YEAR_CYCLE = 4;
  const int64_t TWENTY_FIVE_YEAR_CYCLE = 25;
  const int64_t DAYS_PER_400_YEARS_MINUS_1 = 146096;
  const int64_t YEAR_OFFSET = 5;
  const int64_t CONSTANT_OFFSET = 11017;

  if (month >= MONTH_OFFSET) {
    month -= MONTH_OFFSET;
  } else {
    month += MONTH_ADJUST;
    --year;
  }
  result = (mday - 1) * DAY_MULTIPLIER + DAY_ADJUST + MONTH_MULTIPLIER * month;
  result /= DAY_MULTIPLIER;
  if (result == DAYS_PER_YEAR) {
    year -= LEAP_YEAR_ADJUST;
    result = DAYS_PER_4_YEARS_MINUS_1;
  } else {
    result += DAYS_PER_YEAR * (year % FOUR_YEAR_CYCLE);
  }
  year /= FOUR_YEAR_CYCLE;
  result += DAYS_PER_4_YEARS * (year % TWENTY_FIVE_YEAR_CYCLE);
  year /= TWENTY_FIVE_YEAR_CYCLE;
  if (result == DAYS_PER_100_YEARS) {
    year -= LEAP_YEAR_ADJUST;
    result = DAYS_PER_400_YEARS_MINUS_1;
  } else {
    result += DAYS_PER_100_YEARS * (year % FOUR_YEAR_CYCLE);
  }
  year /= FOUR_YEAR_CYCLE;
  result += DAYS_PER_400_YEARS * (year - YEAR_OFFSET);
  result += CONSTANT_OFFSET;
  return result * SECONDS_PER_DAY;
}

static int flagneedbase = 1;
static time_t base; /* time() value on this OS at the beginning of 1970 TAI */
static int64_t now; /* current time */
static int flagneedcurrentyear = 1;
static int64_t currentyear; /* approximation to current year */

static void initbase() {
  struct tm* t = nullptr;
  if (!flagneedbase) {
    return;
  }

  base = 0;
  t = gmtime(&base);
  base = static_cast<time_t>(
      -(totai(t->tm_year + BASE_YEAR_TM, t->tm_mon, t->tm_mday) +
        static_cast<int64_t>(t->tm_hour * SECONDS_PER_HOUR) +
        static_cast<int64_t>(t->tm_min * SECONDS_PER_MINUTE) + t->tm_sec));
  /* assumes the right time_t, counting seconds. */
  /* base may be slightly off if time_t counts non-leap seconds. */
  flagneedbase = 0;
}

static void initnow() {
  int64_t day = 0;
  int64_t year = 0;

  initbase();
  now = time(nullptr) - base;

  if (flagneedcurrentyear) {
    day = now / SECONDS_PER_DAY;
    if ((now % SECONDS_PER_DAY) < 0) {
      --day;
    }
    const int64_t CONSTANT_OFFSET = 11017;
    const int64_t YEAR_OFFSET = 5;
    const int64_t DAYS_PER_400_YEARS_MINUS_1 = 146096;
    const int64_t DAYS_PER_4_YEARS_MINUS_1 = 1460;
    const int64_t TWENTY_FIVE_YEAR_CYCLE = 25;
    const int64_t FOUR_YEAR_CYCLE = 4;
    const int64_t LEAP_YEAR_ADJUST = 3;

    day -= CONSTANT_OFFSET;
    year = YEAR_OFFSET + day / DAYS_PER_400_YEARS;
    day = day % DAYS_PER_400_YEARS;
    if (day < 0) {
      day += DAYS_PER_400_YEARS;
      --year;
    }
    year *= FOUR_YEAR_CYCLE;
    if (day == DAYS_PER_400_YEARS_MINUS_1) {
      year += LEAP_YEAR_ADJUST;
      day = DAYS_PER_100_YEARS;
    } else {
      year += day / DAYS_PER_100_YEARS;
      day %= DAYS_PER_100_YEARS;
    }
    year *= TWENTY_FIVE_YEAR_CYCLE;
    year += day / DAYS_PER_4_YEARS;
    day %= DAYS_PER_4_YEARS;
    year *= FOUR_YEAR_CYCLE;
    if (day == DAYS_PER_4_YEARS_MINUS_1) {
      year += LEAP_YEAR_ADJUST;
      day = DAYS_PER_YEAR;
    } else {
      year += day / DAYS_PER_YEAR;
      day %= DAYS_PER_YEAR;
    }
    currentyear = year;
    flagneedcurrentyear = 0;
  }
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
static auto ftpparse_offsets(const char* month) -> int {
  const int MONTH_JAN = 0;
  const int MONTH_FEB = 1;
  const int MONTH_MAR = 2;
  const int MONTH_APR = 3;
  const int MONTH_MAY = 4;
  const int MONTH_JUN = 5;
  const int MONTH_JUL = 6;
  const int MONTH_AUG = 7;
  const int MONTH_SEP = 8;
  const int MONTH_OCT = 9;
  const int MONTH_NOV = 10;
  const int MONTH_DEC = 11;

  switch (*month) {
    case 'A':
      if (month[1] == 'p') {
        return MONTH_APR;
      }
      if (month[1] == 'u') {
        return MONTH_AUG;
      }
      break;
    case 'D':
      return MONTH_DEC;
    case 'F':
      return MONTH_FEB;
    case 'J':
      if (month[1] == 'a') {
        return MONTH_JAN;
      }
      if (month[2] == 'n') {
        return MONTH_JUN;
      }
      return MONTH_JUL;
    case 'M':
      if (month[2] == 'r') {
        return MONTH_MAR;
      }
      return MONTH_MAY;
    case 'N':
      return MONTH_NOV;
    case 'O':
      return MONTH_OCT;
    case 'S':
      return MONTH_SEP;
    default:
      break;
  }
  return -1;
}

static auto ftpparse_c(char* buf, int len, struct ftpparse* fp) -> int {
  int i = 0;
  int j = 0;
  int state = 0;
  int64_t size = 0;
  int64_t year = 0;
  int64_t month = 0;
  int64_t mday = 0;

  if (len == 0) {
    return 0;
  }
  if (*buf == '+') {
    i = 1;
    for (j = 1; j < len; ++j) {
      if (buf[j] == ',') {
        if (state == 0) {
          fp->id = (buf + i);
          fp->idlen = j - i;
          state = 1;
          i = j + 1;
        } else if (state == 1) {
          i = j + 1;
          state = 2;
        } else if (state == 2) {
          size = 0;
          for (int k = i; k < j; ++k) {
            const int64_t BASE_10 = 10;
            size = size * BASE_10 + (buf[k] - '0');
          }
          fp->size = size;
          fp->sizetype = FTPPARSE_SIZE_BINARY;
          state = 3;
          i = j + 1;
        } else if (state == 3) {
          fp->mtime = 0;
          if (buf[i] == 'm') {
            for (int k = i + 1; k < j; ++k) {
              const int64_t BASE_10 = 10;
              fp->mtime = fp->mtime * BASE_10 + (buf[k] - '0');
            }
            fp->mtimetype = FTPPARSE_MTIME_LOCAL;
          }
          state = 4;
          i = j + 1;
        } else if (state == 4) {
          if (buf[i] == '/') {
            fp->flagtrycwd = 1;
          }
          const int STATE_5 = 5;
          state = STATE_5;
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
          const int64_t BASE_10 = 10;
          month = month * BASE_10 + (buf[i] - '0');
          ++i;
        }
        if (i < len && buf[i] == '-') {
          mday = 0;
          ++i;
          while (i < len && buf[i] >= '0' && buf[i] <= '9') {
            const int64_t BASE_10 = 10;
            mday = mday * BASE_10 + (buf[i] - '0');
            ++i;
          }
          if (i < len && buf[i] == '-') {
            year = 0;
            ++i;
            while (i < len && buf[i] >= '0' && buf[i] <= '9') {
              const int64_t BASE_10 = 10;
              year = year * BASE_10 + (buf[i] - '0');
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
          const int64_t BASE_10 = 10;
          size = size * BASE_10 + (buf[i] - '0');
          ++i;
        }
        fp->size = static_cast<long>(size);
        fp->sizetype = FTPPARSE_SIZE_BINARY;
        while (i < len && buf[i] == ' ') {
          ++i;
        }
        fp->id = (buf + i);
        fp->idlen = len - i;
        fp->mtime = static_cast<time_t>(totai(year, month - 1, mday));
        fp->mtimetype = FTPPARSE_MTIME_REMOTEDAY;
        return 1;
      case 'd':
        fp->flagtrycwd = 1;
        break;
      case '-':
        fp->flagtrycwd = 0;
        break;
      case 'l':
        fp->flagtrycwd = 1;
        fp->flagtryretr = 1;
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
  } else if (buf[0] == 'l') {
    fp->flagtrycwd = 1;
    fp->flagtryretr = 1;
  } else if (buf[0] == '-') {
    fp->flagtryretr = 1;
  }

  // Skip permissions, links, owner, group
  int spaces = 0;
  const int MAX_SPACES_TO_SKIP = 4;
  while (i < len && spaces < MAX_SPACES_TO_SKIP) {
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
  int64_t size = 0;
  while (i < len && buf[i] >= '0' && buf[i] <= '9') {
    const int64_t BASE_10 = 10;
    size = size * BASE_10 + (buf[i] - '0');
    i++;
  }
  fp->size = static_cast<long>(size);
  fp->sizetype = FTPPARSE_SIZE_BINARY;

  // Skip space
  while (i < len && buf[i] == ' ') {
    i++;
  }

  // Read month
  const int MONTH_STR_LEN = 3;
  if (i + MONTH_STR_LEN < len) {
    std::array<char, 4> month_str{};
    month_str[0] = buf[i++];
    month_str[1] = buf[i++];
    month_str[2] = buf[i++];
    month_str[3] = '\0';
    int month = ftpparse_offsets(month_str.data());
    if (month != -1) {
      // Skip space
      while (i < len && buf[i] == ' ') {
        i++;
      }
      // Read day
      int mday = 0;
      while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        const int BASE_10 = 10;
        mday = mday * BASE_10 + (buf[i] - '0');
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
      const int TIME_STR_LEN = 4;
      const int TIME_COLON_OFFSET = 2;
      const int TIME_TOTAL_LEN = 5;
      if (i + TIME_STR_LEN < len && buf[i + TIME_COLON_OFFSET] == ':') {
        const int BASE_10 = 10;
        hour = (buf[i] - '0') * BASE_10 + (buf[i + 1] - '0');
        minute = (buf[i + 3] - '0') * BASE_10 + (buf[i + 4] - '0');
        i += TIME_TOTAL_LEN;
        initnow();
        year = static_cast<int>(currentyear);
        fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
      } else {
        while (i < len && buf[i] >= '0' && buf[i] <= '9') {
          const int BASE_10 = 10;
          year = year * BASE_10 + (buf[i] - '0');
          i++;
        }
        fp->mtimetype = FTPPARSE_MTIME_REMOTEDAY;
      }

      fp->mtime = static_cast<time_t>(
          totai(year, month, mday) +
          static_cast<int64_t>(hour) * SECONDS_PER_HOUR +
          static_cast<int64_t>(minute) * SECONDS_PER_MINUTE);
    }
  }

  // Skip space to get name
  while (i < len && buf[i] == ' ') {
    i++;
  }
  fp->name = buf + i;
  fp->namelen = len - i;

  // Handle symbolic link suffix: "name -> target"
  if (buf[0] == 'l') {
    const int LINK_SUFFIX_LEN = 3;
    for (int k = 0; k + LINK_SUFFIX_LEN < fp->namelen; ++k) {
      if (fp->name[k] == ' ' && fp->name[k + 1] == '-' &&
          fp->name[k + 2] == '>' && fp->name[k + 3] == ' ') {
        fp->namelen = k;
        break;
      }
    }
  }

  return 1;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
