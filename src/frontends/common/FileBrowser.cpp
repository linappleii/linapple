#include "frontends/common/FileBrowser.h"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "apple2/Apple2Types.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"

static constexpr uint64_t size_k = 1000U;
static constexpr uint64_t size_m = 1000000U;
static constexpr uint64_t size_g = 1000000000U;
static constexpr uint64_t size_block = 1024U;

// Security limit: maximum number of files to list in a single directory
// to prevent OOM / DoS from huge directories (e.g. /proc or malicious mounts)
static constexpr size_t max_directory_entries = 10000;

struct FileList_t {
  std::vector<FileEntry_t> entries;
  std::string failure_message;
};

struct LocalGeneratorContext_t {
  std::string directory;
  std::string failure_message;
};

// --- Helper Functions ---

static auto getstat(const char* catalog, const char* fname, uintmax_t* size)
    -> int {
  if (catalog == nullptr || fname == nullptr) {
    return 0;
  }

  struct stat info{};
  std::array<char, path_max_len> tempname = {};

  int written =
      snprintf(tempname.data(), tempname.size(), "%s/%s", catalog, fname);
  if (written < 0 || static_cast<size_t>(written) >= tempname.size()) {
    return 0;
  }

  if (stat(tempname.data(), &info) == -1) {
    return 0;
  }
  if (S_ISDIR(info.st_mode)) {
    return 1;
  }
  if (S_ISREG(info.st_mode)) {
    if (size != nullptr) {
      if (info.st_size < 0) {
        *size = 0;
      } else {
        *size = static_cast<uintmax_t>(info.st_size) / size_block;
      }
    }
    return 2;
  }
  return 0;
}

static auto get_sorted_directory(const char* incoming_dir,
                                 std::vector<FileEntry_t>& file_list) -> bool {
  if (incoming_dir == nullptr) {
    return false;
  }

  DIR* dp = opendir(incoming_dir);
  if (dp == nullptr) {
    return false;
  }

  struct dirent* entry = nullptr;
  while (true) {
    entry = readdir(dp);
    if (entry == nullptr) {
      break;
    }

    if (file_list.size() >= max_directory_entries) {
      break;
    }

    const char* file_name = entry->d_name;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (file_name == nullptr || strlen(file_name) < 1 || file_name[0] == '.') {
      continue;
    }

    uintmax_t fsize = 0;
    const int what = getstat(incoming_dir, file_name, &fsize);

    FileEntry_t new_entry = {};
    new_entry.name[0] = '\0';
    Util_SafeStrCpy(new_entry.name, file_name, sizeof(new_entry.name));

    if (what == 1) {
      new_entry.type = FILE_ENTRY_DIR;
      new_entry.size = 0;
      file_list.push_back(new_entry);
    } else if (what == 2) {
      new_entry.type = FILE_ENTRY_FILE;
      new_entry.size = static_cast<uint64_t>(fsize) * size_block;
      file_list.push_back(new_entry);
    }
  }
  closedir(dp);

  std::sort(file_list.begin(), file_list.end(),
            [](const FileEntry_t& a, const FileEntry_t& b) -> bool {
              if (a.type < b.type) {
                return true;
              }
              if (a.type > b.type) {
                return false;
              }
              return strcasecmp(&a.name[0], &b.name[0]) < 0;
            });
  return true;
}

static auto local_gen_generate(FileListGenerator_t* self) -> FileList_t* {
  if (self == nullptr || self->context == nullptr) {
    return nullptr;
  }
  auto* ctx = static_cast<LocalGeneratorContext_t*>(self->context);

  auto* list = new (std::nothrow) FileList_t();
  if (list == nullptr) {
    return nullptr;
  }

  if (ctx->directory != "/") {
    FileEntry_t up_entry = {};
    Util_SafeStrCpy(up_entry.name, "..", sizeof(up_entry.name));
    up_entry.type = FILE_ENTRY_UP;
    up_entry.size = 0;
    list->entries.push_back(up_entry);
  }

  if (get_sorted_directory(ctx->directory.c_str(), list->entries)) {
    if (list->entries.size() >= max_directory_entries) {
      ctx->failure_message = "Directory too large, listing truncated.";
    }
    list->failure_message = ctx->failure_message;
    return list;
  }

  ctx->failure_message = "Failed to list directory: " + ctx->directory;
  list->failure_message = ctx->failure_message;
  list->entries.clear();
  return list;
}

