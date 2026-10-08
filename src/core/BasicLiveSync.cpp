// SPDX-License-Identifier: GPL-2.0-only
#include "core/BasicLiveSync.h"

#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <ios>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "apple2/Apple2Types.h"
#include "apple2/Memory.h"
#include "core/Log.h"

namespace {

constexpr uint16_t addr_txttab =
    0x67;  // $67/$68: TXTTAB (Start of BASIC program)
constexpr uint16_t addr_vartab =
    0x69;  // $69/$6A: VARTAB (Start of simple variables)
constexpr uint16_t addr_arytab =
    0x6B;  // $6B/$6C: ARYTAB (Start of array variables)
constexpr uint16_t addr_strend =
    0x6D;  // $6D/$6E: STREND (End of array variables / start of free string
           // space)
constexpr uint16_t addr_fretop =
    0x6F;  // $6F/$70: FRETOP (Top of free string memory)
constexpr uint16_t addr_himem = 0x73;   // $73/$74: HIMEM (Top of BASIC memory)
constexpr uint16_t addr_prgend = 0xAF;  // $AF/$B0: PRGEND (End of program)
constexpr uint16_t default_txttab = 0x0801;
constexpr uint16_t default_himem = 0x9600;
constexpr uint16_t hard_himem_ceiling = 0xC000;
constexpr uint16_t max_line_number = 63999;
constexpr size_t max_input_line_len = 255;
constexpr size_t inotify_event_buf_size = 4096;
constexpr int frame_check_interval = 15;
constexpr uint8_t byte_shift = 8;
constexpr uint8_t byte_mask = 0xFF;
constexpr uint32_t djb2_init = 5381;
constexpr uint32_t djb2_shift = 5;
constexpr unsigned char ascii_printable_min = 32;
constexpr unsigned char ascii_printable_max = 126;
constexpr uint8_t token_rem = 0xB2;
constexpr uint8_t high_bit_mask = 0x80;
constexpr uint8_t ascii_7bit_mask = 0x7F;

struct TokenDef_t {
  uint8_t token;
  const char* name;
  size_t length;
};

// Applesoft BASIC tokens ($80..$EA) ordered by keyword length descending
// to ensure longest-prefix matching (e.g. ATN before AT, HCOLOR= before COLOR=)
static const std::array<TokenDef_t, 107> applesoft_tokens = {
    {
        {0x92, "HCOLOR=", 7}, {0x9C, "NOTRACE", 7}, {0x9E, "INVERSE", 7},
        {0xAE, "RESTORE", 7}, {0x99, "SCALE=", 6},  {0x9A, "SHLOAD", 6},
        {0x9D, "NORMAL", 6},  {0xA0, "COLOR=", 6},  {0xA3, "HIMEM:", 6},
        {0xA4, "LOMEM:", 6},  {0xA6, "RESUME", 6},  {0xA7, "RECALL", 6},
        {0xA9, "SPEED=", 6},  {0xB1, "RETURN", 6},  {0xE9, "RIGHT$", 6},
        {0x84, "INPUT", 5},   {0x93, "HPLOT", 5},   {0x95, "XDRAW", 5},
        {0x9B, "TRACE", 5},   {0x9F, "FLASH", 5},   {0xA5, "ONERR", 5},
        {0xA8, "STORE", 5},   {0xB0, "GOSUB", 5},   {0xBA, "PRINT", 5},
        {0xBD, "CLEAR", 5},   {0xD7, "SCRN(", 5},   {0xE8, "LEFT$", 5},
        {0x82, "NEXT", 4},    {0x83, "DATA", 4},    {0x87, "READ", 4},
        {0x89, "TEXT", 4},    {0x8C, "CALL", 4},    {0x8D, "PLOT", 4},
        {0x8E, "HLIN", 4},    {0x8F, "VLIN", 4},    {0x90, "HGR2", 4},
        {0x94, "DRAW", 4},    {0x96, "HTAB", 4},    {0x97, "HOME", 4},
        {0x98, "ROT=", 4},    {0xA2, "VTAB", 4},    {0xAB, "GOTO", 4},
        {0xB3, "STOP", 4},    {0xB5, "WAIT", 4},    {0xB6, "LOAD", 4},
        {0xB7, "SAVE", 4},    {0xB9, "POKE", 4},    {0xBB, "CONT", 4},
        {0xBC, "LIST", 4},    {0xC0, "TAB(", 4},    {0xC3, "SPC(", 4},
        {0xC4, "THEN", 4},    {0xC7, "STEP", 4},    {0xE2, "PEEK", 4},
        {0xE4, "STR$", 4},    {0xE7, "CHR$", 4},    {0xEA, "MID$", 4},
        {0x80, "END", 3},     {0x81, "FOR", 3},     {0x85, "DEL", 3},
        {0x86, "DIM", 3},     {0x8A, "PR#", 3},     {0x8B, "IN#", 3},
        {0x91, "HGR", 3},     {0xA1, "POP", 3},     {0xAA, "LET", 3},
        {0xAC, "RUN", 3},     {0xB2, "REM", 3},     {0xB8, "DEF", 3},
        {0xBE, "GET", 3},     {0xBF, "NEW", 3},     {0xC6, "NOT", 3},
        {0xCD, "AND", 3},     {0xD2, "SGN", 3},     {0xD3, "INT", 3},
        {0xD4, "ABS", 3},     {0xD5, "USR", 3},     {0xD6, "FRE", 3},
        {0xD8, "PDL", 3},     {0xD9, "POS", 3},     {0xDA, "SQR", 3},
        {0xDB, "RND", 3},     {0xDC, "LOG", 3},     {0xDD, "EXP", 3},
        {0xDE, "COS", 3},     {0xDF, "SIN", 3},     {0xE0, "TAN", 3},
        {0xE1, "ATN", 3},     {0xE3, "LEN", 3},     {0xE5, "VAL", 3},
        {0xE6, "ASC", 3},     {0x88, "GR", 2},      {0xAD, "IF", 2},
        {0xB4, "ON", 2},      {0xC1, "TO", 2},      {0xC2, "FN", 2},
        {0xC5, "AT", 2},      {0xCE, "OR", 2},      {0xAF, "&", 1},
        {0xC8, "+", 1},       {0xC9, "-", 1},       {0xCA, "*", 1},
        {0xCB, "/", 1},       {0xCC, "^", 1},       {0xCF, ">", 1},
        {0xD0, "=", 1},       {0xD1, "<", 1},
    },
};

struct ParsedLine_t {
  uint16_t line_number = 0;
  std::vector<uint8_t> token_bytes;

