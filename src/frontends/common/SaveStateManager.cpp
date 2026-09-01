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

auto save_state_load() -> void {
  auto snapshot = std::unique_ptr<ApplewinSnapshot_t>(new ApplewinSnapshot_t());

  const char* filename = g_save_state_filename;
  if (*filename == '\0') {
    filename = default_snapshot_name;
  }

  FilePtr_t file{fopen(filename, "rb"), fclose};
  if (!file) {
    Logger::error("Failed to open save state file for reading: %s\n", filename);
    return;
  }

  size_t bytes_read =
      fread(snapshot.get(), 1, sizeof(ApplewinSnapshot_t), file.get());
  file.reset();

  if (bytes_read != sizeof(ApplewinSnapshot_t)) {
    Logger::error(
        "Failed to read complete save state data from %s (read %zu of %zu "
        "bytes)\n",
        g_save_state_filename, bytes_read, sizeof(ApplewinSnapshot_t));
    return;
  }

  if (snapshot->hdr.tag != static_cast<uint32_t>(aw_ss_tag)) {
    Logger::error("Invalid save state file format or tag mismatch in %s\n",
                  g_save_state_filename);
    return;
  }

  if (snapshot->hdr.version != make_version(1, 0, 0, 1)) {
    Logger::error("Version mismatch in save state file %s\n",
                  g_save_state_filename);
    return;
  }

  if (!snapshot_deserialize(snapshot.get())) {
    Logger::error("Failed to deserialize machine state from %s\n",
                  g_save_state_filename);
  } else {
    Logger::info("Loaded save state from: %s\n", g_save_state_filename);
  }
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