static auto local_gen_get_start_msg(FileListGenerator_t* self) -> const char* {
  (void)self;
  return "Reading directory listing...";
}

static auto local_gen_get_fail_msg(FileListGenerator_t* self) -> const char* {
  if (self == nullptr || self->context == nullptr) {
    return "(no info)";
  }
  auto* ctx = static_cast<LocalGeneratorContext_t*>(self->context);
  return ctx->failure_message.c_str();
}

static void LocalGen_Destroy(FileListGenerator_t* self) {
  if (self != nullptr) {
    delete static_cast<LocalGeneratorContext_t*>(self->context);
    delete self;
  }
}

// --- Public C ABI ---

extern "C" {

auto file_entry_is_dir_type(const FileEntry_t* entry) -> bool {
  if (entry == nullptr) {
    return false;
  }
  return entry->type == FILE_ENTRY_UP || entry->type == FILE_ENTRY_DIR;
}

void FileEntry_FormatTypeOrSize(const FileEntry_t* entry, char* out_str,
                                size_t max_len) {
  if (entry == nullptr || out_str == nullptr || max_len == 0) {
    return;
  }

  switch (entry->type) {
    case FILE_ENTRY_UP:
      Util_SafeStrCpy(out_str, "<UP>", max_len);
      break;
    case FILE_ENTRY_DIR:
      Util_SafeStrCpy(out_str, "<DIR>", max_len);
      break;
    case FILE_ENTRY_FILE: {
      uint64_t s = entry->size;
      const char* suffix = "";
      if (size_k > s) {
      } else if (size_m > s) {
        s /= size_k;
        suffix = "K";
      } else if (size_g > s) {
        s /= size_m;
        suffix = "M";
      } else {
        s /= size_g;
        suffix = "G";
      }
      snprintf(out_str, max_len, "%llu%s", static_cast<unsigned long long>(s),
               suffix);
      break;
    }
    default:
      Util_SafeStrCpy(out_str, "???", max_len);
      break;
  }
}

void FileBrowser_FreeList(FileList_t* list) { delete list; }

auto file_browser_create_list(void) -> FileList_t* {
  return new (std::nothrow) FileList_t();
}

void FileBrowser_AppendEntry(FileList_t* list, const FileEntry_t* entry) {
  if (list != nullptr && entry != nullptr) {
    list->entries.push_back(*entry);
  }
}

void FileBrowser_SetFailureMessage(FileList_t* list, const char* msg) {
  if (list != nullptr && msg != nullptr) {
    list->failure_message = msg;
  }
}

void FileBrowser_SortList(FileList_t* list) {
  if (list != nullptr) {
    std::sort(list->entries.begin(), list->entries.end(),
              [](const FileEntry_t& a, const FileEntry_t& b) -> bool {
                if (a.type < b.type) {
                  return true;
                }
                if (a.type > b.type) {
                  return false;
                }
                return strcasecmp(&a.name[0], &b.name[0]) < 0;
              });
  }
}

auto file_browser_get_count(const FileList_t* list) -> size_t {
  return list != nullptr ? list->entries.size() : 0;
}

auto file_browser_get_entry(const FileList_t* list, size_t index)
    -> const FileEntry_t* {
  if (list == nullptr || index >= list->entries.size()) {
    return nullptr;
  }
  return &list->entries.at(index);
}

auto file_browser_get_failure_message(const FileList_t* list) -> const char* {
  return list != nullptr ? list->failure_message.c_str() : "Null list handle";
}

auto file_browser_create_local_generator(const char* directory)
    -> FileListGenerator_t* {
  if (directory == nullptr) {
    return nullptr;
  }

  auto* gen = new (std::nothrow) FileListGenerator_t();
  if (gen == nullptr) {
    return nullptr;
  }

  auto* ctx = new (std::nothrow) LocalGeneratorContext_t();
  if (ctx == nullptr) {
    delete gen;
    return nullptr;
  }

  ctx->directory = directory;
  ctx->failure_message = "(success)";

  gen->context = ctx;
  gen->generate_file_list = local_gen_generate;
  gen->get_starting_message = local_gen_get_start_msg;
  gen->get_failure_message = local_gen_get_fail_msg;
  gen->destroy = LocalGen_Destroy;

  return gen;
}

}  // extern "C"