  ParsedLine_t() = default;
  ParsedLine_t(uint16_t num, std::vector<uint8_t> bytes)
      : line_number(num), token_bytes(std::move(bytes)) {}
};

static BasicSyncConfig sync_config;
static std::string watch_dir;
static std::string watch_filename;
static uint32_t last_exported_hash = 0;
static uint32_t last_file_content_hash = 0;
static time_t last_file_mtime = 0;
static bool initial_import_pending = false;
static int frame_counter = 0;

static int inotify_fd = -1;
static int inotify_wd = -1;
static int inotify_dir_wd = -1;

static auto split_path(const std::string& full_path, std::string* out_dir,
                       std::string* out_filename) -> void {
  if (out_dir == nullptr || out_filename == nullptr) {
    return;
  }
  size_t pos = full_path.find_last_of("/\\");
  if (pos != std::string::npos) {
    *out_dir = (pos == 0) ? "/" : full_path.substr(0, pos);
    *out_filename = full_path.substr(pos + 1);
  } else {
    *out_dir = ".";
    *out_filename = full_path;
  }
  if (out_dir->empty()) {
    *out_dir = ".";
  }
}

static auto get_ram_byte_ptr(uint16_t addr) -> uint8_t* {
  if (mem == nullptr) {
    return nullptr;
  }
  return mem + addr;
}

static auto read_zero_page_16(uint16_t addr, uint16_t fallback) -> uint16_t {
  uint8_t* m = get_ram_byte_ptr(addr);
  if (m == nullptr) {
    return fallback;
  }
  uint8_t* m_high = get_ram_byte_ptr(static_cast<uint16_t>(addr + 1));
  if (m_high == nullptr) {
    return fallback;
  }
  return static_cast<uint16_t>(*m | (*m_high << byte_shift));
}

static auto write_zero_page_16(uint16_t addr, uint16_t val) -> void {
  uint8_t* m = get_ram_byte_ptr(addr);
  uint8_t* m_high = get_ram_byte_ptr(static_cast<uint16_t>(addr + 1));
  if (m == nullptr || m_high == nullptr) {
    return;
  }
  *m = static_cast<uint8_t>(val & byte_mask);
  *m_high = static_cast<uint8_t>((val >> byte_shift) & byte_mask);
}

static auto compute_string_hash(const std::string& str) -> uint32_t {
  uint32_t hash = djb2_init;
  for (char ch : str) {
    hash = ((hash << djb2_shift) + hash) + static_cast<unsigned char>(ch);
  }
  return hash;
}

static auto compute_program_hash() -> uint32_t {
  uint16_t txttab = read_zero_page_16(addr_txttab, default_txttab);
  uint16_t vartab = read_zero_page_16(addr_vartab, default_txttab);
  uint16_t prgend = read_zero_page_16(addr_prgend, default_txttab);
  uint16_t end_addr = std::max(vartab, prgend);
  if (end_addr <= txttab || end_addr > hard_himem_ceiling) {
    return 0;
  }

  uint32_t hash = djb2_init;
  for (uint32_t addr = txttab; addr < end_addr; ++addr) {
    uint8_t* ptr = get_ram_byte_ptr(static_cast<uint16_t>(addr));
    if (ptr == nullptr) {
      break;
    }
    hash = ((hash << djb2_shift) + hash) + *ptr;
  }
  return hash;
}

static auto is_uppercase_only_machine() -> bool {
  return (current_apple2_type == A2TYPE_APPLE2 ||
          current_apple2_type == A2TYPE_APPLE2PLUS ||
          current_apple2_type == A2TYPE_APPLE2JPLUS);
}

static auto sanitize_and_truncate_line(const std::string& input,
                                       bool force_uppercase) -> std::string {
  std::string result;
  result.reserve(std::min(input.length(), max_input_line_len));

  for (char ch : input) {
    auto uch = static_cast<unsigned char>(ch);
    if (uch < ascii_printable_min || uch > ascii_printable_max) {
      continue;
    }

    if (force_uppercase && uch >= 'a' && uch <= 'z') {
      uch = static_cast<unsigned char>(uch - ('a' - 'A'));
    }

    result.push_back(static_cast<char>(uch));
    if (result.length() >= max_input_line_len) {
      break;
    }
  }
  return result;
}

static auto iequals_prefix(const std::string& str, size_t pos, const char* kw,
                           size_t len) -> bool {
  if (pos + len > str.length()) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    auto c1 = static_cast<unsigned char>(str.at(pos + i));
    auto c2 = static_cast<unsigned char>(kw[i]);
    if (std::toupper(c1) != std::toupper(c2)) {
      return false;
    }
  }
  return true;
}

