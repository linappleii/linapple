// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>

constexpr size_t file_browser_path_max = 260;
constexpr size_t file_browser_cache_max = 32;

enum FileEntryType : uint8_t {
  FILE_ENTRY_UP = 0,
  FILE_ENTRY_DIR,
  FILE_ENTRY_FILE,
};

struct FileEntry {
  char name[file_browser_path_max];
  FileEntryType type;
  uint64_t size;
};

auto file_entry_is_dir_type(const FileEntry* entry) -> bool;

auto file_entry_format_type_or_size(const FileEntry* entry, char* out_str,
                                    size_t max_len) -> void;

struct FileList;

struct FileListGenerator {
  void* context = nullptr;
  auto (*generate_file_list)(FileListGenerator* self) -> FileList* = nullptr;
  auto (*get_starting_message)(FileListGenerator* self) -> const
      char* = nullptr;
  auto (*get_failure_message)(FileListGenerator* self) -> const char* = nullptr;
  auto (*destroy)(FileListGenerator* self) -> void = nullptr;
};

auto file_browser_is_extension_supported(const char* filename,
                                         const char* allowed_extensions)
    -> bool;

auto file_browser_create_local_generator(const char* directory,
                                         const char* filter_extensions)
    -> FileListGenerator*;

auto file_browser_create_ftp_generator(const char* directory,
                                       const char* filter_extensions)
    -> FileListGenerator*;

auto file_browser_create_list() -> FileList*;
auto file_browser_free_list(FileList* list) -> void;
auto file_browser_append_entry(FileList* list, const FileEntry* entry) -> void;
auto file_browser_set_failure_message(FileList* list, const char* msg) -> void;
auto file_browser_sort_list(FileList* list) -> void;

auto file_browser_get_count(const FileList* list) -> size_t;
auto file_browser_get_entry(const FileList* list, size_t index)
    -> const FileEntry*;
auto file_browser_get_failure_message(const FileList* list) -> const char*;

struct DiskBrowser {
  int slot = 0;
  int drive = 0;
  char current_dir[file_browser_path_max]{};
  FileList* list_handle = nullptr;
  FileListGenerator* generator = nullptr;
  size_t selected_index = 0;
  size_t first_visible_index = 0;
  bool is_active = false;
};

auto disk_browser_open(DiskBrowser* b, int slot, int drive,
                       const char* start_dir) -> bool;
auto disk_browser_close(DiskBrowser* b) -> void;
auto disk_browser_refresh(DiskBrowser* b) -> void;
auto disk_browser_move(DiskBrowser* b, int delta, size_t page_size) -> void;
auto disk_browser_page(DiskBrowser* b, int direction, size_t page_size) -> void;
auto disk_browser_home(DiskBrowser* b) -> void;
auto disk_browser_end(DiskBrowser* b, size_t page_size) -> void;
auto disk_browser_jump_char(DiskBrowser* b, char ch, size_t page_size) -> void;
auto disk_browser_confirm(DiskBrowser* b) -> bool;
auto disk_browser_get_title(int slot) noexcept -> const char*;
