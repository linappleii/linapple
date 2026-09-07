// SPDX-License-Identifier: GPL-2.0-only
#include "core/config/Toml.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace {

auto trim(const std::string& str) -> std::string {
  const auto start =
      std::find_if_not(str.begin(), str.end(),
                       [](unsigned char ch) { return std::isspace(ch) != 0; });
  if (start == str.end()) {
    return "";
  }
  const auto end =
      std::find_if_not(str.rbegin(), str.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
      }).base();
  return {start, end};
}

auto is_hex_color(const std::string& str) -> bool {
  if (str.size() != 7 && str.size() != 9) {
    return false;
  }
  if (str[0] != '#') {
    return false;
  }
  for (size_t i = 1; i < str.size(); ++i) {
    if (std::isxdigit(static_cast<unsigned char>(str[i])) == 0) {
      return false;
    }
  }
  return true;
}

auto format_float(double val) -> std::string {
  std::ostringstream float_stream;
  float_stream << val;
  std::string str = float_stream.str();
  if (str.find('.') == std::string::npos &&
      str.find('e') == std::string::npos) {
    str += ".0";
  }
  return str;
}

auto is_all_whitespace(const char* ptr) -> bool {
  if (ptr == nullptr) {
    return true;
  }
  while (*ptr != '\0') {
    if (std::isspace(static_cast<unsigned char>(*ptr)) == 0) {
      return false;
    }
    ptr++;
  }
  return true;
}

auto is_escaped(const std::string& str, size_t pos) -> bool {
  size_t backslash_count = 0;
  while (pos > 0 && str[pos - 1] == '\\') {
    backslash_count++;
    pos--;
  }
  return (backslash_count % 2) != 0;
}

auto is_hex_color_token(const std::string& line, size_t pos) -> size_t {
  if (pos == 0 || line[pos] != '#') {
    return 0;
  }
  const char prev = line[pos - 1];
  if (std::isspace(static_cast<unsigned char>(prev)) == 0 && prev != '=' &&
      prev != '[' && prev != ',') {
    return 0;
  }
  size_t hex_len = 0;
  while (pos + 1 + hex_len < line.size() &&
         std::isxdigit(static_cast<unsigned char>(line[pos + 1 + hex_len])) !=
             0) {
    hex_len++;
  }
  if (hex_len != 6 && hex_len != 8) {
    return 0;
  }
  const size_t next_pos = pos + 1 + hex_len;
  if (next_pos < line.size()) {
    const char next_char = line[next_pos];
    if (std::isspace(static_cast<unsigned char>(next_char)) == 0 &&
        next_char != ',' && next_char != ']' && next_char != '#') {
      return 0;
    }
  }
  return 1 + hex_len;
}

auto strip_inline_comment(const std::string& line, std::string* out_comment)
    -> std::string {
  bool in_quotes = false;
  char quote_char = '\0';

  for (size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (in_quotes) {
      if (ch == quote_char && !is_escaped(line, i)) {
        in_quotes = false;
      }
      continue;
    }

    if (ch == '"' || ch == '\'') {
      const bool can_open_quote =
          (i == 0 ||
           std::isspace(static_cast<unsigned char>(line[i - 1])) != 0 ||
           line[i - 1] == '=' || line[i - 1] == '[' || line[i - 1] == ',');
      if (can_open_quote) {
        in_quotes = true;
        quote_char = ch;
        continue;
      }
    }

    if (ch == '#') {
      const size_t hex_tok_len = is_hex_color_token(line, i);
      if (hex_tok_len > 0) {
        i += hex_tok_len - 1;
        continue;
      }
      if (out_comment != nullptr) {
        *out_comment = trim(line.substr(i + 1));
      }
      return line.substr(0, i);
    }
  }
  return line;
}