static auto find_token_name(uint8_t token) -> const char* {
  for (const auto& t : applesoft_tokens) {
    if (t.token == token) {
      return t.name;
    }
  }
  return nullptr;
}

static auto match_token(const std::string& content, size_t pos)
    -> const TokenDef_t* {
  for (const auto& t : applesoft_tokens) {
    if (iequals_prefix(content, pos, t.name, t.length)) {
      return &t;
    }
  }
  return nullptr;
}

static auto to_normalized_byte(char ch, bool force_uppercase) -> uint8_t {
  auto uch = static_cast<unsigned char>(ch);
  if (force_uppercase && uch >= 'a' && uch <= 'z') {
    uch = static_cast<unsigned char>(uch - ('a' - 'A'));
  }
  return uch;
}

static auto tokenize_line_content(const std::string& content,
                                  bool force_uppercase)
    -> std::vector<uint8_t> {
  std::vector<uint8_t> tokens;
  tokens.reserve(content.length());

  bool in_quotes = false;
  bool in_rem = false;
  size_t i = 0;

  while (i < content.length()) {
    char ch = content.at(i);

    if (ch == '"') {
      in_quotes = !in_quotes;
      tokens.push_back(static_cast<uint8_t>(ch));
      ++i;
      continue;
    }

    if (!in_quotes && !in_rem) {
      const auto* matched = match_token(content, i);
      if (matched != nullptr) {
        tokens.push_back(matched->token);
        in_rem = (matched->token == token_rem);
        i += matched->length;
        continue;
      }
    }

    tokens.push_back(to_normalized_byte(ch, force_uppercase));
    ++i;
  }

  return tokens;
}

