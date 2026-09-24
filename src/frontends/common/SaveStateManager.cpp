// SPDX-License-Identifier: GPL-2.0-only
#include "frontends/common/SaveStateManager.h"

#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "apple2/Snapshot.h"
#include "apple2/SnapshotTypes.h"
#include "core/LinAppleCore.h"
#include "core/Log.h"
#include "core/Util_Path.h"

constexpr const char* default_snapshot_name = "SaveState.aws";

bool g_save_state_on_exit = false;

static char g_save_state_filename[path_max_len] = {0};

auto save_state_get_filename() -> char* { return g_save_state_filename; }

auto save_state_set_filename(const char* filename) -> void {
  if (filename && *filename) {
    snprintf(g_save_state_filename, sizeof(g_save_state_filename), "%s",
             filename);
  } else {
    g_save_state_filename[0] = '\0';
  }
}

// Differentiate snapshot format with slot trailer by file length.
static auto snapshot_layout_size(size_t file_size) -> size_t {
  if (file_size == snapshot_size_fixed_body ||
      file_size == sizeof(ApplewinSnapshot_t)) {
    return file_size;
  }
  return 0;
}

auto save_state_load() -> bool {
  auto snapshot = std::unique_ptr<ApplewinSnapshot_t>(new ApplewinSnapshot_t());

  const char* filename = g_save_state_filename;
  if (*filename == '\0') {
    filename = default_snapshot_name;
  }

  FilePtr_t file{fopen(filename, "rb"), fclose};
  if (!file) {
    Logger::error("Failed to open save state file for reading: %s\n", filename);
    return false;
  }

  const size_t header_read =
      fread(&snapshot->hdr, 1, sizeof(snapshot->hdr), file.get());
  if (header_read != sizeof(snapshot->hdr)) {
    Logger::error("Save state file %s is shorter than its header\n", filename);
    return false;
  }

  if (snapshot->hdr.tag != static_cast<uint32_t>(aw_ss_tag)) {
    Logger::error("Invalid save state file format or tag mismatch in %s\n",
                  filename);
    return false;
  }

  if (snapshot->hdr.version != snapshot_version) {
    Logger::error("Version mismatch in save state file %s\n", filename);
    return false;
  }

  auto* body = reinterpret_cast<uint8_t*>(snapshot.get()) + header_read;
  const size_t body_read =
      fread(body, 1, sizeof(ApplewinSnapshot_t) - header_read, file.get());
  const bool at_end = (fgetc(file.get()) == EOF);
  file.reset();

  const size_t file_size = header_read + body_read;
  if (!at_end || snapshot_layout_size(file_size) == 0) {
    Logger::error(
        "Save state file %s is %s%zu bytes; a save state is %zu bytes, or %zu "
        "with the slot trailer\n",
        filename, at_end ? "" : "more than ", file_size,
        snapshot_size_fixed_body, sizeof(ApplewinSnapshot_t));
    return false;
  }

  if (!snapshot_deserialize(snapshot.get())) {
    Logger::error("Failed to deserialize machine state from %s\n", filename);
    return false;
  }

  Logger::info("Loaded save state from: %s\n", filename);
  return true;
}

auto save_state_save() -> void {
  auto snapshot = std::unique_ptr<ApplewinSnapshot_t>(new ApplewinSnapshot_t());

  snapshot_serialize(snapshot.get());

  const char* filename = g_save_state_filename;
  if (*filename == '\0') {
    filename = default_snapshot_name;
  }

  const std::string temp_filename = std::string(filename) + ".tmp";

  FilePtr_t file{fopen(temp_filename.c_str(), "wb"), fclose};
  if (!file) {
    Logger::error("Failed to open save state file for writing: %s\n",
                  temp_filename.c_str());
    return;
  }

  const size_t bytes_written =
      fwrite(snapshot.get(), 1, sizeof(ApplewinSnapshot_t), file.get());
  const bool flush_ok = (fflush(file.get()) == 0);
  file.reset();

  if (bytes_written != sizeof(ApplewinSnapshot_t) || !flush_ok) {
    unlink(temp_filename.c_str());
    Logger::error(
        "Failed to write complete save state data to %s (wrote %zu of %zu "
        "bytes)\n",
        filename, bytes_written, sizeof(ApplewinSnapshot_t));
    return;
  }

  if (std::rename(temp_filename.c_str(), filename) != 0) {
    unlink(temp_filename.c_str());
    Logger::error("Failed to commit save state file to: %s\n", filename);
    return;
  }

  Logger::info("Saved state to: %s\n", filename);
}

auto save_state_startup() -> void {
  static bool done = false;
  if (done) return;

  if (g_save_state_filename[0] != '\0') {
    save_state_load();
  } else if (g_save_state_on_exit) {
    if (access(default_snapshot_name, F_OK) == 0) {
      save_state_set_filename(default_snapshot_name);
      save_state_load();
    }
  }

  done = true;
}

auto save_state_shutdown() -> void {
  static bool done = false;
  if (!g_save_state_on_exit || done) return;

  save_state_save();
  done = true;
}