auto unescape_string(const std::string& str) -> std::string {
  if (str.size() >= 2 && ((str.front() == '"' && str.back() == '"') ||
                          (str.front() == '\'' && str.back() == '\''))) {
    const char quote = str.front();
    const std::string inner = str.substr(1, str.size() - 2);
    if (quote == '\'') {
      return inner;
    }
    std::string result;
    result.reserve(inner.size());
    for (size_t i = 0; i < inner.size(); ++i) {
      if (inner[i] == '\\' && i + 1 < inner.size()) {
        const char next = inner[++i];
        switch (next) {
          case 'n':
            result += '\n';
            break;
          case 'r':
            result += '\r';
            break;
          case 't':
            result += '\t';
            break;
          case '\\':
            result += '\\';
            break;
          case '"':
            result += '"';
            break;
          default:
            result += next;
            break;
        }
      } else {
        result += inner[i];
      }
    }
    return result;
  }
  return str;
}

auto escape_string(const std::string& str) -> std::string {
  std::string result = "\"";
  for (const char ch : str) {
    switch (ch) {
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '"':
        result += "\\\"";
        break;
      default:
        result += ch;
        break;
    }
  }
  result += "\"";
  return result;
}

auto parse_scalar_value(const std::string& raw_val) -> TomlValue_t {
  TomlValue_t val;
  const std::string trimmed = trim(raw_val);

  if (trimmed.empty()) {
    val.type = TomlType_t::String;
    val.string_val = "";
    return val;
  }

  // Quoted string
  if ((trimmed.front() == '"' && trimmed.back() == '"') ||
      (trimmed.front() == '\'' && trimmed.back() == '\'')) {
    val.type = TomlType_t::String;
    val.string_val = unescape_string(trimmed);
    return val;
  }

  // Boolean
  std::string lower = trimmed;
  std::transform(
      lower.begin(), lower.end(), lower.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  if (lower == "true" || lower == "yes" || lower == "on") {
    val.type = TomlType_t::Boolean;
    val.bool_val = true;
    return val;
  }
  if (lower == "false" || lower == "no" || lower == "off") {
    val.type = TomlType_t::Boolean;
    val.bool_val = false;
    return val;
  }

  // Hex color code (e.g. #C0C0C0)
  if (is_hex_color(trimmed)) {
    val.type = TomlType_t::String;
    val.string_val = trimmed;
    return val;
  }

  // Hex integer: 0x... or $...
  if (trimmed.size() > 2 &&
      (trimmed.substr(0, 2) == "0x" || trimmed.substr(0, 2) == "0X")) {
    char* end_ptr = nullptr;
    const unsigned long long parsed =
        std::strtoull(trimmed.c_str() + 2, &end_ptr, 16);
    if (end_ptr != nullptr && *end_ptr == '\0') {
      val.type = TomlType_t::Integer;
      val.int_val = static_cast<int64_t>(parsed);
      return val;
    }
  }
  if (trimmed.size() > 1 && trimmed[0] == '$') {
    char* end_ptr = nullptr;
    const unsigned long long parsed =
        std::strtoull(trimmed.c_str() + 1, &end_ptr, 16);
    if (end_ptr != nullptr && *end_ptr == '\0') {
      val.type = TomlType_t::Integer;
      val.int_val = static_cast<int64_t>(parsed);
      return val;
    }
  }

  // Speed multiplier suffix (e.g. "1.0x", "2.5x")
  std::string num_str = trimmed;
  if (num_str.size() > 1 && (num_str.back() == 'x' || num_str.back() == 'X') &&
      (std::isdigit(static_cast<unsigned char>(num_str[num_str.size() - 2])) !=
       0)) {
    num_str.pop_back();
  }

  // Decimal Integer or Float
  char* end_ptr = nullptr;
  const long long parsed_int = std::strtoll(num_str.c_str(), &end_ptr, 10);
  if (end_ptr != nullptr && *end_ptr == '\0' && num_str == trimmed) {
    val.type = TomlType_t::Integer;
    val.int_val = static_cast<int64_t>(parsed_int);
    return val;
  }

  const double parsed_float = std::strtod(num_str.c_str(), &end_ptr);
  if (end_ptr != nullptr && *end_ptr == '\0' &&
      (num_str.find('.') != std::string::npos || num_str != trimmed)) {
    val.type = TomlType_t::Float;
    val.float_val = parsed_float;
    return val;
  }

  // Fallback: unquoted plain string (e.g. "Apple //e Enhanced", "Color
  // Standard")
  val.type = TomlType_t::String;
  val.string_val = trimmed;
  return val;
}

auto parse_array_value(const std::string& raw_val) -> TomlValue_t {
  TomlValue_t array_val;
  array_val.type = TomlType_t::Array;

  std::string inner = trim(raw_val);
  if (inner.size() >= 2 && inner.front() == '[' && inner.back() == ']') {
    inner = inner.substr(1, inner.size() - 2);
  }

  bool in_quotes = false;
  char quote_char = '\0';
  std::string current_token;

  for (size_t i = 0; i < inner.size(); ++i) {
    const char ch = inner[i];
    if (in_quotes) {
      current_token += ch;
      if (ch == quote_char && !is_escaped(inner, i)) {
        in_quotes = false;
      }
      continue;
    }

    if (ch == '"' || ch == '\'') {
      const bool can_open_quote =
          (i == 0 ||
           std::isspace(static_cast<unsigned char>(inner[i - 1])) != 0 ||
           inner[i - 1] == '[' || inner[i - 1] == ',');
      if (can_open_quote) {
        in_quotes = true;
        quote_char = ch;
        current_token += ch;
        continue;
      }
    }

    if (ch == ',') {
      const std::string token = trim(current_token);
      if (!token.empty()) {
        array_val.array_val.push_back(parse_scalar_value(token));
      }
      current_token.clear();
      continue;
    }

    current_token += ch;
  }

  const std::string final_token = trim(current_token);
  if (!final_token.empty()) {
    array_val.array_val.push_back(parse_scalar_value(final_token));
  }

  return array_val;
}

auto parse_toml_value(const std::string& raw_val) -> TomlValue_t {
  const std::string trimmed = trim(raw_val);
  if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
    return parse_array_value(trimmed);
  }
  return parse_scalar_value(trimmed);
}

}  // namespace