static auto detokenize_line_bytes(uint16_t start_addr, uint16_t end_addr)
    -> std::string {
  std::string line;
  if (end_addr > start_addr) {
    line.reserve(end_addr - start_addr);
  }
  bool in_quotes = false;
  bool in_rem = false;

  for (uint16_t addr = start_addr; addr < end_addr; ++addr) {
    uint8_t* byte_ptr = get_ram_byte_ptr(addr);
    if (byte_ptr == nullptr || *byte_ptr == 0) {
      break;
    }

    uint8_t b = *byte_ptr;
    if (b == '"') {
      in_quotes = !in_quotes;
      line.push_back('"');
      continue;
    }

    if (in_quotes || in_rem || b < high_bit_mask) {
      line.push_back(static_cast<char>(b & ascii_7bit_mask));
      continue;
    }

    const char* token_name = find_token_name(b);
    if (token_name != nullptr) {
      in_rem = in_rem || (b == token_rem);
      line.append(token_name);
      continue;
    }

    std::ostringstream fallback;
    fallback << "?TOKEN_$" << std::hex << static_cast<int>(b) << std::dec
             << "?";
    line.append(fallback.str());
  }

  return line;
}

static auto parse_positional_line(const std::string& sanitized,
                                  uint32_t line_index, uint16_t* out_num,
                                  std::string* out_statement) -> bool {
  if (line_index > max_line_number) {
    return false;
  }
  *out_num = static_cast<uint16_t>(line_index);

  size_t pos = 0;
  while (pos < sanitized.length() && std::isdigit(sanitized.at(pos))) {
    pos++;
  }
  if (pos == 0 || pos >= sanitized.length() ||
      !std::isspace(sanitized.at(pos))) {
    *out_statement = sanitized;
    return true;
  }

  unsigned long val = (pos <= 5) ? std::stoul(sanitized.substr(0, pos))
                                 : static_cast<unsigned long>(max_line_number);
  if (val != line_index) {
    *out_statement = sanitized;
    return true;
  }

  while (pos < sanitized.length() && std::isspace(sanitized.at(pos))) {
    pos++;
  }
  *out_statement = sanitized.substr(pos);
  return true;
}

static auto parse_explicit_line(const std::string& sanitized, uint16_t* out_num,
                                std::string* out_statement) -> bool {
  size_t pos = 0;
  while (pos < sanitized.length() && std::isspace(sanitized.at(pos))) {
    pos++;
  }
  size_t num_start = pos;
  while (pos < sanitized.length() && std::isdigit(sanitized.at(pos))) {
    pos++;
  }
  if (pos == num_start) {
    return false;
  }

  size_t num_len = pos - num_start;
  unsigned long val = (num_len <= 5)
                          ? std::stoul(sanitized.substr(num_start, num_len))
                          : static_cast<unsigned long>(max_line_number);
  *out_num =
      static_cast<uint16_t>(std::min<unsigned long>(val, max_line_number));

  while (pos < sanitized.length() && std::isspace(sanitized.at(pos))) {
    pos++;
  }
  *out_statement = sanitized.substr(pos);
  return true;
}

