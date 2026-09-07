// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

enum class TomlType_t {
  None = 0,
  String,
  Integer,
  Float,
  Boolean,
  Array,
  Table
};

struct TomlValue_t;

using TomlArray_t = std::vector<TomlValue_t>;
using TomlTable_t = std::map<std::string, TomlValue_t>;

struct TomlValue_t {
  TomlType_t type = TomlType_t::None;
  std::string string_val;
  int64_t int_val = 0;
  double float_val = 0.0;
  bool bool_val = false;
  TomlArray_t array_val;
  TomlTable_t table_val;
  std::string comment;
  std::string pre_comment;
};

struct TomlDocument_t {
  std::vector<std::string> section_order;
  std::map<std::string, TomlTable_t> sections;
  std::map<std::string, std::string> section_comments;
};

auto toml_document_create() -> std::unique_ptr<TomlDocument_t>;

auto toml_document_parse(const std::string& content,
                         std::string* out_error = nullptr)
    -> std::unique_ptr<TomlDocument_t>;

auto toml_document_load_file(const std::string& filepath,
                             std::string* out_error = nullptr)
    -> std::unique_ptr<TomlDocument_t>;

auto toml_document_serialize(const TomlDocument_t* doc) -> std::string;

auto toml_document_save_file(const TomlDocument_t* doc,
                             const std::string& filepath,
                             std::string* out_error = nullptr) -> bool;

auto toml_find_table(const TomlDocument_t* doc, const std::string& section)
    -> const TomlTable_t*;

auto toml_get_or_create_table(TomlDocument_t* doc, const std::string& section)
    -> TomlTable_t*;

auto toml_table_has_key(const TomlTable_t* table, const std::string& key)
    -> bool;

auto toml_table_get_value(const TomlTable_t* table, const std::string& key)
    -> const TomlValue_t*;

auto toml_table_get_string(const TomlTable_t* table, const std::string& key,
                           const std::string& default_val = "") -> std::string;

auto toml_table_get_int(const TomlTable_t* table, const std::string& key,
                        int64_t default_val = 0) -> int64_t;

auto toml_table_get_double(const TomlTable_t* table, const std::string& key,
                           double default_val = 0.0) -> double;

auto toml_table_get_bool(const TomlTable_t* table, const std::string& key,
                         bool default_val = false) -> bool;

auto toml_table_get_array(const TomlTable_t* table, const std::string& key)
    -> const TomlArray_t*;

auto toml_document_set_section_comment(TomlDocument_t* doc,
                                       const std::string& section,
                                       const std::string& comment) -> void;

auto toml_table_set_string(TomlTable_t* table, const std::string& key,
                           const std::string& val,
                           const std::string& comment = "",
                           const std::string& pre_comment = "") -> void;

auto toml_table_set_int(TomlTable_t* table, const std::string& key, int64_t val,
                        const std::string& comment = "",
                        const std::string& pre_comment = "") -> void;

auto toml_table_set_double(TomlTable_t* table, const std::string& key,
                           double val, const std::string& comment = "",
                           const std::string& pre_comment = "") -> void;

auto toml_table_set_bool(TomlTable_t* table, const std::string& key, bool val,
                         const std::string& comment = "",
                         const std::string& pre_comment = "") -> void;

auto toml_table_set_array(TomlTable_t* table, const std::string& key,
                          const TomlArray_t& val,
                          const std::string& comment = "",
                          const std::string& pre_comment = "") -> void;