auto toml_document_create() -> std::unique_ptr<TomlDocument_t> {
  return std::unique_ptr<TomlDocument_t>(new TomlDocument_t());
}

auto toml_document_parse(const std::string& content, std::string* out_error)
    -> std::unique_ptr<TomlDocument_t> {
  auto doc = toml_document_create();
  std::istringstream stream(content);
  std::string line;
  std::string current_section;
  size_t line_number = 0;

  while (std::getline(stream, line)) {
    line_number++;
    std::string comment;
    const std::string clean_line = trim(strip_inline_comment(line, &comment));

    if (clean_line.empty()) {
      continue;
    }

    // Section header: [SectionName]
    if (clean_line.front() == '[' && clean_line.back() == ']') {
      current_section = trim(clean_line.substr(1, clean_line.size() - 2));
      if (current_section.empty()) {
        if (out_error != nullptr) {
          *out_error =
              "Line " + std::to_string(line_number) + ": Empty section header";
        }
        return nullptr;
      }
      if (std::find(doc->section_order.begin(), doc->section_order.end(),
                    current_section) == doc->section_order.end()) {
        doc->section_order.push_back(current_section);
      }
      continue;
    }

    // Key = Value
    const size_t eq_pos = clean_line.find('=');
    if (eq_pos == std::string::npos) {
      if (out_error != nullptr) {
        *out_error = "Line " + std::to_string(line_number) +
                     ": Expected '=' in assignment: " + clean_line;
      }
      return nullptr;
    }

    const std::string key = trim(clean_line.substr(0, eq_pos));
    const std::string raw_val = trim(clean_line.substr(eq_pos + 1));

    if (key.empty()) {
      if (out_error != nullptr) {
        *out_error =
            "Line " + std::to_string(line_number) + ": Missing key before '='";
      }
      return nullptr;
    }

    TomlValue_t val = parse_toml_value(raw_val);
    val.comment = comment;

    if (!current_section.empty() &&
        std::find(doc->section_order.begin(), doc->section_order.end(),
                  current_section) == doc->section_order.end()) {
      doc->section_order.push_back(current_section);
    }
    doc->sections[current_section][key] = val;
  }

  return doc;
}