static auto inject_program_lines(uint16_t txttab, uint16_t himem,
                                 const std::vector<ParsedLine_t>& lines)
    -> uint16_t {
  uint16_t current_addr = txttab;
  for (const auto& line : lines) {
    size_t line_size = 4 + line.token_bytes.size() + 1;
    if (current_addr + line_size + 2 >= himem) {
      Logger::warning(
          "BasicLiveSync: Program exceeded HIMEM ($%04X). Truncated at line "
          "%u\n",
          himem, line.line_number);
      break;
    }

    uint16_t next_line_addr = static_cast<uint16_t>(current_addr + line_size);
    uint8_t* ptr = get_ram_byte_ptr(current_addr);
    if (ptr == nullptr) {
      break;
    }

    ptr[0] = static_cast<uint8_t>(next_line_addr & byte_mask);
    ptr[1] = static_cast<uint8_t>((next_line_addr >> byte_shift) & byte_mask);
    ptr[2] = static_cast<uint8_t>(line.line_number & byte_mask);
    ptr[3] = static_cast<uint8_t>((line.line_number >> byte_shift) & byte_mask);

    for (size_t k = 0; k < line.token_bytes.size(); ++k) {
      ptr[4 + k] = line.token_bytes.at(k);
    }
    ptr[4 + line.token_bytes.size()] = 0x00;

    current_addr = next_line_addr;
  }

  uint8_t* end_ptr = get_ram_byte_ptr(current_addr);
  if (end_ptr != nullptr) {
    end_ptr[0] = 0x00;
    end_ptr[1] = 0x00;
    end_ptr[2] = 0x00;
  }

  return current_addr;
}

static auto check_inotify_events() -> bool {
  if (inotify_fd < 0) {
    return false;
  }

  alignas(struct inotify_event) std::array<char, inotify_event_buf_size> buf{};
  ssize_t len = read(inotify_fd, buf.data(), buf.size());
  if (len <= 0) {
    return false;
  }

  constexpr uint32_t modify_mask =
      IN_CLOSE_WRITE | IN_MODIFY | IN_MOVED_TO | IN_CREATE;

  ssize_t pos = 0;
  while (pos < len) {
    const auto* event =
        reinterpret_cast<const struct inotify_event*>(&buf.at(pos));

    bool is_file_event = (event->wd == inotify_wd);
    bool is_dir_event = (event->wd == inotify_dir_wd && event->len > 0 &&
                         watch_filename == event->name);

    if ((event->mask & modify_mask) != 0 && (is_file_event || is_dir_event)) {
      return true;
    }

    pos += static_cast<ssize_t>(sizeof(struct inotify_event) + event->len);
  }

  return false;
}

static auto rearm_file_watch() -> void {
  if (inotify_fd < 0) {
    return;
  }
  if (inotify_wd >= 0) {
    inotify_rm_watch(inotify_fd, inotify_wd);
  }
  inotify_wd = inotify_add_watch(inotify_fd, sync_config.file_path.c_str(),
                                 IN_CLOSE_WRITE | IN_MODIFY);
}

static auto inotify_teardown() noexcept -> void {
  if (inotify_fd < 0) {
    return;
  }
  if (inotify_wd >= 0) {
    inotify_rm_watch(inotify_fd, inotify_wd);
  }
  if (inotify_dir_wd >= 0) {
    inotify_rm_watch(inotify_fd, inotify_dir_wd);
  }
  close(inotify_fd);
  inotify_fd = -1;
  inotify_wd = -1;
  inotify_dir_wd = -1;
}

static auto check_file_mtime_changed() -> bool {
  struct stat st{};
  if (stat(sync_config.file_path.c_str(), &st) != 0) {
    return false;
  }
  return (last_file_mtime != 0 && st.st_mtime > last_file_mtime);
}

static auto append_line_prefix(std::ostringstream& ss, BasicLineMode mode,
                               uint16_t line_num,
                               uint32_t* expected_positional_line) -> void {
  if (mode != basic_line_mode_positional) {
    ss << line_num << " ";
    return;
  }

  while (*expected_positional_line < line_num &&
         *expected_positional_line <= max_line_number) {
    ss << "\n";
    (*expected_positional_line)++;
  }
  *expected_positional_line = line_num + 1;
}

}  // namespace

