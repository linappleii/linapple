// SPDX-License-Identifier: GPL-2.0-only
#include "core/services/ftp/FtpParser.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>

#include "core/services/ftp/FtpTypes.h"

namespace {

enum class FtpSizeType_t : uint8_t {
  unknown = 0,
  binary,
  ascii,
};

enum class FtpMtimeType_t : uint8_t {
  unknown = 0,
  local,
  remote_minute,
  remote_day,
};

enum class FtpIdType_t : uint8_t {
  unknown = 0,
  full,
};

enum class UnixParserState_t : uint8_t {
  skip_perm = 1,
  skip_nlink,
  skip_uid,
  tentative_size,
  find_month,
  have_month,
  have_date,
};

struct FtpParsedEntry_t {
  const char* name = nullptr;
  size_t namelen = 0;
  bool flagtrycwd = false;
  bool flagtryretr = false;
  FtpSizeType_t sizetype = FtpSizeType_t::unknown;
  int64_t size = 0;
  FtpMtimeType_t mtimetype = FtpMtimeType_t::unknown;
  time_t mtime = 0;
  FtpIdType_t idtype = FtpIdType_t::unknown;
  const char* id = nullptr;
  size_t idlen = 0;
};

struct CurrentTime_t {
  int64_t now_seconds = 0;
  int64_t current_year = 0;
};

constexpr int64_t k_seconds_per_day = 86400;
constexpr int64_t k_seconds_per_hour = 3600;
constexpr int64_t k_seconds_per_minute = 60;
constexpr int64_t k_days_per_400_years = 146097;
constexpr int64_t k_days_per_100_years = 36524;
constexpr int64_t k_days_per_4_years = 1461;
constexpr int64_t k_days_per_year = 365;
constexpr int k_base_year_tm = 1900;
constexpr int64_t k_guess_max_days_past = 350;

constexpr std::array<const char*, 12> k_months = {{
    "jan",
    "feb",
    "mar",
    "apr",
    "may",
    "jun",
    "jul",
    "aug",
    "sep",
    "oct",
    "nov",
    "dec",
}};

auto totai(int64_t year, int64_t month, int64_t mday) noexcept -> int64_t {
  int64_t result = 0;
  constexpr int64_t k_month_offset = 2;
  constexpr int64_t k_month_adjust = 10;
  constexpr int64_t k_day_multiplier = 10;
  constexpr int64_t k_day_adjust = 5;
  constexpr int64_t k_month_multiplier = 306;
  constexpr int64_t k_leap_year_adjust = 3;
  constexpr int64_t k_days_per_4_years_minus_1 = 1460;
  constexpr int64_t k_four_year_cycle = 4;
  constexpr int64_t k_twenty_five_year_cycle = 25;
  constexpr int64_t k_days_per_400_years_minus_1 = 146096;
  constexpr int64_t k_year_offset = 5;
  constexpr int64_t k_constant_offset = 11017;

  if (month >= k_month_offset) {
    month -= k_month_offset;
  } else {
    month += k_month_adjust;
    --year;
  }
  result =
      (mday - 1) * k_day_multiplier + k_day_adjust + k_month_multiplier * month;
  result /= k_day_multiplier;
  if (result == k_days_per_year) {
    year -= k_leap_year_adjust;
    result = k_days_per_4_years_minus_1;
  } else {
    result += k_days_per_year * (year % k_four_year_cycle);
  }
  year /= k_four_year_cycle;
  result += k_days_per_4_years * (year % k_twenty_five_year_cycle);
  year /= k_twenty_five_year_cycle;
  if (result == k_days_per_100_years) {
    year -= k_leap_year_adjust;
    result = k_days_per_400_years_minus_1;
  } else {
    result += k_days_per_100_years * (year % k_four_year_cycle);
  }
  year /= k_four_year_cycle;
  result += k_days_per_400_years * (year - k_year_offset);
  result += k_constant_offset;
  return result * k_seconds_per_day;
}

auto get_time_base() noexcept -> int64_t {
  time_t zero = 0;
  struct tm t{};
  gmtime_r(&zero, &t);
  return -(totai(t.tm_year + k_base_year_tm, t.tm_mon, t.tm_mday) +
           static_cast<int64_t>(t.tm_hour * k_seconds_per_hour) +
           static_cast<int64_t>(t.tm_min * k_seconds_per_minute) + t.tm_sec);
}

auto get_current_time() noexcept -> CurrentTime_t {
  const time_t raw_time = std::time(nullptr);
  struct tm t{};
  gmtime_r(&raw_time, &t);
  CurrentTime_t result{};
  result.now_seconds = static_cast<int64_t>(raw_time);
  result.current_year = t.tm_year + k_base_year_tm;
  return result;
}

auto guesstai(int64_t month, int64_t mday, int64_t now_seconds,
              int64_t current_year) noexcept -> int64_t {
  constexpr int64_t k_year_search_limit = 100;
  for (int64_t year = current_year - 1;
       year < current_year + k_year_search_limit; ++year) {
    const int64_t t = totai(year, month, mday);
    if (now_seconds - t < k_guess_max_days_past * k_seconds_per_day) {
      return t;
    }
  }
  return 0;
}

constexpr auto ascii_tolower(char c) noexcept -> char {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

auto check_month(const char* buf, const char* month_name) noexcept -> bool {
  return ascii_tolower(buf[0]) == month_name[0] &&
         ascii_tolower(buf[1]) == month_name[1] &&
         ascii_tolower(buf[2]) == month_name[2];
}

auto getmonth(const char* buf, size_t len) noexcept -> int {
  if (len != 3) {
    return -1;
  }
  for (size_t i = 0; i < k_months.size(); ++i) {
    if (check_month(buf, k_months[i])) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

auto getlong(const char* buf, size_t len) noexcept -> uint64_t {
  uint64_t u = 0;
  while (len > 0) {
    --len;
    constexpr uint64_t k_base10 = 10;
    const uint64_t digit = static_cast<uint64_t>(*buf++ - '0');
    if (u <= (UINT64_MAX - digit) / k_base10) {
      u = u * k_base10 + digit;
    } else {
      u = UINT64_MAX;
    }
  }
  return u;
}

auto parse_eplf_fact(FtpParsedEntry_t& fp, char fact_type, const char* val,
                     size_t val_len, int64_t base) noexcept -> void {
  switch (fact_type) {
    case '/':
      fp.flagtrycwd = true;
      break;
    case 'r':
      fp.flagtryretr = true;
      break;
    case 's':
      fp.sizetype = FtpSizeType_t::binary;
      fp.size = static_cast<int64_t>(getlong(val, val_len));
      break;
    case 'm':
      fp.mtimetype = FtpMtimeType_t::local;
      fp.mtime = static_cast<time_t>(
          base + static_cast<int64_t>(getlong(val, val_len)));
      break;
    case 'i':
      fp.idtype = FtpIdType_t::full;
      fp.id = val;
      fp.idlen = val_len;
      break;
    default:
      break;
  }
}

// EPLF format: "+i8388621.29609,m824255902,/,\tdev"
auto parse_eplf(FtpParsedEntry_t& fp, const char* buf, size_t len) -> bool {
  const int64_t base = get_time_base();
  size_t i = 1;
  for (size_t j = 1; j < len; ++j) {
    if (buf[j] != '\t' && buf[j] != ',') {
      continue;
    }
    if (j > i) {
      parse_eplf_fact(fp, buf[i], buf + i + 1, j - i - 1, base);
    }
    i = j + 1;
    if (buf[j] == '\t') {
      fp.name = buf + j + 1;
      fp.namelen = len - j - 1;
      return true;
    }
  }
  return false;
}

constexpr auto is_unix_file_type(char c) noexcept -> bool {
  return c == '-' || c == 'd' || c == 'l' || c == 'b' || c == 'c' || c == 'p' ||
         c == 's';
}

auto parse_unix_time_or_year(FtpParsedEntry_t& fp, const char* token,
                             size_t token_len, int64_t month, int64_t mday,
                             int64_t base,
                             const CurrentTime_t& now_time) noexcept -> bool {
  if (token_len == 4 && token[1] == ':') {
    const auto hour = static_cast<int64_t>(getlong(token, 1));
    const auto minute = static_cast<int64_t>(getlong(token + 2, 2));
    fp.mtimetype = FtpMtimeType_t::remote_minute;
    fp.mtime = static_cast<time_t>(
        base +
        guesstai(month, mday, now_time.now_seconds, now_time.current_year) +
        hour * k_seconds_per_hour + minute * k_seconds_per_minute);
    return true;
  }
  if (token_len == 5 && token[2] == ':') {
    const auto hour = static_cast<int64_t>(getlong(token, 2));
    const auto minute = static_cast<int64_t>(getlong(token + 3, 2));
    fp.mtimetype = FtpMtimeType_t::remote_minute;
    fp.mtime = static_cast<time_t>(
        base +
        guesstai(month, mday, now_time.now_seconds, now_time.current_year) +
        hour * k_seconds_per_hour + minute * k_seconds_per_minute);
    return true;
  }
  if (token_len >= 4) {
    const auto year = static_cast<int64_t>(getlong(token, token_len));
    fp.mtimetype = FtpMtimeType_t::remote_day;
    fp.mtime = static_cast<time_t>(base + totai(year, month, mday));
    return true;
  }
  return false;
}

// UNIX ls format: "-rw-r--r-- 1 owner group 143360 Sep 19 12:00 file.dsk"
auto parse_unix(FtpParsedEntry_t& fp, const char* buf, size_t len) -> bool {
  if (buf[0] == 'd') {
    fp.flagtrycwd = true;
  } else if (buf[0] == '-') {
    fp.flagtryretr = true;
  } else if (buf[0] == 'l') {
    fp.flagtrycwd = true;
    fp.flagtryretr = true;
  }

  const int64_t base = get_time_base();
  const CurrentTime_t now_time = get_current_time();

  auto state = UnixParserState_t::skip_perm;
  uint64_t size = 0;
  int64_t month = 0;
  int64_t mday = 0;
  size_t i = 0;
  bool found_date = false;

  for (size_t j = 1; j < len; ++j) {
    if (buf[j] != ' ' || buf[j - 1] == ' ') {
      continue;
    }

    const size_t token_len = j - i;
    const char* token = buf + i;

    switch (state) {
      case UnixParserState_t::skip_perm:
        state = UnixParserState_t::skip_nlink;
        break;
      case UnixParserState_t::skip_nlink:
        state = (token_len == 6 && token[0] == 'f')
                    ? UnixParserState_t::tentative_size
                    : UnixParserState_t::skip_uid;
        break;
      case UnixParserState_t::skip_uid:
        state = UnixParserState_t::tentative_size;
        break;
      case UnixParserState_t::tentative_size:
        size = getlong(token, token_len);
        state = UnixParserState_t::find_month;
        break;
      case UnixParserState_t::find_month:
        month = getmonth(token, token_len);
        if (month >= 0) {
          state = UnixParserState_t::have_month;
        } else {
          size = getlong(token, token_len);
        }
        break;
      case UnixParserState_t::have_month:
        mday = static_cast<int64_t>(getlong(token, token_len));
        state = UnixParserState_t::have_date;
        break;
      case UnixParserState_t::have_date:
        if (!parse_unix_time_or_year(fp, token, token_len, month, mday, base,
                                     now_time)) {
          return false;
        }
        fp.name = buf + j + 1;
        fp.namelen = len - j - 1;
        found_date = true;
        break;
    }

    if (found_date) {
      break;
    }

    i = j + 1;
    while (i < len && buf[i] == ' ') {
      ++i;
    }
  }

  if (!found_date) {
    return false;
  }

  fp.size = static_cast<int64_t>(size);
  fp.sizetype = FtpSizeType_t::binary;

  if (buf[0] == 'l') {
    constexpr size_t k_arrow_len = 4;
    for (size_t k = 0; k + k_arrow_len <= fp.namelen; ++k) {
      if (fp.name[k] == ' ' && fp.name[k + 1] == '-' && fp.name[k + 2] == '>' &&
          fp.name[k + 3] == ' ') {
        fp.namelen = k;
        break;
      }
    }
  }

  // NetWare extra space elimination
  if ((buf[1] == ' ' || buf[1] == '[') && fp.namelen > 3 && fp.name[0] == ' ' &&
      fp.name[1] == ' ' && fp.name[2] == ' ') {
    fp.name += 3;
    fp.namelen -= 3;
  }

  return true;
}

constexpr size_t k_min_input_len = 2;

auto skip_until(const char* buf, size_t len, size_t& pos, char c) noexcept
    -> bool {
  while (pos < len && buf[pos] != c) {
    ++pos;
  }
  return pos < len;
}

auto skip_matching(const char* buf, size_t len, size_t& pos, char c) noexcept
    -> bool {
  while (pos < len && buf[pos] == c) {
    ++pos;
  }
  return pos < len;
}

// MultiNet / VMS format: "00README.TXT;1 2 30-DEC-1996 17:44 [SYSTEM]"
auto parse_vms(FtpParsedEntry_t& fp, const char* buf, size_t len) -> bool {
  size_t semicolon_pos = 0;
  while (semicolon_pos < len && buf[semicolon_pos] != ';') {
    ++semicolon_pos;
  }
  if (semicolon_pos == len) {
    return false;
  }

  fp.name = buf;
  fp.namelen = semicolon_pos;

  constexpr size_t k_vms_dir_suffix_len = 4;
  if (semicolon_pos > k_vms_dir_suffix_len && buf[semicolon_pos - 4] == '.' &&
      buf[semicolon_pos - 3] == 'D' && buf[semicolon_pos - 2] == 'I' &&
      buf[semicolon_pos - 1] == 'R') {
    fp.namelen -= k_vms_dir_suffix_len;
    fp.flagtrycwd = true;
  }
  if (!fp.flagtrycwd) {
    fp.flagtryretr = true;
  }

  size_t i = semicolon_pos;
  if (!skip_until(buf, len, i, ' ')) return false;
  if (!skip_matching(buf, len, i, ' ')) return false;
  if (!skip_until(buf, len, i, ' ')) return false;
  if (!skip_matching(buf, len, i, ' ')) return false;

  size_t j = i;
  if (!skip_until(buf, len, j, '-')) return false;
  const auto mday = static_cast<int64_t>(getlong(buf + i, j - i));
  if (!skip_matching(buf, len, j, '-')) return false;

  i = j;
  if (!skip_until(buf, len, j, '-')) return false;
  const auto month = getmonth(buf + i, j - i);
  if (month < 0) {
    return false;
  }
  if (!skip_matching(buf, len, j, '-')) return false;

  i = j;
  if (!skip_until(buf, len, j, ' ')) return false;
  const auto year = static_cast<int64_t>(getlong(buf + i, j - i));
  if (!skip_matching(buf, len, j, ' ')) return false;

  i = j;
  if (!skip_until(buf, len, j, ':')) return false;
  const auto hour = static_cast<int64_t>(getlong(buf + i, j - i));
  if (!skip_matching(buf, len, j, ':')) return false;

  i = j;
  while (j < len && buf[j] != ':' && buf[j] != ' ') {
    ++j;
  }
  if (j == len) {
    return false;
  }
  const auto minute = static_cast<int64_t>(getlong(buf + i, j - i));

  const int64_t base = get_time_base();
  fp.mtimetype = FtpMtimeType_t::remote_minute;
  fp.mtime = static_cast<time_t>(base + totai(year, month, mday) +
                                 hour * k_seconds_per_hour +
                                 minute * k_seconds_per_minute);
  return true;
}

// MSDOS / Windows NT format: "04-27-00 09:09PM <DIR> licensed"
auto parse_dos(FtpParsedEntry_t& fp, const char* buf, size_t len) -> bool {
  if (buf == nullptr || len < k_min_input_len || buf[0] < '0' || buf[0] > '9') {
    return false;
  }

  size_t i = 0;
  size_t j = 0;
  if (!skip_until(buf, len, j, '-')) return false;
  const auto month = static_cast<int64_t>(getlong(buf + i, j - i)) - 1;
  if (!skip_matching(buf, len, j, '-')) return false;

  i = j;
  if (!skip_until(buf, len, j, '-')) return false;
  const auto mday = static_cast<int64_t>(getlong(buf + i, j - i));
  if (!skip_matching(buf, len, j, '-')) return false;

  i = j;
  if (!skip_until(buf, len, j, ' ')) return false;
  auto year = static_cast<int64_t>(getlong(buf + i, j - i));
  constexpr int64_t k_two_digit_cutoff = 50;
  constexpr int64_t k_year_2000 = 2000;
  constexpr int64_t k_three_digit_cutoff = 1000;
  constexpr int64_t k_year_1900 = 1900;
  if (year < k_two_digit_cutoff) {
    year += k_year_2000;
  } else if (year < k_three_digit_cutoff) {
    year += k_year_1900;
  }
  if (!skip_matching(buf, len, j, ' ')) return false;

  i = j;
  if (!skip_until(buf, len, j, ':')) return false;
  auto hour = static_cast<int64_t>(getlong(buf + i, j - i));
  if (!skip_matching(buf, len, j, ':')) return false;

  i = j;
  while (j < len && buf[j] != 'A' && buf[j] != 'P') {
    ++j;
  }
  if (j == len) {
    return false;
  }
  const auto minute = static_cast<int64_t>(getlong(buf + i, j - i));
  constexpr int64_t k_noon_hour = 12;
  if (hour == k_noon_hour) {
    hour = 0;
  }
  if (buf[j] == 'P') {
    hour += k_noon_hour;
  }
  if (++j == len) {
    return false;
  }
  if (buf[j] == 'M' && ++j == len) {
    return false;
  }

  if (!skip_matching(buf, len, j, ' ')) return false;

  const size_t token_start = j;
  if (!skip_until(buf, len, j, ' ')) return false;

  if (buf[token_start] == '<') {
    fp.flagtrycwd = true;
  } else {
    fp.size = static_cast<int64_t>(getlong(buf + token_start, j - token_start));
    fp.sizetype = FtpSizeType_t::binary;
    fp.flagtryretr = true;
  }

  if (!skip_matching(buf, len, j, ' ')) return false;

  fp.name = buf + j;
  fp.namelen = len - j;

  const int64_t base = get_time_base();
  fp.mtimetype = FtpMtimeType_t::remote_minute;
  fp.mtime = static_cast<time_t>(base + totai(year, month, mday) +
                                 hour * k_seconds_per_hour +
                                 minute * k_seconds_per_minute);
  return true;
}

auto ftpparse(FtpParsedEntry_t& fp, const char* buf, size_t len) -> bool {
  if (buf == nullptr || len < 2) {
    return false;
  }

  if (buf[0] == '+') {
    return parse_eplf(fp, buf, len);
  }
  if (is_unix_file_type(buf[0])) {
    return parse_unix(fp, buf, len);
  }
  if (parse_vms(fp, buf, len)) {
    return true;
  }
  if (buf[0] >= '0' && buf[0] <= '9') {
    return parse_dos(fp, buf, len);
  }
  return false;
}

}  // namespace

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

  FtpParsedEntry_t fp{};
  if (!ftpparse(fp, line, length)) {
    return false;
  }
  if (fp.name == nullptr || fp.namelen == 0) {
    return false;
  }

  out_entry.name.assign(fp.name, fp.namelen);
  out_entry.can_cwd = fp.flagtrycwd;
  out_entry.can_retr = fp.flagtryretr;

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
