// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

constexpr size_t k_file_browser_path_max = 260;
constexpr size_t k_file_browser_cache_max = 32;

// Backward-compatibility aliases for frontends
constexpr size_t FILE_BROWSER_PATH_MAX = k_file_browser_path_max;
constexpr size_t FILE_BROWSER_CACHE_MAX = k_file_browser_cache_max;

enum FileEntryType_t : uint8_t {
  FILE_ENTRY_UP = 0,
  FILE_ENTRY_DIR,
  FILE_ENTRY_FILE
};

struct FileEntry_t {
  char name[k_file_browser_path_max];
  FileEntryType_t type;
  uint64_t size;
};

[[nodiscard]] auto file_entry_is_dir_type(const FileEntry_t* entry) -> bool;

auto file_entry_format_type_or_size(const FileEntry_t* entry, char* out_str,
                                    size_t max_len) -> void;

struct FileList_t;

struct FileListGenerator_t {
  void* context = nullptr;
  auto (*generate_file_list)(FileListGenerator_t* self)
      -> FileList_t* = nullptr;
  auto (*get_starting_message)(FileListGenerator_t* self) -> const
      char* = nullptr;
  auto (*get_failure_message)(FileListGenerator_t* self) -> const
      char* = nullptr;
  void (*destroy)(FileListGenerator_t* self) = nullptr;
};

[[nodiscard]] auto file_browser_is_extension_supported(
    const char* filename, const char* allowed_extensions) -> bool;

[[nodiscard]] auto file_browser_create_local_generator(
    const char* directory, const char* filter_extensions)
    -> FileListGenerator_t*;

[[nodiscard]] auto file_browser_create_ftp_generator(
    const char* directory, const char* filter_extensions)
    -> FileListGenerator_t*;

[[nodiscard]] auto file_browser_create_list() -> FileList_t*;
auto file_browser_free_list(FileList_t* list) -> void;
auto file_browser_append_entry(FileList_t* list, const FileEntry_t* entry)
    -> void;
auto file_browser_set_failure_message(FileList_t* list, const char* msg)
    -> void;
auto file_browser_sort_list(FileList_t* list) -> void;

[[nodiscard]] auto file_browser_get_count(const FileList_t* list) -> size_t;
[[nodiscard]] auto file_browser_get_entry(const FileList_t* list, size_t index)
    -> const FileEntry_t*;
[[nodiscard]] auto file_browser_get_failure_message(const FileList_t* list)
    -> const char*;

struct DiskBrowser_t {
  int slot = 0;
  int drive = 0;
  char current_dir[k_file_browser_path_max]{};
  FileList_t* list_handle = nullptr;
  FileListGenerator_t* generator = nullptr;
  size_t selected_index = 0;
  size_t first_visible_index = 0;
  bool is_active = false;
};

auto disk_browser_open(DiskBrowser_t* b, int slot, int drive,
                       const char* start_dir) -> bool;
auto disk_browser_close(DiskBrowser_t* b) -> void;
auto disk_browser_refresh(DiskBrowser_t* b) -> void;
auto disk_browser_move(DiskBrowser_t* b, int delta, size_t page_size) -> void;
auto disk_browser_page(DiskBrowser_t* b, int direction, size_t page_size)
    -> void;
auto disk_browser_home(DiskBrowser_t* b) -> void;
auto disk_browser_end(DiskBrowser_t* b, size_t page_size) -> void;
auto disk_browser_jump_char(DiskBrowser_t* b, char ch, size_t page_size)
    -> void;
auto disk_browser_confirm(DiskBrowser_t* b) -> bool;
[[nodiscard]] auto disk_browser_get_title(int slot) -> const char*;
