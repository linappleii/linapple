#include "apple2/peripherals/disk/DiskFTP.h"

#include <sys/stat.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <new>
#include <string>

#include "apple2/peripherals/disk/DiskCommands.h"
#include "apple2/peripherals/disk/ftpparse.h"
#include "apple2/peripherals/harddisk/HarddiskCommands.h"
#include "core/LinAppleCore.h"
#include "core/Registry.h"
#include "core/Util_Path.h"
#include "core/Util_Text.h"
#include "frontends/common/FileBrowser.h"
#include "frontends/common/Util_Hash.h"
#include "frontends/sdl1/DiskChoose.h"

// how many file names we are able to see at once!

// delay after key pressed (in milliseconds??)

// define time when cache ftp dir.listing must be refreshed
static constexpr int renew_time = 24 * 3600;

static std::array<char, 512> g_ftp_dir_listing = {
    {"cache/ftp."}};  // name for FTP-directory listing

struct FtpGeneratorContext_t {
  std::string directory;
  std::string filter_extensions;
  std::string failure_message;
};

static FileList_t* FTPGen_Generate(FileListGenerator_t* self) {
  if (!self || !self->context) return nullptr;
  auto* ctx = static_cast<FtpGeneratorContext_t*>(self->context);

  FileList_t* list = file_browser_create_list();
  if (!list) return nullptr;

  std::array<char, 1024> ftpdirpath;
  int l = snprintf(ftpdirpath.data(), ftpdirpath.size(), "%s/%s%s",
                   g_state.ftp_local_dir.data(), g_ftp_dir_listing.data(),
                   md5str(ctx->directory.c_str()));

  if (l < 0 || static_cast<size_t>(l) >= ftpdirpath.size()) {
    ctx->failure_message = "Failed get path for FTP dir listing";
    FileBrowser_SetFailureMessage(list, ctx->failure_message.c_str());
    return list;
  }

  bool OKI = false;
  struct stat info{};
  if (stat(ftpdirpath.data(), &info) == 0 &&
      info.st_mtime > time(nullptr) - renew_time) {
    OKI = false;
  } else {
    OKI = ftp_get(ctx->directory.c_str(), ftpdirpath.data());
  }

  if (OKI) {
    ctx->failure_message = "Failed getting FTP directory " + ctx->directory +
                           " to " + std::string(ftpdirpath.data());
    FileBrowser_SetFailureMessage(list, ctx->failure_message.c_str());
    return list;
  }

  FilePtr_t fdir(fopen(ftpdirpath.data(), "r"), fclose);
  if (!fdir) {
    ctx->failure_message = "Failed to open FTP directory listing file: " +
                           std::string(ftpdirpath.data());
    FileBrowser_SetFailureMessage(list, ctx->failure_message.c_str());
    return list;
  }

  if (ctx->directory != "ftp://") {
    FileEntry_t up_entry{};
    up_entry.name[0] = '\0';
    Util_SafeStrCpy(up_entry.name, "..", sizeof(up_entry.name));
    up_entry.type = FILE_ENTRY_UP;
    up_entry.size = 0;
    FileBrowser_AppendEntry(list, &up_entry);
  }

  std::array<char, 1024> line;
  while (fgets(line.data(), line.size(), fdir.get())) {
    struct ftpparse fp;
    if (ftpparse(&fp, line.data(), strlen(line.data()))) {
      std::unique_ptr<char, void (*)(void*)> trimmed_name(
          php_trim(fp.name, fp.namelen), free);

      FileEntry_t entry{};
      entry.name[0] = '\0';
      Util_SafeStrCpy(entry.name, trimmed_name.get(), sizeof(entry.name));

      if (fp.flagtrycwd) {
        entry.type = FILE_ENTRY_DIR;
        entry.size = 0;
        FileBrowser_AppendEntry(list, &entry);
      } else if (fp.flagtryretr) {
        if (file_browser_is_extension_supported(
                trimmed_name.get(), ctx->filter_extensions.c_str())) {
          entry.type = FILE_ENTRY_FILE;
          entry.size = static_cast<std::uintmax_t>(fp.size);
          FileBrowser_AppendEntry(list, &entry);
        }
      }
    }
  }

  FileBrowser_SortList(list);
  return list;
}

static const char* FTPGen_GetStartMsg(FileListGenerator_t* self) {
  (void)self;
  return "Connecting to FTP server... Please wait.";
}

static const char* FTPGen_GetFailMsg(FileListGenerator_t* self) {
  if (!self || !self->context) return "(no info)";
  auto* ctx = static_cast<FtpGeneratorContext_t*>(self->context);
  return ctx->failure_message.c_str();
}

static void FTPGen_Destroy(FileListGenerator_t* self) {
  if (self) {
    delete static_cast<FtpGeneratorContext_t*>(self->context);
    delete self;
  }
}

extern "C" {

FileListGenerator_t* file_browser_create_ftp_generator(
    const char* directory, const char* filter_extensions) {
  if (!directory) return nullptr;

  auto* gen = new (std::nothrow) FileListGenerator_t();
  if (!gen) return nullptr;

  auto* ctx = new (std::nothrow) FtpGeneratorContext_t();
  if (!ctx) {
    delete gen;
    return nullptr;
  }

  ctx->directory = directory;
  if (filter_extensions != nullptr) {
    ctx->filter_extensions = filter_extensions;
  }
  ctx->failure_message = "(success)";

  gen->context = ctx;
  gen->generate_file_list = FTPGen_Generate;
  gen->get_starting_message = FTPGen_GetStartMsg;
  gen->get_failure_message = FTPGen_GetFailMsg;
  gen->destroy = FTPGen_Destroy;

  return gen;
}
}

auto choose_an_image_ftp(int sx, int sy, const std::string& ftp_dir, int slot,
                         std::string& filename, bool& isdir, size_t& index_file)
    -> bool {
  char supported_exts[256] = {};
  size_t exts_size = sizeof(supported_exts);
  if (slot == 7) {
    (void)peripheral_query(7, harddisk_cmd_get_supported_extensions,
                           supported_exts, &exts_size);
  } else {
    (void)peripheral_query(slot, disk_cmd_get_supported_extensions,
                           supported_exts, &exts_size);
  }

  FileListGenerator_t* generator =
      file_browser_create_ftp_generator(ftp_dir.c_str(), supported_exts);
  if (!generator) return false;
  bool result = choose_image_dialog(sx, sy, ftp_dir, slot, generator, filename,
                                    isdir, index_file);
  generator->destroy(generator);
  return result;
}
