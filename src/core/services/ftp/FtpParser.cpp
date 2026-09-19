// SPDX-License-Identifier: GPL-2.0-only
#include "core/services/ftp/FtpParser.h"

#include <cstdint>
#include <cstring>
#include <ctime>
#include <vector>

#include "core/services/ftp/FtpTypes.h"

namespace {

constexpr int64_t SECONDS_PER_DAY = 86400;
constexpr int64_t SECONDS_PER_HOUR = 3600;
constexpr int64_t SECONDS_PER_MINUTE = 60;
constexpr int64_t DAYS_PER_400_YEARS = 146097;
constexpr int64_t DAYS_PER_100_YEARS = 36524;
constexpr int64_t DAYS_PER_4_YEARS = 1461;
constexpr int64_t DAYS_PER_YEAR = 365;
constexpr int BASE_YEAR_TM = 1900;

auto totai(int64_t year, int64_t month, int64_t mday) -> int64_t {
  int64_t result = 0;
  constexpr int64_t MONTH_OFFSET = 2;
  constexpr int64_t MONTH_ADJUST = 10;
  constexpr int64_t DAY_MULTIPLIER = 10;
  constexpr int64_t DAY_ADJUST = 5;
  constexpr int64_t MONTH_MULTIPLIER = 306;
  constexpr int64_t LEAP_YEAR_ADJUST = 3;
  constexpr int64_t DAYS_PER_4_YEARS_MINUS_1 = 1460;
  constexpr int64_t FOUR_YEAR_CYCLE = 4;
  constexpr int64_t TWENTY_FIVE_YEAR_CYCLE = 25;
  constexpr int64_t DAYS_PER_400_YEARS_MINUS_1 = 146096;
  constexpr int64_t YEAR_OFFSET = 5;
  constexpr int64_t CONSTANT_OFFSET = 11017;

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

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
int flagneedbase = 1;
time_t base = 0;
int64_t now = 0;
int flagneedcurrentyear = 1;
int64_t currentyear = 0;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void initbase() {
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
  flagneedbase = 0;
}

void initnow() {
  int64_t day = 0;
  int64_t year = 0;

  initbase();
  now = time(nullptr) - base;

  if (flagneedcurrentyear) {
    day = now / SECONDS_PER_DAY;
    if ((now % SECONDS_PER_DAY) < 0) {
      --day;
    }
    constexpr int64_t CONSTANT_OFFSET = 11017;
    constexpr int64_t YEAR_OFFSET = 5;
    constexpr int64_t DAYS_PER_400_YEARS_MINUS_1 = 146096;
    constexpr int64_t DAYS_PER_4_YEARS_MINUS_1 = 1460;
    constexpr int64_t TWENTY_FIVE_YEAR_CYCLE = 25;
    constexpr int64_t FOUR_YEAR_CYCLE = 4;
    constexpr int64_t LEAP_YEAR_ADJUST = 3;

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

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, readability-magic-numbers)
// Justification: Upstream D. J. Bernstein ftpparse algorithmic core preserved
// for 1:1 format fidelity

auto guesstai(int64_t month, int64_t mday) -> int64_t {
  initnow();
  for (int64_t year = currentyear - 1; year < currentyear + 100; ++year) {
    const int64_t t = totai(year, month, mday);
    if (now - t < 350 * SECONDS_PER_DAY) {
      return t;
    }
  }
  return 0;
}

auto check_month(const char* buf, const char* monthname) -> int {
  if ((buf[0] != monthname[0]) && (buf[0] != monthname[0] - 32)) {
    return 0;
  }
  if ((buf[1] != monthname[1]) && (buf[1] != monthname[1] - 32)) {
    return 0;
  }
  if ((buf[2] != monthname[2]) && (buf[2] != monthname[2] - 32)) {
    return 0;
  }
  return 1;
}

const char* const months[12] = {"jan", "feb", "mar", "apr", "may", "jun",
                                "jul", "aug", "sep", "oct", "nov", "dec"};

auto getmonth(const char* buf, int len) -> int {
  if (len == 3) {
    for (int i = 0; i < 12; ++i) {
      if (check_month(buf, months[i]) != 0) {
        return i;
      }
    }
  }
  return -1;
}

auto getlong(const char* buf, int len) -> uint64_t {
  uint64_t u = 0;
  while (len-- > 0) {
    constexpr uint64_t BASE_10 = 10;
    const uint64_t digit = static_cast<uint64_t>(*buf++ - '0');
    if (u <= (UINT64_MAX - digit) / BASE_10) {
      u = u * BASE_10 + digit;
    } else {
      u = UINT64_MAX;
    }
  }
  return u;
}

}  // namespace

auto ftpparse(struct ftpparse* fp, char* buf, int len) -> int {
  int i = 0;
  int j = 0;
  int state = 0;
  uint64_t size = 0;
  int64_t year = 0;
  int64_t month = 0;
  int64_t mday = 0;
  int64_t hour = 0;
  int64_t minute = 0;

  fp->name = nullptr;
  fp->namelen = 0;
  fp->flagtrycwd = 0;
  fp->flagtryretr = 0;
  fp->sizetype = FTPPARSE_SIZE_UNKNOWN;
  fp->size = 0;
  fp->mtimetype = FTPPARSE_MTIME_UNKNOWN;
  fp->mtime = 0;
  fp->idtype = FTPPARSE_ID_UNKNOWN;
  fp->id = nullptr;
  fp->idlen = 0;

  if (len < 2) {
    return 0;
  }

  switch (*buf) {
    // EPLF format: "+i8388621.29609,m824255902,/,\tdev"
    case '+':
      i = 1;
      for (j = 1; j < len; ++j) {
        if (buf[j] == 9 || buf[j] == ',') {
          switch (buf[i]) {
            case '/':
              fp->flagtrycwd = 1;
              break;
            case 'r':
              fp->flagtryretr = 1;
              break;
            case 's':
              fp->sizetype = FTPPARSE_SIZE_BINARY;
              fp->size = static_cast<int64_t>(getlong(buf + i + 1, j - i - 1));
              break;
            case 'm':
              fp->mtimetype = FTPPARSE_MTIME_LOCAL;
              initbase();
              fp->mtime = static_cast<time_t>(
                  base + static_cast<int64_t>(getlong(buf + i + 1, j - i - 1)));
              break;
            case 'i':
              fp->idtype = FTPPARSE_ID_FULL;
              fp->id = buf + i + 1;
              fp->idlen = j - i - 1;
              break;
            default:
              break;
          }
          i = j + 1;
          if (buf[j] == 9) {
            fp->name = buf + j + 1;
            fp->namelen = len - j - 1;
            return 1;
          }
        }
      }
      return 0;

    // UNIX ls format
    case 'b':
    case 'c':
    case 'd':
    case 'l':
    case 'p':
    case 's':
    case '-':
      if (*buf == 'd') {
        fp->flagtrycwd = 1;
      }
      if (*buf == '-') {
        fp->flagtryretr = 1;
      }
      if (*buf == 'l') {
        fp->flagtrycwd = 1;
        fp->flagtryretr = 1;
      }

      state = 1;
      i = 0;
      for (j = 1; j < len; ++j) {
        if ((buf[j] == ' ') && (buf[j - 1] != ' ')) {
          switch (state) {
            case 1:  // skipping perm
              state = 2;
              break;
            case 2:  // skipping nlink
              state = 3;
              if ((j - i == 6) && (buf[i] == 'f')) {
                state = 4;
              }
              break;
            case 3:  // skipping uid
              state = 4;
              break;
            case 4:  // tentative size
              size = getlong(buf + i, j - i);
              state = 5;
              break;
            case 5:  // searching for month
              month = getmonth(buf + i, j - i);
              if (month >= 0) {
                state = 6;
              } else {
                size = getlong(buf + i, j - i);
              }
              break;
            case 6:  // have size and month
              mday = static_cast<int64_t>(getlong(buf + i, j - i));
              state = 7;
              break;
            case 7:  // have size, month, mday
              if ((j - i == 4) && (buf[i + 1] == ':')) {
                hour = static_cast<int64_t>(getlong(buf + i, 1));
                minute = static_cast<int64_t>(getlong(buf + i + 2, 2));
                fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
                initbase();
                fp->mtime = static_cast<time_t>(base + guesstai(month, mday) +
                                                hour * SECONDS_PER_HOUR +
                                                minute * SECONDS_PER_MINUTE);
              } else if ((j - i == 5) && (buf[i + 2] == ':')) {
                hour = static_cast<int64_t>(getlong(buf + i, 2));
                minute = static_cast<int64_t>(getlong(buf + i + 3, 2));
                fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
                initbase();
                fp->mtime = static_cast<time_t>(base + guesstai(month, mday) +
                                                hour * SECONDS_PER_HOUR +
                                                minute * SECONDS_PER_MINUTE);
              } else if (j - i >= 4) {
                year = static_cast<int64_t>(getlong(buf + i, j - i));
                fp->mtimetype = FTPPARSE_MTIME_REMOTEDAY;
                initbase();
                fp->mtime =
                    static_cast<time_t>(base + totai(year, month, mday));
              } else {
                return 0;
              }
              fp->name = buf + j + 1;
              fp->namelen = len - j - 1;
              state = 8;
              break;
            case 8:
              break;
            default:
              break;
          }
          i = j + 1;
          while ((i < len) && (buf[i] == ' ')) {
            ++i;
          }
        }
      }

      if (state != 8) {
        return 0;
      }

      fp->size = static_cast<int64_t>(size);
      fp->sizetype = FTPPARSE_SIZE_BINARY;

      if (*buf == 'l') {
        for (i = 0; i + 3 < fp->namelen; ++i) {
          if (fp->name[i] == ' ' && fp->name[i + 1] == '-' &&
              fp->name[i + 2] == '>' && fp->name[i + 3] == ' ') {
            fp->namelen = i;
            break;
          }
        }
      }

      // Eliminate extra NetWare spaces
      if ((buf[1] == ' ') || (buf[1] == '[')) {
        if (fp->namelen > 3 && fp->name[0] == ' ' && fp->name[1] == ' ' &&
            fp->name[2] == ' ') {
          fp->name += 3;
          fp->namelen -= 3;
        }
      }

      return 1;
    default:
      break;
  }

  // MultiNet / VMS: "00README.TXT;1 2 30-DEC-1996 17:44 [SYSTEM]"
  for (i = 0; i < len; ++i) {
    if (buf[i] == ';') {
      break;
    }
  }
  if (i < len) {
    fp->name = buf;
    fp->namelen = i;
    if (i > 4) {
      if (buf[i - 4] == '.' && buf[i - 3] == 'D' && buf[i - 2] == 'I' &&
          buf[i - 1] == 'R') {
        fp->namelen -= 4;
        fp->flagtrycwd = 1;
      }
    }
    if (!fp->flagtrycwd) {
      fp->flagtryretr = 1;
    }
    while (buf[i] != ' ') {
      if (++i == len) return 0;
    }
    while (buf[i] == ' ') {
      if (++i == len) return 0;
    }
    while (buf[i] != ' ') {
      if (++i == len) return 0;
    }
    while (buf[i] == ' ') {
      if (++i == len) return 0;
    }
    j = i;
    while (buf[j] != '-') {
      if (++j == len) return 0;
    }
    mday = static_cast<int64_t>(getlong(buf + i, j - i));
    while (buf[j] == '-') {
      if (++j == len) return 0;
    }
    i = j;
    while (buf[j] != '-') {
      if (++j == len) return 0;
    }
    month = getmonth(buf + i, j - i);
    if (month < 0) {
      return 0;
    }
    while (buf[j] == '-') {
      if (++j == len) return 0;
    }
    i = j;
    while (buf[j] != ' ') {
      if (++j == len) return 0;
    }
    year = static_cast<int64_t>(getlong(buf + i, j - i));
    while (buf[j] == ' ') {
      if (++j == len) return 0;
    }
    i = j;
    while (buf[j] != ':') {
      if (++j == len) return 0;
    }
    hour = static_cast<int64_t>(getlong(buf + i, j - i));
    while (buf[j] == ':') {
      if (++j == len) return 0;
    }
    i = j;
    while ((buf[j] != ':') && (buf[j] != ' ')) {
      if (++j == len) return 0;
    }
    minute = static_cast<int64_t>(getlong(buf + i, j - i));

    fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
    initbase();
    fp->mtime = static_cast<time_t>(base + totai(year, month, mday) +
                                    hour * SECONDS_PER_HOUR +
                                    minute * SECONDS_PER_MINUTE);

    return 1;
  }

  // MSDOS / Windows NT format: "04-27-00 09:09PM <DIR> licensed"
  if ((*buf >= '0') && (*buf <= '9')) {
    i = 0;
    j = 0;
    while (buf[j] != '-') {
      if (++j == len) return 0;
    }
    month = static_cast<int64_t>(getlong(buf + i, j - i)) - 1;
    while (buf[j] == '-') {
      if (++j == len) return 0;
    }
    i = j;
    while (buf[j] != '-') {
      if (++j == len) return 0;
    }
    mday = static_cast<int64_t>(getlong(buf + i, j - i));
    while (buf[j] == '-') {
      if (++j == len) return 0;
    }
    i = j;
    while (buf[j] != ' ') {
      if (++j == len) return 0;
    }
    year = static_cast<int64_t>(getlong(buf + i, j - i));
    if (year < 50) {
      year += 2000;
    } else if (year < 1000) {
      year += 1900;
    }
    while (buf[j] == ' ') {
      if (++j == len) return 0;
    }
    i = j;
    while (buf[j] != ':') {
      if (++j == len) return 0;
    }
    hour = static_cast<int64_t>(getlong(buf + i, j - i));
    while (buf[j] == ':') {
      if (++j == len) return 0;
    }
    i = j;
    while ((buf[j] != 'A') && (buf[j] != 'P')) {
      if (++j == len) return 0;
    }
    minute = static_cast<int64_t>(getlong(buf + i, j - i));
    if (hour == 12) {
      hour = 0;
    }
    if (buf[j] == 'A') {
      if (++j == len) return 0;
    }
    if (buf[j] == 'P') {
      hour += 12;
      if (++j == len) return 0;
    }
    if (buf[j] == 'M') {
      if (++j == len) return 0;
    }

    while (buf[j] == ' ') {
      if (++j == len) return 0;
    }
    if (buf[j] == '<') {
      fp->flagtrycwd = 1;
      while (buf[j] != ' ') {
        if (++j == len) return 0;
      }
    } else {
      i = j;
      while (buf[j] != ' ') {
        if (++j == len) return 0;
      }
      fp->size = static_cast<int64_t>(getlong(buf + i, j - i));
      fp->sizetype = FTPPARSE_SIZE_BINARY;
      fp->flagtryretr = 1;
    }
    while (buf[j] == ' ') {
      if (++j == len) return 0;
    }

    fp->name = buf + j;
    fp->namelen = len - j;

    fp->mtimetype = FTPPARSE_MTIME_REMOTEMINUTE;
    initbase();
    fp->mtime = static_cast<time_t>(base + totai(year, month, mday) +
                                    hour * SECONDS_PER_HOUR +
                                    minute * SECONDS_PER_MINUTE);

    return 1;
  }

  return 0;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, readability-magic-numbers)

auto ftp_parse_line(const char* line, size_t length, FtpFileEntry_t& out_entry)
    -> bool {
  if (line == nullptr || length == 0) {
    return false;
  }

  // Strip trailing CR and LF
  while (length > 0 && (line[length - 1] == '\r' || line[length - 1] == '\n')) {
    --length;
  }
  if (length == 0) {
    return false;
  }

  std::vector<char> buffer(line, line + length);
  buffer.push_back('\0');

  struct ftpparse fp{};
  if (ftpparse(&fp, buffer.data(), static_cast<int>(length)) == 0) {
    return false;
  }
  if (fp.name == nullptr || fp.namelen <= 0) {
    return false;
  }

  out_entry.name.assign(fp.name, static_cast<size_t>(fp.namelen));
  out_entry.can_cwd = (fp.flagtrycwd != 0);
  out_entry.can_retr = (fp.flagtryretr != 0);

  if (fp.flagtrycwd && !fp.flagtryretr) {
    out_entry.type = FtpEntryType_t::directory;
  } else if (fp.flagtrycwd && fp.flagtryretr) {
    out_entry.type = FtpEntryType_t::symlink;
  } else if (fp.flagtryretr) {
    out_entry.type = FtpEntryType_t::file;
  } else {
    out_entry.type = FtpEntryType_t::unknown;
  }

  out_entry.size = static_cast<uint64_t>(fp.size > 0 ? fp.size : 0);
  out_entry.mtime = static_cast<int64_t>(fp.mtime);

  return true;
}