auto basic_sync_init(const char* file_path, BasicLineMode mode) -> void {
  basic_sync_shutdown();
  if (file_path == nullptr || file_path[0] == '\0') {
    return;
  }

  sync_config.file_path = file_path;
  sync_config.line_mode = mode;
  sync_config.enabled = true;
  last_exported_hash = compute_program_hash();
  last_file_content_hash = compute_string_hash("");
  frame_counter = 0;
  initial_import_pending = false;

  split_path(sync_config.file_path, &watch_dir, &watch_filename);

  // Clear live sync file on disk at startup so stale programs from previous
  // runs do not clobber boot
  {
    std::ofstream out(sync_config.file_path, std::ios::out | std::ios::trunc);
  }

  struct stat st{};
  if (stat(sync_config.file_path.c_str(), &st) == 0) {
    last_file_mtime = st.st_mtime;
  } else {
    last_file_mtime = 0;
  }

  inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (inotify_fd >= 0) {
    inotify_dir_wd =
        inotify_add_watch(inotify_fd, watch_dir.c_str(),
                          IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_MODIFY);
    inotify_wd = inotify_add_watch(inotify_fd, sync_config.file_path.c_str(),
                                   IN_CLOSE_WRITE | IN_MODIFY);
  }

  Logger::info("BasicLiveSync: initialized for '%s' (mode: %s)\n", file_path,
               mode == basic_line_mode_positional ? "positional" : "explicit");
}

auto basic_sync_shutdown() noexcept -> void {
  inotify_teardown();
  watch_dir.clear();
  watch_filename.clear();
  last_exported_hash = 0;
  last_file_content_hash = 0;
  last_file_mtime = 0;
  initial_import_pending = false;
  frame_counter = 0;
  sync_config = BasicSyncConfig{};
}

auto basic_sync_is_active() noexcept -> bool { return sync_config.enabled; }

auto basic_sync_get_config() noexcept -> const BasicSyncConfig& {
  return sync_config;
}

auto basic_sync_export_to_string(BasicLineMode mode) -> std::string {
  uint16_t txttab = read_zero_page_16(addr_txttab, default_txttab);
  uint16_t vartab = read_zero_page_16(addr_vartab, default_txttab);
  uint16_t himem = read_zero_page_16(addr_himem, default_himem);
  if (himem > hard_himem_ceiling) {
    himem = hard_himem_ceiling;
  }

  uint16_t program_end = himem;
  if (vartab > txttab && vartab <= himem) {
    program_end = vartab;
  }

  std::ostringstream ss;
  uint16_t current_addr = txttab;
  uint32_t expected_positional_line = 1;

  while (current_addr + 4 < program_end) {
    uint8_t* ptr = get_ram_byte_ptr(current_addr);
    if (ptr == nullptr) {
      break;
    }

    uint16_t next_line = static_cast<uint16_t>(ptr[0] | (ptr[1] << byte_shift));
    if (next_line == 0 || next_line <= current_addr + 4 ||
        next_line > program_end) {
      break;
    }

    uint16_t line_num = static_cast<uint16_t>(ptr[2] | (ptr[3] << byte_shift));
    if (line_num > max_line_number) {
      break;
    }
    append_line_prefix(ss, mode, line_num, &expected_positional_line);

    ss << detokenize_line_bytes(static_cast<uint16_t>(current_addr + 4),
                                next_line)
       << "\n";
    current_addr = next_line;
  }

  return ss.str();
}

