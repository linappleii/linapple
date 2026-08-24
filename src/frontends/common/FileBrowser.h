#pragma once

// This header defines a dual C99/C++11 ABI. Specific checks are disabled to
// allow C-compatible patterns (plain enums, public POD members, manual memory
// management handles) which are required for frontend portability.
// NOLINTBEGIN(modernize-deprecated-headers, hicpp-deprecated-headers,
// modernize-use-using) NOLINTBEGIN(cppcoreguidelines-use-enum-class)
// NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// NOLINTBEGIN(modernize-use-auto, modernize-use-trailing-return-type)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { FILE_BROWSER_PATH_MAX = 260, FILE_BROWSER_CACHE_MAX = 32 };

typedef enum {
  FILE_ENTRY_UP = 0,
  FILE_ENTRY_DIR,
  FILE_ENTRY_FILE
} FileEntryType_t;

/**
 * Pure C layout (POD) ensures stable ABI across different frontend languages.
 */
typedef struct FileEntry_t {
  char name[FILE_BROWSER_PATH_MAX];
  FileEntryType_t type;
  uint64_t size;
} FileEntry_t;

auto file_entry_is_dir_type(const FileEntry_t* entry) -> bool;

void FileEntry_FormatTypeOrSize(const FileEntry_t* entry, char* out_str,
                                size_t max_len);

/**
 * Opaque handle hides internal implementation details (std::vector).
 */
typedef struct FileList_t FileList_t;

/**
 * Uses C-style polymorphism (function pointers) to support both local
 * and remote (FTP) providers in any frontend language.
 */
typedef struct FileListGenerator_t FileListGenerator_t;
struct FileListGenerator_t {
  void* context;
  auto (*generate_file_list)(FileListGenerator_t* self) -> FileList_t*;
  auto (*get_starting_message)(FileListGenerator_t* self) -> const char*;
  auto (*get_failure_message)(FileListGenerator_t* self) -> const char*;
  void (*destroy)(FileListGenerator_t* self);
};

auto file_browser_is_extension_supported(const char* filename,
                                         const char* allowed_extensions)
    -> bool;

auto file_browser_create_local_generator(const char* directory,
                                         const char* filter_extensions)
    -> FileListGenerator_t*;

auto file_browser_create_ftp_generator(const char* directory,
                                       const char* filter_extensions)
    -> FileListGenerator_t*;

auto file_browser_create_list(void) -> FileList_t*;
void FileBrowser_FreeList(FileList_t* list);
void FileBrowser_AppendEntry(FileList_t* list, const FileEntry_t* entry);
void FileBrowser_SetFailureMessage(FileList_t* list, const char* msg);
void FileBrowser_SortList(FileList_t* list);

auto file_browser_get_count(const FileList_t* list) -> size_t;
auto file_browser_get_entry(const FileList_t* list, size_t index)
    -> const FileEntry_t*;
auto file_browser_get_failure_message(const FileList_t* list) -> const char*;

#ifdef __cplusplus
}  // extern "C"
#endif

// NOLINTEND(modernize-use-auto, modernize-use-trailing-return-type)
// NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
// NOLINTEND(cppcoreguidelines-use-enum-class)
// NOLINTEND(modernize-deprecated-headers, hicpp-deprecated-headers,
// modernize-use-using)
