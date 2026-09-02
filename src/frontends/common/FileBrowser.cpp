#include "frontends/common/FileBrowser.h"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Peripheral.h"
#include "core/Peripheral_Types.h"
#include "core/Registry.h"
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
  std::string filter_extensions;
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
                                 const char* filter_extensions,
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
    util_safe_strcpy(new_entry.name, file_name, sizeof(new_entry.name));

    if (what == 1) {
      new_entry.type = FILE_ENTRY_DIR;
      new_entry.size = 0;
      file_list.push_back(new_entry);
    } else if (what == 2) {
      if (file_browser_is_extension_supported(file_name, filter_extensions)) {
        new_entry.type = FILE_ENTRY_FILE;
        new_entry.size = static_cast<uint64_t>(fsize) * size_block;
        file_list.push_back(new_entry);
      }
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
    util_safe_strcpy(up_entry.name, "..", sizeof(up_entry.name));
    up_entry.type = FILE_ENTRY_UP;
    up_entry.size = 0;
    list->entries.push_back(up_entry);
  }

  if (get_sorted_directory(ctx->directory.c_str(),
                           ctx->filter_extensions.c_str(), list->entries)) {
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

static void local_gen_destroy(FileListGenerator_t* self) {
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

void file_entry_format_type_or_size(const FileEntry_t* entry, char* out_str,
                                    size_t max_len) {
  if (entry == nullptr || out_str == nullptr || max_len == 0) {
    return;
  }

  switch (entry->type) {
    case FILE_ENTRY_UP:
      util_safe_strcpy(out_str, "<UP>", max_len);
      break;
    case FILE_ENTRY_DIR:
      util_safe_strcpy(out_str, "<DIR>", max_len);
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
      util_safe_strcpy(out_str, "???", max_len);
      break;
  }
}

void file_browser_free_list(FileList_t* list) { delete list; }

auto file_browser_create_list(void) -> FileList_t* {
  return new (std::nothrow) FileList_t();
}

void file_browser_append_entry(FileList_t* list, const FileEntry_t* entry) {
  if (list != nullptr && entry != nullptr) {
    list->entries.push_back(*entry);
  }
}

void file_browser_set_failure_message(FileList_t* list, const char* msg) {
  if (list != nullptr && msg != nullptr) {
    list->failure_message = msg;
  }
}

void file_browser_sort_list(FileList_t* list) {
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

auto file_browser_is_extension_supported(const char* filename,
                                         const char* allowed_extensions)
    -> bool {
  if (filename == nullptr) {
    return false;
  }
  if (allowed_extensions == nullptr || allowed_extensions[0] == '\0') {
    return true;
  }

  const char* dot = strrchr(filename, '.');
  if (dot == nullptr || dot[1] == '\0') {
    return false;
  }
  const char* ext = dot + 1;

  const char* p = allowed_extensions;
  while (*p != '\0') {
    while (*p == ';' || *p == ' ' || *p == ',') {
      ++p;
    }
    if (*p == '\0') {
      break;
    }
    const char* start = p;
    while (*p != '\0' && *p != ';' && *p != ' ' && *p != ',') {
      ++p;
    }
    const size_t len = static_cast<size_t>(p - start);
    if (len > 0 && strncasecmp(ext, start, len) == 0 && ext[len] == '\0') {
      return true;
    }
  }
  return false;
}

auto file_browser_create_local_generator(const char* directory,
                                         const char* filter_extensions)
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
  if (filter_extensions != nullptr) {
    ctx->filter_extensions = filter_extensions;
  }
  ctx->failure_message = "(success)";

  gen->context = ctx;
  gen->generate_file_list = local_gen_generate;
  gen->get_starting_message = local_gen_get_start_msg;
  gen->get_failure_message = local_gen_get_fail_msg;
  gen->destroy = local_gen_destroy;

  return gen;
}

auto disk_browser_open(DiskBrowser_t* b, int slot, int drive,
                       const char* start_dir) -> bool {
  if (b == nullptr) return false;
  b->slot = slot;
  b->drive = drive;
  b->is_active = true;
  b->selected_index = 0;
  b->first_visible_index = 0;
  b->list_handle = nullptr;
  b->generator = nullptr;

  if (start_dir != nullptr && start_dir[0] != '\0') {
    util_safe_strcpy(b->current_dir, start_dir, sizeof(b->current_dir));
  } else if (b->slot == 7 && g_state.hdd_dir.at(0) != '\0') {
    util_safe_strcpy(b->current_dir, g_state.hdd_dir.data(),
                     sizeof(b->current_dir));
  } else if (g_state.current_dir.at(0) != '\0') {
    util_safe_strcpy(b->current_dir, g_state.current_dir.data(),
                     sizeof(b->current_dir));
  } else {
    util_safe_strcpy(b->current_dir, ".", sizeof(b->current_dir));
  }

  // Strip trailing slashes (except root "/")
  size_t dlen = strlen(b->current_dir);
  while (dlen > 1 && b->current_dir[dlen - 1] == '/') {
    b->current_dir[dlen - 1] = '\0';
    dlen--;
  }

  disk_browser_refresh(b);
  return true;
}

void disk_browser_close(DiskBrowser_t* b) {
  if (b == nullptr) return;
  b->is_active = false;
  if (b->list_handle != nullptr) {
    file_browser_free_list(b->list_handle);
    b->list_handle = nullptr;
  }
  if (b->generator != nullptr) {
    b->generator->destroy(b->generator);
    b->generator = nullptr;
  }
}

void disk_browser_refresh(DiskBrowser_t* b) {
  if (b == nullptr) return;
  if (b->list_handle != nullptr) {
    file_browser_free_list(b->list_handle);
    b->list_handle = nullptr;
  }
  if (b->generator != nullptr) {
    b->generator->destroy(b->generator);
    b->generator = nullptr;
  }

  char supported_exts[256] = {};
  size_t exts_size = sizeof(supported_exts);
  if (b->slot == 7) {
    (void)peripheral_query(7, harddisk_cmd_get_supported_extensions,
                           supported_exts, &exts_size);
  } else {
    (void)peripheral_query(b->slot != 0 ? b->slot : disk_default_slot,
                           disk_cmd_get_supported_extensions, supported_exts,
                           &exts_size);
  }

  b->generator =
      file_browser_create_local_generator(b->current_dir, supported_exts);
  if (b->generator != nullptr) {
    b->list_handle = b->generator->generate_file_list(b->generator);
  }
  b->selected_index = 0;
  b->first_visible_index = 0;
}

void disk_browser_move(DiskBrowser_t* b, int delta, size_t page_size) {
  if (b == nullptr || !b->is_active || b->list_handle == nullptr) return;
  size_t count = file_browser_get_count(b->list_handle);
  if (count == 0) return;

  if (delta < 0) {
    if (b->selected_index > 0) {
      b->selected_index--;
    }
    if (b->selected_index < b->first_visible_index) {
      b->first_visible_index = b->selected_index;
    }
  } else if (delta > 0) {
    if (b->selected_index + 1 < count) {
      b->selected_index++;
    }
    if (b->selected_index >= b->first_visible_index + page_size) {
      b->first_visible_index = b->selected_index - page_size + 1;
    }
  }
}

void disk_browser_page(DiskBrowser_t* b, int direction, size_t page_size) {
  if (b == nullptr || !b->is_active || b->list_handle == nullptr ||
      page_size == 0)
    return;
  size_t count = file_browser_get_count(b->list_handle);
  if (count == 0) return;

  if (direction < 0) {
    if (b->selected_index <= page_size) {
      b->selected_index = 0;
    } else {
      b->selected_index -= page_size;
    }
    if (b->selected_index < b->first_visible_index) {
      b->first_visible_index = b->selected_index;
    }
  } else {
    b->selected_index += page_size;
    if (b->selected_index >= count) {
      b->selected_index = count - 1;
    }
    if (b->selected_index >= b->first_visible_index + page_size) {
      b->first_visible_index = b->selected_index - page_size + 1;
    }
  }
}

void disk_browser_home(DiskBrowser_t* b) {
  if (b == nullptr) return;
  b->selected_index = 0;
  b->first_visible_index = 0;
}

void disk_browser_end(DiskBrowser_t* b, size_t page_size) {
  if (b == nullptr || !b->is_active || b->list_handle == nullptr) return;
  size_t count = file_browser_get_count(b->list_handle);
  if (count == 0) return;
  b->selected_index = count - 1;
  if (b->selected_index <= page_size - 1) {
    b->first_visible_index = 0;
  } else {
    b->first_visible_index = b->selected_index - page_size + 1;
  }
}

void disk_browser_jump_char(DiskBrowser_t* b, char ch, size_t page_size) {
  if (b == nullptr || !b->is_active || b->list_handle == nullptr ||
      page_size == 0)
    return;
  size_t count = file_browser_get_count(b->list_handle);
  if (count == 0) return;

  for (size_t i = 0; i < count; ++i) {
    const FileEntry_t* entry = file_browser_get_entry(b->list_handle, i);
    if (entry != nullptr && entry->name[0] != '\0') {
      if (std::toupper(static_cast<unsigned char>(entry->name[0])) ==
          std::toupper(static_cast<unsigned char>(ch))) {
        b->selected_index = i;
        if (b->selected_index < b->first_visible_index) {
          b->first_visible_index = b->selected_index;
        } else if (b->selected_index >= b->first_visible_index + page_size) {
          b->first_visible_index = b->selected_index - page_size + 1;
        }
        break;
      }
    }
  }
}

auto disk_browser_confirm(DiskBrowser_t* b) -> bool {
  if (b == nullptr || !b->is_active || b->list_handle == nullptr) return false;
  size_t count = file_browser_get_count(b->list_handle);
  if (b->selected_index >= count) return false;

  const FileEntry_t* entry =
      file_browser_get_entry(b->list_handle, b->selected_index);
  if (entry == nullptr) return false;

  if (entry->type == FILE_ENTRY_UP || strcmp(entry->name, "..") == 0) {
    std::string dir = b->current_dir;
    while (dir.size() > 1 && dir.back() == '/') {
      dir.pop_back();
    }
    const auto last_sep = dir.find_last_of(file_separator);
    if (last_sep != std::string::npos) {
      dir = dir.substr(0, last_sep);
    }
    if (dir.empty()) dir = "/";
    util_safe_strcpy(b->current_dir, dir.c_str(), sizeof(b->current_dir));
    disk_browser_refresh(b);
    return false;
  }

  if (file_entry_is_dir_type(entry)) {
    std::string dir = b->current_dir;
    if (dir != "/") {
      dir += "/" + std::string(entry->name);
    } else {
      dir = "/" + std::string(entry->name);
    }
    util_safe_strcpy(b->current_dir, dir.c_str(), sizeof(b->current_dir));
    disk_browser_refresh(b);
    return false;
  }

  // File entry selected - build full path
  std::string full_path = b->current_dir;
  if (full_path != "/") {
    full_path += "/" + std::string(entry->name);
  } else {
    full_path = "/" + std::string(entry->name);
  }

  if (b->slot == 7) {
    util_safe_strcpy(g_state.hdd_dir.data(), b->current_dir,
                     g_state.hdd_dir.size());
    Configuration_t::instance().set_string(
        "Preferences", REGVALUE_PREF_HDD_START_DIR, g_state.hdd_dir.data());
    Configuration_t::instance().save();

    HarddiskInsertCmd_t hcmd{};
    hcmd.drive = static_cast<uint8_t>(b->drive);
    util_safe_strcpy(hcmd.path, full_path.c_str(), sizeof(hcmd.path));
    if (peripheral_command(7, harddisk_cmd_insert, &hcmd, sizeof(hcmd)) ==
        peripheral_ok) {
      if (b->drive != 0) {
        Configuration_t::instance().set_string(
            "Preferences", REGVALUE_HDD_IMAGE2, full_path.c_str());
      } else {
        Configuration_t::instance().set_string(
            "Preferences", REGVALUE_HDD_IMAGE1, full_path.c_str());
      }
      Configuration_t::instance().save();
    }

    disk_browser_close(b);
    return true;
  }

  // Update current_dir and save to Preferences
  util_safe_strcpy(g_state.current_dir.data(), b->current_dir,
                   g_state.current_dir.size());
  Configuration_t::instance().set_string("Preferences", REGVALUE_PREF_START_DIR,
                                         g_state.current_dir.data());
  Configuration_t::instance().save();

  // Mount image into hardware
  DiskInsertCmd_t cmd{};
  cmd.drive = static_cast<uint8_t>(b->drive);
  util_safe_strcpy(cmd.path, full_path.c_str(), sizeof(cmd.path));
  cmd.write_protected = 0;
  cmd.create_if_necessary = 1;
  peripheral_command(b->slot != 0 ? b->slot : disk_default_slot,
                     disk_cmd_insert, &cmd, sizeof(cmd));

  disk_browser_close(b);
  return true;
}

auto disk_browser_get_title(int slot) -> const char* {
  if (slot == 6) {
    return "Choose image for floppy 140KB drive";
  }
  if (slot == 7) {
    return "Choose image for Hard Disk";
  }
  if (slot == 5) {
    return "Choose image for floppy 800KB drive";
  }
  if (slot == 1) {
    return "Select file name for saving snapshot";
  }
  if (slot == 0) {
    return "Select snapshot file name for loading";
  }
  return "Choose disk image";
}

}  // extern "C"