auto toml_document_load_file(const std::string& filepath,
                             std::string* out_error)
    -> std::unique_ptr<TomlDocument_t> {
  std::ifstream file(filepath);
  if (!file.is_open()) {
    if (out_error != nullptr) {
      *out_error = "Failed to open file for reading: " + filepath;
    }
    return nullptr;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return toml_document_parse(buffer.str(), out_error);
}

auto toml_document_serialize(const TomlDocument_t* doc) -> std::string {
  if (doc == nullptr) {
    return "";
  }

  std::ostringstream out;

  auto write_table = [&](const TomlTable_t& table) {
    bool is_first = true;
    for (const auto& kv : table) {
      const std::string& key = kv.first;
      const TomlValue_t& val = kv.second;

      if (!is_first && !val.pre_comment.empty()) {
        out << "\n";
      }
      is_first = false;

      if (!val.pre_comment.empty()) {
        std::istringstream pstream(val.pre_comment);
        std::string pline;
        while (std::getline(pstream, pline)) {
          if (pline.empty() || pline[0] != '#') {
            out << "# " << pline << "\n";
          } else {
            out << pline << "\n";
          }
        }
      }

      out << key << " = ";
      switch (val.type) {
        case TomlType_t::String:
          out << escape_string(val.string_val);
          break;
        case TomlType_t::Integer:
          out << val.int_val;
          break;
        case TomlType_t::Float:
          out << format_float(val.float_val);
          break;
        case TomlType_t::Boolean:
          out << (val.bool_val ? "true" : "false");
          break;
        case TomlType_t::Array: {
          out << "[";
          for (size_t i = 0; i < val.array_val.size(); ++i) {
            if (i > 0) {
              out << ", ";
            }
            const auto& elem = val.array_val[i];
            if (elem.type == TomlType_t::String) {
              out << escape_string(elem.string_val);
            } else if (elem.type == TomlType_t::Integer) {
              out << elem.int_val;
            } else if (elem.type == TomlType_t::Float) {
              out << format_float(elem.float_val);
            } else if (elem.type == TomlType_t::Boolean) {
              out << (elem.bool_val ? "true" : "false");
            }
          }
          out << "]";
          break;
        }
        case TomlType_t::Table:
        case TomlType_t::None:
          out << "\"\"";
          break;
      }
      if (!val.comment.empty()) {
        out << " # " << val.comment;
      }
      out << "\n";
    }
  };

  auto write_section_comment = [&](const std::string& sec_name) {
    const auto cit = doc->section_comments.find(sec_name);
    if (cit != doc->section_comments.end() && !cit->second.empty()) {
      std::istringstream cstream(cit->second);
      std::string cline;
      while (std::getline(cstream, cline)) {
        if (cline.empty() || cline[0] != '#') {
          out << "# " << cline << "\n";
        } else {
          out << cline << "\n";
        }
      }
      out << "\n";
    }
  };

  // 1. Root / Top-level keys first (section "")
  const auto root_it = doc->sections.find("");
  if (root_it != doc->sections.end() && !root_it->second.empty()) {
    write_table(root_it->second);
    out << "\n";
  }

  // 2. Sections in registered order
  for (const auto& section_name : doc->section_order) {
    if (section_name.empty()) {
      continue;
    }
    const auto sec_it = doc->sections.find(section_name);
    if (sec_it == doc->sections.end() || sec_it->second.empty()) {
      continue;
    }
    out << "[" << section_name << "]\n";
    write_section_comment(section_name);
    write_table(sec_it->second);
    out << "\n";
  }

  // 3. Any additional sections in doc->sections not tracked in section_order
  for (const auto& sec_kv : doc->sections) {
    const std::string& section_name = sec_kv.first;
    if (section_name.empty() || sec_kv.second.empty()) {
      continue;
    }
    if (std::find(doc->section_order.begin(), doc->section_order.end(),
                  section_name) != doc->section_order.end()) {
      continue;
    }
    out << "[" << section_name << "]\n";
    write_section_comment(section_name);
    write_table(sec_kv.second);
    out << "\n";
  }

  return out.str();
}

auto toml_document_save_file(const TomlDocument_t* doc,
                             const std::string& filepath,
                             std::string* out_error) -> bool {
  if (doc == nullptr) {
    if (out_error != nullptr) {
      *out_error = "Null document pointer";
    }
    return false;
  }
  std::ofstream file(filepath);
  if (!file.is_open()) {
    if (out_error != nullptr) {
      *out_error = "Failed to open file for writing: " + filepath;
    }
    return false;
  }
  file << toml_document_serialize(doc);
  return file.good();
}

auto toml_find_table(const TomlDocument_t* doc, const std::string& section)
    -> const TomlTable_t* {
  if (doc == nullptr) {
    return nullptr;
  }
  const auto it = doc->sections.find(section);
  if (it == doc->sections.end()) {
    return nullptr;
  }
  return &it->second;
}

auto toml_get_or_create_table(TomlDocument_t* doc, const std::string& section)
    -> TomlTable_t* {
  if (doc == nullptr) {
    return nullptr;
  }
  if (!section.empty() &&
      std::find(doc->section_order.begin(), doc->section_order.end(),
                section) == doc->section_order.end()) {
    doc->section_order.push_back(section);
  }
  return &doc->sections[section];
}

auto toml_table_has_key(const TomlTable_t* table, const std::string& key)
    -> bool {
  if (table == nullptr) {
    return false;
  }
  return table->find(key) != table->end();
}

auto toml_table_get_value(const TomlTable_t* table, const std::string& key)
    -> const TomlValue_t* {
  if (table == nullptr) {
    return nullptr;
  }
  const auto it = table->find(key);
  if (it == table->end()) {
    return nullptr;
  }
  return &it->second;
}

auto toml_table_get_string(const TomlTable_t* table, const std::string& key,
                           const std::string& default_val) -> std::string {
  const auto* val = toml_table_get_value(table, key);
  if (val == nullptr) {
    return default_val;
  }
  if (val->type == TomlType_t::String) {
    return val->string_val;
  }
  if (val->type == TomlType_t::Integer) {
    return std::to_string(val->int_val);
  }
  if (val->type == TomlType_t::Float) {
    return std::to_string(val->float_val);
  }
  if (val->type == TomlType_t::Boolean) {
    return val->bool_val ? "true" : "false";
  }
  return default_val;
}

auto toml_table_get_int(const TomlTable_t* table, const std::string& key,
                        int64_t default_val) -> int64_t {
  const auto* val = toml_table_get_value(table, key);
  if (val == nullptr) {
    return default_val;
  }
  if (val->type == TomlType_t::Integer) {
    return val->int_val;
  }
  if (val->type == TomlType_t::Float) {
    return static_cast<int64_t>(std::llround(val->float_val));
  }
  if (val->type == TomlType_t::Boolean) {
    return val->bool_val ? 1 : 0;
  }
  if (val->type == TomlType_t::String) {
    const std::string str = trim(val->string_val);
    if (str.empty()) {
      return default_val;
    }
    if (str.size() > 1 && str[0] == '$') {
      char* end_ptr = nullptr;
      const unsigned long long parsed =
          std::strtoull(str.c_str() + 1, &end_ptr, 16);
      if (end_ptr != nullptr && end_ptr != (str.c_str() + 1) &&
          is_all_whitespace(end_ptr)) {
        return static_cast<int64_t>(parsed);
      }
    } else {
      char* end_ptr = nullptr;
      const long long parsed = std::strtoll(str.c_str(), &end_ptr, 0);
      if (end_ptr != nullptr && end_ptr != str.c_str() &&
          is_all_whitespace(end_ptr)) {
        return static_cast<int64_t>(parsed);
      }
    }
  }
  return default_val;
}

auto toml_table_get_double(const TomlTable_t* table, const std::string& key,
                           double default_val) -> double {
  const auto* val = toml_table_get_value(table, key);
  if (val == nullptr) {
    return default_val;
  }
  if (val->type == TomlType_t::Float) {
    return val->float_val;
  }
  if (val->type == TomlType_t::Integer) {
    return static_cast<double>(val->int_val);
  }
  if (val->type == TomlType_t::String) {
    const std::string str = trim(val->string_val);
    if (str.empty()) {
      return default_val;
    }
    std::string num_str = str;
    if (num_str.size() > 1 &&
        (num_str.back() == 'x' || num_str.back() == 'X') &&
        (std::isdigit(
             static_cast<unsigned char>(num_str[num_str.size() - 2])) != 0)) {
      num_str.pop_back();
    }
    char* end_ptr = nullptr;
    const double parsed = std::strtod(num_str.c_str(), &end_ptr);
    if (end_ptr != nullptr && end_ptr != num_str.c_str() &&
        is_all_whitespace(end_ptr)) {
      return parsed;
    }
  }
  return default_val;
}

auto toml_table_get_bool(const TomlTable_t* table, const std::string& key,
                         bool default_val) -> bool {
  const auto* val = toml_table_get_value(table, key);
  if (val == nullptr) {
    return default_val;
  }
  if (val->type == TomlType_t::Boolean) {
    return val->bool_val;
  }
  if (val->type == TomlType_t::Integer) {
    return val->int_val != 0;
  }
  if (val->type == TomlType_t::String) {
    std::string lower = val->string_val;
    std::transform(
        lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "true" || lower == "yes" || lower == "on" || lower == "1") {
      return true;
    }
    if (lower == "false" || lower == "no" || lower == "off" || lower == "0") {
      return false;
    }
  }
  return default_val;
}

auto toml_table_get_array(const TomlTable_t* table, const std::string& key)
    -> const TomlArray_t* {
  const auto* val = toml_table_get_value(table, key);
  if (val == nullptr || val->type != TomlType_t::Array) {
    return nullptr;
  }
  return &val->array_val;
}

auto toml_document_set_section_comment(TomlDocument_t* doc,
                                       const std::string& section,
                                       const std::string& comment) -> void {
  if (doc == nullptr || section.empty()) {
    return;
  }
  doc->section_comments[section] = comment;
}

auto toml_table_set_string(TomlTable_t* table, const std::string& key,
                           const std::string& val, const std::string& comment,
                           const std::string& pre_comment) -> void {
  if (table == nullptr || key.empty()) {
    return;
  }
  TomlValue_t item;
  item.type = TomlType_t::String;
  item.string_val = val;
  item.comment = comment;
  item.pre_comment = pre_comment;
  (*table)[key] = item;
}

auto toml_table_set_int(TomlTable_t* table, const std::string& key, int64_t val,
                        const std::string& comment,
                        const std::string& pre_comment) -> void {
  if (table == nullptr || key.empty()) {
    return;
  }
  TomlValue_t item;
  item.type = TomlType_t::Integer;
  item.int_val = val;
  item.comment = comment;
  item.pre_comment = pre_comment;
  (*table)[key] = item;
}

auto toml_table_set_double(TomlTable_t* table, const std::string& key,
                           double val, const std::string& comment,
                           const std::string& pre_comment) -> void {
  if (table == nullptr || key.empty()) {
    return;
  }
  TomlValue_t item;
  item.type = TomlType_t::Float;
  item.float_val = val;
  item.comment = comment;
  item.pre_comment = pre_comment;
  (*table)[key] = item;
}

auto toml_table_set_bool(TomlTable_t* table, const std::string& key, bool val,
                         const std::string& comment,
                         const std::string& pre_comment) -> void {
  if (table == nullptr || key.empty()) {
    return;
  }
  TomlValue_t item;
  item.type = TomlType_t::Boolean;
  item.bool_val = val;
  item.comment = comment;
  item.pre_comment = pre_comment;
  (*table)[key] = item;
}

auto toml_table_set_array(TomlTable_t* table, const std::string& key,
                          const TomlArray_t& val, const std::string& comment,
                          const std::string& pre_comment) -> void {
  if (table == nullptr || key.empty()) {
    return;
  }
  TomlValue_t item;
  item.type = TomlType_t::Array;
  item.array_val = val;
  item.comment = comment;
  item.pre_comment = pre_comment;
  (*table)[key] = item;
}
