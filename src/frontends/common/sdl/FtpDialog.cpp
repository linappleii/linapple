// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/FtpDialog.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <vector>

#include "apple2/peripherals/Peripheral.h"
#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "core/services/ftp/FtpClient.h"
#include "core/services/ftp/FtpTypes.h"
#include "frontends/common/FileBrowser.h"
#include "frontends/common/sdl/DiskChoose_Decl.h"

namespace {

constexpr size_t EXT_LIST_CAP = 256;
constexpr int HARDDISK_SLOT = 7;

struct FtpGeneratorContext_t {
  std::string directory;
  std::string filter_extensions;
  std::string failure_message;
};

auto ftp_gen_generate(FileListGenerator_t* self) -> FileList_t* {
  if (self == nullptr || self->context == nullptr) {
    return nullptr;
  }
  auto* ctx = static_cast<FtpGeneratorContext_t*>(self->context);

  FileList_t* list = file_browser_create_list();
  if (list == nullptr) {
    return nullptr;
  }

#if ENABLE_FTP
  FtpClient_t client;
  std::vector<FtpFileEntry_t> entries;
  const FtpStatus_t status = client.fetch_directory_listing(
      ctx->directory, entries, g_state.ftp_user_pass.data());

  if (status != FtpStatus_t::ok) {
    if (status == FtpStatus_t::connect_error) {
      ctx->failure_message =
          "Failed to connect to FTP server: " + ctx->directory;
    } else if (status == FtpStatus_t::timeout) {
      ctx->failure_message = "FTP connection timed out: " + ctx->directory;
    } else {
      ctx->failure_message = "Failed getting FTP directory: " + ctx->directory;
    }
    file_browser_set_failure_message(list, ctx->failure_message.c_str());
    file_browser_free_list(list);
    return nullptr;
  }

  if (ctx->directory != "ftp://" && ctx->directory != "ftp:///") {
    FileEntry_t up_entry{};
    up_entry.name[0] = '\0';
    util_safe_strcpy(up_entry.name, "..", sizeof(up_entry.name));
    up_entry.type = FILE_ENTRY_UP;
    up_entry.size = 0;
    file_browser_append_entry(list, &up_entry);
  }

  for (const auto& entry : entries) {
    const std::string safe_name = Path::sanitize_filename(entry.name);
    if (safe_name.empty()) {
      continue;
    }

    FileEntry_t ui_entry{};
    ui_entry.name[0] = '\0';
    util_safe_strcpy(ui_entry.name, safe_name.c_str(), sizeof(ui_entry.name));

    if (entry.type == FtpEntryType_t::directory) {
      ui_entry.type = FILE_ENTRY_DIR;
      ui_entry.size = 0;
      file_browser_append_entry(list, &ui_entry);
    } else if (entry.type == FtpEntryType_t::file) {
      if (file_browser_is_extension_supported(safe_name.c_str(),
                                              ctx->filter_extensions.c_str())) {
        ui_entry.type = FILE_ENTRY_FILE;
        ui_entry.size = static_cast<std::uintmax_t>(entry.size);
        file_browser_append_entry(list, &ui_entry);
      }
    }
  }

  file_browser_sort_list(list);
  return list;
#else
  ctx->failure_message = "FTP support is disabled in this build";
  file_browser_set_failure_message(list, ctx->failure_message.c_str());
  file_browser_free_list(list);
  return nullptr;
#endif
}

auto ftp_gen_get_start_msg(FileListGenerator_t* self) -> const char* {
  (void)self;
  return "Connecting to FTP server... Please wait.";
}

auto ftp_gen_get_fail_msg(FileListGenerator_t* self) -> const char* {
  if (self == nullptr || self->context == nullptr) {
    return "(no info)";
  }
  auto* ctx = static_cast<FtpGeneratorContext_t*>(self->context);
  return ctx->failure_message.c_str();
}

auto ftp_gen_destroy(FileListGenerator_t* self) -> void {
  if (self != nullptr) {
    delete static_cast<FtpGeneratorContext_t*>(self->context);
    delete self;
  }
}

}  // namespace


auto file_browser_create_ftp_generator(const char* directory,
                                       const char* filter_extensions)
    -> FileListGenerator_t* {
  if (directory == nullptr) {
    return nullptr;
  }

  auto* gen = new (std::nothrow) FileListGenerator_t();
  if (gen == nullptr) {
    return nullptr;
  }

  auto* ctx = new (std::nothrow) FtpGeneratorContext_t();
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
  gen->generate_file_list = ftp_gen_generate;
  gen->get_starting_message = ftp_gen_get_start_msg;
  gen->get_failure_message = ftp_gen_get_fail_msg;
  gen->destroy = ftp_gen_destroy;

  return gen;
}


auto choose_an_image_ftp(int sx, int sy, const std::string& ftp_dir, int slot,
                         std::string& filename, bool& isdir, size_t& index_file)
    -> bool {
#if ENABLE_FTP
  char supported_exts[EXT_LIST_CAP] = {};
  size_t exts_size = sizeof(supported_exts);
  if (slot == HARDDISK_SLOT) {
    (void)peripheral_query(HARDDISK_SLOT, harddisk_query_supported_extensions,
                           supported_exts, &exts_size);
  } else {
    (void)peripheral_query(slot, disk_query_supported_extensions,
                           supported_exts, &exts_size);
  }

  FileListGenerator_t* generator =
      file_browser_create_ftp_generator(ftp_dir.c_str(), supported_exts);
  if (generator == nullptr) {
    return false;
  }
  const bool result = choose_image_dialog(sx, sy, ftp_dir, slot, generator,
                                          filename, isdir, index_file);
  generator->destroy(generator);
  return result;
#else
  (void)sx;
  (void)sy;
  (void)ftp_dir;
  (void)slot;
  (void)filename;
  (void)isdir;
  (void)index_file;
  return false;
#endif
}