auto basic_sync_import_from_string(const std::string& text,
                                   BasicLineMode mode) -> bool {
  uint16_t himem = read_zero_page_16(addr_himem, default_himem);
  if (himem == 0 || himem > hard_himem_ceiling) {
    himem = default_himem;
    write_zero_page_16(addr_himem, default_himem);
  }
  uint16_t txttab = read_zero_page_16(addr_txttab, default_txttab);
  if (txttab < default_txttab || txttab >= himem) {
    txttab = default_txttab;
    write_zero_page_16(addr_txttab, default_txttab);
  }

  // Applesoft expects the byte immediately preceding TXTTAB to be 0x00
  if (txttab > 0) {
    uint8_t* p0 = get_ram_byte_ptr(static_cast<uint16_t>(txttab - 1));
    if (p0 != nullptr) {
      *p0 = 0x00;
    }
  }

  bool force_uppercase = is_uppercase_only_machine();
  std::istringstream stream(text);
  std::string raw_line;
  std::vector<ParsedLine_t> lines;
  uint32_t file_line_index = 1;

  while (std::getline(stream, raw_line)) {
    std::string sanitized =
        sanitize_and_truncate_line(raw_line, force_uppercase);
    if (sanitized.empty()) {
      file_line_index++;
      continue;
    }

    uint16_t line_num = 0;
    std::string statement;
    bool parsed = (mode == basic_line_mode_positional)
                      ? parse_positional_line(sanitized, file_line_index,
                                              &line_num, &statement)
                      : parse_explicit_line(sanitized, &line_num, &statement);

    if (parsed) {
      lines.emplace_back(line_num,
                         tokenize_line_content(statement, force_uppercase));
    }
    file_line_index++;
  }

  std::sort(lines.begin(), lines.end(),
            [](const ParsedLine_t& a, const ParsedLine_t& b) {
              return a.line_number < b.line_number;
            });

  uint16_t current_addr = inject_program_lines(txttab, himem, lines);
  uint16_t var_start = static_cast<uint16_t>(current_addr + 2);

  write_zero_page_16(addr_prgend, current_addr);
  write_zero_page_16(addr_vartab, var_start);
  write_zero_page_16(addr_arytab, var_start);
  write_zero_page_16(addr_strend, var_start);
  write_zero_page_16(addr_fretop, himem);

  last_exported_hash = compute_program_hash();

  Logger::info(
      "BasicLiveSync: Injected %zu BASIC lines into RAM ($%04X-$%04X)\n",
      lines.size(), txttab, current_addr);
  return true;
}

auto basic_sync_import_from_string(const char* text, size_t length,
                                   BasicLineMode mode) -> bool {
  if (text == nullptr) {
    return false;
  }
  return basic_sync_import_from_string(std::string(text, length), mode);
}

auto basic_sync_export_file() -> bool {
  if (sync_config.file_path.empty()) {
    return false;
  }

  std::string text = basic_sync_export_to_string(sync_config.line_mode);
  uint32_t text_hash = compute_string_hash(text);

  if (text_hash == last_file_content_hash) {
    last_exported_hash = compute_program_hash();
    return true;
  }

  // Do not wipe out host file if memory is completely empty
  if (text.empty() && last_file_content_hash != 0) {
    return false;
  }

  std::ofstream out(sync_config.file_path, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    Logger::warning("BasicLiveSync: Unable to open file '%s' for writing\n",
                    sync_config.file_path.c_str());
    return false;
  }

  out << text;
  out.close();

  last_file_content_hash = text_hash;
  last_exported_hash = compute_program_hash();

  struct stat st{};
  if (stat(sync_config.file_path.c_str(), &st) == 0) {
    last_file_mtime = st.st_mtime;
  }

  return true;
}

auto basic_sync_import_file() -> bool {
  if (sync_config.file_path.empty()) {
    return false;
  }

  std::ifstream in(sync_config.file_path);
  if (!in.is_open()) {
    return false;
  }

  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  in.close();

  uint32_t content_hash = compute_string_hash(content);
  if (content_hash == last_file_content_hash &&
      compute_program_hash() == last_exported_hash) {
    return true;
  }

  struct stat st{};
  if (stat(sync_config.file_path.c_str(), &st) == 0) {
    last_file_mtime = st.st_mtime;
  }

  last_file_content_hash = content_hash;
  return basic_sync_import_from_string(content, sync_config.line_mode);
}

auto basic_sync_update() -> void {
  if (!sync_config.enabled) {
    return;
  }

  if (initial_import_pending) {
    initial_import_pending = false;
    static_cast<void>(basic_sync_import_file());
  }

  bool file_modified =
      (inotify_fd >= 0) ? check_inotify_events() : check_file_mtime_changed();

  if (file_modified) {
    rearm_file_watch();
    static_cast<void>(basic_sync_import_file());
    return;
  }

  if (++frame_counter < frame_check_interval) {
    return;
  }
  frame_counter = 0;

  uint32_t current_hash = compute_program_hash();
  if (current_hash != 0 && current_hash != last_exported_hash) {
    static_cast<void>(basic_sync_export_file());
  }
}
